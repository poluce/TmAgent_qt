#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QUuid>

#include "core/tools/MemoryTool.h"

static int g_testCount = 0;
static int g_passCount = 0;

#define PRINT_DIVIDER() qDebug().noquote() << "────────────────────────────────────────"
#define PRINT_RESULT(pass) qDebug().noquote() << (pass ? "  ✅ 通过" : "  ❌ 失败")

#define TEST(name) \
    ++g_testCount; \
    PRINT_DIVIDER(); \
    qDebug().noquote() << QString("[测试 %1] %2").arg(g_testCount).arg(name); \
    if (auto result = [&]() -> int

#define END_TEST \
    (); result != 0) { \
        PRINT_RESULT(false); \
    } else { \
        ++g_passCount; \
        PRINT_RESULT(true); \
    }

struct AgentFixture {
    QString agentId;
    QString agentPath;

    AgentFixture()
    {
        agentId = QStringLiteral("memory-tool-test-%1")
                      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        agentPath = QDir(QDir::home().filePath(QStringLiteral(".tmagent/identities/agents")))
                        .filePath(agentId);
    }

    ~AgentFixture()
    {
        if (!agentPath.trimmed().isEmpty())
            QDir(agentPath).removeRecursively();
    }
};

static bool writeTextFile(const QString& path, const QString& text)
{
    QFileInfo info(path);
    QDir().mkpath(info.dir().path());
    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Text | QFile::Truncate))
        return false;
    file.write(text.toUtf8());
    file.close();
    return true;
}

static QStringList extractHitLines(const QString& output)
{
    const int marker = output.indexOf(QStringLiteral("命中列表:\n"));
    if (marker < 0)
        return {};
    const QString body = output.mid(marker + QStringLiteral("命中列表:\n").size());
    QStringList lines;
    const QStringList rawLines = body.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : rawLines) {
        if (line.startsWith(QStringLiteral("- [")))
            lines.append(line.trimmed());
    }
    return lines;
}

