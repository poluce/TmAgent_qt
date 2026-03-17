#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "HeartbeatPromptBuilder.h"

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

namespace {

int fail(const QString& expected, const QString& actual)
{
    qDebug().noquote() << "  [期望]" << expected;
    qDebug().noquote() << "  [实际]" << actual;
    return 1;
}

bool writeText(const QString& path, const QString& content)
{
    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Text))
        return false;
    file.write(content.toUtf8());
    file.close();
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "  HeartbeatPromptBuilder 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("无指令文件时使用默认模板并回填 interval") {
        HeartbeatPromptBuilder builder(
            HeartbeatPromptBuilder::Dependencies {
                [](const QString&) { return QString(); }
            });
        const QString prompt = builder.build(QStringLiteral("agent-1"), QString());
        if (!prompt.startsWith(QStringLiteral("【系统心跳任务】reason=interval")))
            return fail(QStringLiteral("reason=interval"), prompt);
        if (!prompt.contains(QStringLiteral("轻量心跳巡检")))
            return fail(QStringLiteral("包含默认巡检文案"), prompt);
        return 0;
    } END_TEST

    TEST("存在 legacy 指令文件时会修正旧文案并输出自定义内容") {
        QTemporaryDir dir;
        if (!dir.isValid())
            return fail(QStringLiteral("有效临时目录"), QStringLiteral("<invalid>"));
        const QString path = QDir(dir.path()).filePath(QStringLiteral("heartbeat.md"));
        const QString legacy = QStringLiteral(
            "请巡检\n"
            "3. 若无关键变化，返回“当前无关键更新”。\n");
        if (!writeText(path, legacy))
            return fail(QStringLiteral("成功写入测试文件"), path);

        HeartbeatPromptBuilder builder(
            HeartbeatPromptBuilder::Dependencies {
                [path](const QString&) { return path; }
            });
        const QString prompt = builder.build(QStringLiteral("agent-1"), QStringLiteral("requested"));
        if (!prompt.startsWith(QStringLiteral("【系统心跳任务】reason=requested")))
            return fail(QStringLiteral("reason=requested"), prompt);
        if (!prompt.contains(QStringLiteral("默认静默")))
            return fail(QStringLiteral("旧文案被修正"), prompt);

        QFile file(path);
        if (!file.open(QFile::ReadOnly | QFile::Text))
            return fail(QStringLiteral("修正后文件可读"), path);
        const QString repaired = QString::fromUtf8(file.readAll());
        file.close();
        if (!repaired.contains(QStringLiteral("默认静默")))
            return fail(QStringLiteral("文件内容已修正"), repaired);
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果：%1/%2 通过").arg(g_passCount).arg(g_testCount);
    PRINT_DIVIDER();
    return g_passCount == g_testCount ? 0 : 1;
}