static int failWithActual(const QString& actual)
{
    qDebug().noquote() << "  [实际]" << actual;
    return 1;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "       MemoryTool 测试套件";
    qDebug().noquote() << "════════════════════════════════════════";

    AgentFixture fixture;
    const QString memoryPath = QDir(fixture.agentPath).filePath(QStringLiteral("memory.md"));
    const QString userViewPath = QDir(fixture.agentPath).filePath(QStringLiteral("user_view.md"));
    const QDate currentDay = QDate::currentDate();
    const QString newerDailyName = currentDay.toString(Qt::ISODate);
    const QString olderDailyName = currentDay.addDays(-9).toString(Qt::ISODate);
    const QString dailyOldPath = QDir(fixture.agentPath).filePath(QStringLiteral("memory/%1.md").arg(olderDailyName));
    const QString dailyNewPath = QDir(fixture.agentPath).filePath(QStringLiteral("memory/%1.md").arg(newerDailyName));

    if (!writeTextFile(
            memoryPath,
            QStringLiteral(
                "## 2026-03-11T10:00:00.000Z\n"
                "- fp: [fp:test-memory]\n"
                "- source_session_id: `session-1`\n"
                "- reflected: true\n"
                "- memory: 反思提炼：CLI 构建失败时先运行 qmake 再执行 mingw32-make\n"
                "- memory: version changelog must be synced before release\n"
                "- memory: before release, confirm changelog and version are updated\n"
                "- memory: release checklist guardrails must be verified before deployment\n"))
        || !writeTextFile(
            userViewPath,
            QStringLiteral("- 用户偏好：遇到构建失败时先看命令行输出\n"))
        || !writeTextFile(
            dailyOldPath,
            QStringLiteral("- 今天复盘：CLI 构建失败后要检查发布检查清单（旧）\n"))
        || !writeTextFile(
            dailyNewPath,
            QStringLiteral("- 今天复盘：CLI 构建失败后要更新发布检查清单（新）\n"))) {
        qDebug().noquote() << "初始化测试文件失败";
        return 1;
    }

    TEST("memory_reindex - 可为测试助手建立索引") {
        QJsonObject args;
        args.insert(QStringLiteral("scope"), QStringLiteral("self"));
        args.insert(QStringLiteral("agent_id"), fixture.agentId);
        const QString output = MemoryTool::executeRebuild(args);
        if (!output.contains(QStringLiteral("agents_success: 1"))
            || !output.contains(QStringLiteral("rows_indexed:"))) {
            return failWithActual(output);
        }
        return 0;
    } END_TEST

    TEST("memory_search - 多词查询支持短语外 AND 命中") {
        QJsonObject args;
        args.insert(QStringLiteral("scope"), QStringLiteral("self"));
        args.insert(QStringLiteral("agent_id"), fixture.agentId);
        args.insert(QStringLiteral("query"), QStringLiteral("qmake 构建"));
        args.insert(QStringLiteral("max_results"), 5);
        const QString output = MemoryTool::executeSearch(args);
        if (!output.contains(QStringLiteral("backend: sqlite_fts_ranked"))
            || !output.contains(QStringLiteral("memory.md"))
            || !output.contains(QStringLiteral("qmake"))) {
            return failWithActual(output);
        }
        return 0;
    } END_TEST

    TEST("memory_search - SQLite BM25 保持强匹配优先") {
        QJsonObject args;
        args.insert(QStringLiteral("scope"), QStringLiteral("self"));
        args.insert(QStringLiteral("agent_id"), fixture.agentId);
        args.insert(QStringLiteral("query"), QStringLiteral("version changelog"));
        args.insert(QStringLiteral("max_results"), 5);
        const QString output = MemoryTool::executeSearch(args);
        const QStringList lines = extractHitLines(output);
        if (lines.size() < 2
            || !lines.at(0).contains(QStringLiteral("version changelog must be synced before release"))
            || !lines.at(1).contains(QStringLiteral("before release, confirm changelog and version are updated"))) {
            return failWithActual(output);
        }
        return 0;
    } END_TEST

    TEST("memory_search - 语义向量回退可召回部分重叠条目") {
        QJsonObject args;
        args.insert(QStringLiteral("scope"), QStringLiteral("self"));
        args.insert(QStringLiteral("agent_id"), fixture.agentId);
        args.insert(QStringLiteral("query"), QStringLiteral("release checklist approval"));
        args.insert(QStringLiteral("max_results"), 5);
        const QString output = MemoryTool::executeSearch(args);
        const QStringList lines = extractHitLines(output);
        if (lines.isEmpty()
            || !lines.first().contains(QStringLiteral("release checklist guardrails must be verified before deployment"))) {
            return failWithActual(output);
        }
        return 0;
    } END_TEST

    TEST("memory_search - 长期记忆优先于 daily 命中") {
        QJsonObject args;
        args.insert(QStringLiteral("scope"), QStringLiteral("self"));
        args.insert(QStringLiteral("agent_id"), fixture.agentId);
        args.insert(QStringLiteral("query"), QStringLiteral("CLI 构建失败"));
        args.insert(QStringLiteral("max_results"), 5);
        const QString output = MemoryTool::executeSearch(args);
        const QStringList lines = extractHitLines(output);
        if (lines.isEmpty() || !lines.first().contains(QStringLiteral("/memory.md"))) {
            return failWithActual(output);
        }
        return 0;
    } END_TEST

    TEST("memory_search - 近日报志优先于旧日报") {
        QJsonObject args;
        args.insert(QStringLiteral("scope"), QStringLiteral("self"));
        args.insert(QStringLiteral("agent_id"), fixture.agentId);
        args.insert(QStringLiteral("query"), QStringLiteral("发布检查清单"));
        args.insert(QStringLiteral("max_results"), 5);
        const QString output = MemoryTool::executeSearch(args);
        const QStringList lines = extractHitLines(output);
        if (lines.size() < 2
            || !lines.at(0).contains(QStringLiteral("/memory/%1.md").arg(newerDailyName))
            || !lines.at(1).contains(QStringLiteral("/memory/%1.md").arg(olderDailyName))) {
            return failWithActual(output);
        }
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("测试结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    return g_passCount == g_testCount ? 0 : 1;
}
