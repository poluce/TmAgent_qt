#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>
#include <QUuid>

#include "core/memory/MemoryManager.h"
#include "core/persistence/ChatPersistenceService.h"

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

static QString makeTempHomeDir()
{
    const QString base = QDir::tempPath();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QDir(base).filePath(QStringLiteral("tmagent_test_home_%1").arg(id));
}

static bool writeUtf8Atomic(const QString& filePath, const QString& content, QString* error = nullptr)
{
    if (error)
        error->clear();
    const QString parent = QFileInfo(filePath).absolutePath();
    if (!QDir().mkpath(parent)) {
        if (error)
            *error = QStringLiteral("failed to create parent dir: %1").arg(parent);
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        if (error)
            *error = QStringLiteral("failed to open for write: %1").arg(filePath);
        return false;
    }
    if (file.write(content.toUtf8()) < 0) {
        if (error)
            *error = QStringLiteral("failed to write: %1").arg(filePath);
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("failed to commit: %1").arg(filePath);
        return false;
    }
    return true;
}

static bool writeJsonObjectAtomic(const QString& filePath, const QJsonObject& obj, QString* error = nullptr)
{
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    return writeUtf8Atomic(filePath, QString::fromUtf8(payload), error);
}

static QString readUtf8(const QString& filePath, bool* ok = nullptr)
{
    if (ok)
        *ok = false;
    QFile file(filePath);
    if (!file.exists()) {
        if (ok)
            *ok = true;
        return QString();
    }
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QString();
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    if (ok)
        *ok = true;
    return content;
}

static QJsonObject makePolicyRules(bool enabled, int maxCandidates, int scanDailyFiles)
{
    QJsonObject rules;
    rules.insert(QStringLiteral("reflect_enabled"), enabled);
    rules.insert(QStringLiteral("reflect_every_n_turns"), 1);
    rules.insert(QStringLiteral("reflect_max_candidates_per_run"), maxCandidates);
    rules.insert(QStringLiteral("reflect_scan_daily_files"), scanDailyFiles);
    return rules;
}

static QString dailyPathFor(const ChatPersistenceService& persistence, const QString& agentId, const QDate& date)
{
    const QString agentDir = QDir(persistence.agentsDirPath()).filePath(agentId.trimmed());
    const QString dailyDir = QDir(agentDir).filePath(QStringLiteral("memory"));
    return QDir(dailyDir).filePath(date.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".md"));
}

static QString memoryPathFor(const ChatPersistenceService& persistence, const QString& agentId)
{
    const QString agentDir = QDir(persistence.agentsDirPath()).filePath(agentId.trimmed());
    return QDir(agentDir).filePath(QStringLiteral("memory.md"));
}

static void removeAgentDir(const ChatPersistenceService& persistence, const QString& agentId)
{
    const QString agentDir = QDir(persistence.agentsDirPath()).filePath(agentId.trimmed());
    if (agentDir.trimmed().isEmpty())
        return;
    QDir(agentDir).removeRecursively();
}

int main(int argc, char* argv[])
{
    const QString tempHome = makeTempHomeDir();
    qputenv("HOME", tempHome.toUtf8());
    qputenv("USERPROFILE", tempHome.toUtf8());

    QCoreApplication app(argc, argv);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "   Memory Reflection (M4) 测试套件";
    qDebug().noquote() << "════════════════════════════════════════";

    ChatPersistenceService persistence;
    MemoryManager memoryManager(&persistence);

    const QString expectedRoot = QDir(tempHome).filePath(QStringLiteral(".tmagent"));
    const bool isolated = QDir::cleanPath(persistence.dataRootPath()) == QDir::cleanPath(expectedRoot);
    if (!isolated) {
        qDebug().noquote() << "警告: 未能隔离 HOME，测试将自动做最小影响恢复。";
        qDebug().noquote() << "dataRootPath: " << persistence.dataRootPath();
    }

    const QString policyPath = persistence.memoryPolicyPath();
    bool hadOriginalPolicy = false;
    bool policyReadOk = false;
    const QString originalPolicy = readUtf8(policyPath, &policyReadOk);
    if (policyReadOk && QFileInfo::exists(policyPath))
        hadOriginalPolicy = true;

    QStringList createdAgents;

    auto restore = [&]() {
        for (const QString& agentId : createdAgents)
            removeAgentDir(persistence, agentId);

        if (isolated) {
            QDir(expectedRoot).removeRecursively();
            return;
        }

        if (hadOriginalPolicy) {
            QString err;
            if (!writeUtf8Atomic(policyPath, originalPolicy, &err))
                qDebug().noquote() << "恢复 memory_policy.json 失败:" << err;
        } else {
            QFile::remove(policyPath);
        }
    };

    // Test 1: enabled -> writes reflected entries into memory.md
    TEST("reflect_enabled = true 产生长期记忆") {
        const QString agentId = QStringLiteral("agent_reflect_enabled_%1").arg(g_testCount);
        createdAgents.append(agentId);

        QJsonObject policy;
        policy.insert(QStringLiteral("memory_rules"), makePolicyRules(true, 2, 1));
        if (!writeJsonObjectAtomic(policyPath, policy))
            return 1;

        const QString dailyPath = dailyPathFor(persistence, agentId, QDate::currentDate());
        QString writeErr;
        const QString dailyContent = QStringLiteral(
            "## daily\n"
            "- user: 我偏好使用中文输出\n"
            "- assistant: 好的\n"
            "- user: 我叫小明\n");
        if (!writeUtf8Atomic(dailyPath, dailyContent, &writeErr)) {
            qDebug().noquote() << "写入 daily 失败:" << writeErr;
            return 1;
        }

        QString summary;
        QString writtenPath;
        QJsonObject metadata;
        QString error;
        const bool ok = memoryManager.reflectAndScore(agentId, QStringLiteral("s"), QStringLiteral("t"), QStringLiteral("tr"), &summary, &writtenPath, &metadata, &error);
        if (!ok) {
            qDebug().noquote() << "reflectAndScore failed:" << error;
            return 1;
        }
        if (!error.trimmed().isEmpty()) {
            qDebug().noquote() << "unexpected error:" << error;
            return 1;
        }
        if (!metadata.value(QStringLiteral("reflection_enabled")).toBool())
            return 1;
        if (metadata.value(QStringLiteral("longMemoryAdded")).toInt() <= 0)
            return 1;

        const QString memoryPath = memoryPathFor(persistence, agentId);
        bool memOk = false;
        const QString memoryContent = readUtf8(memoryPath, &memOk);
        if (!memOk || memoryContent.trimmed().isEmpty())
            return 1;
        if (!memoryContent.contains(QStringLiteral("reflected: true")))
            return 1;
        if (!memoryContent.contains(QStringLiteral("反思提炼：我偏好使用中文输出"))
            && !memoryContent.contains(QStringLiteral("反思提炼：我叫小明"))) {
            return 1;
        }
        return 0;
    }
    END_TEST

    // Test 2: disabled -> no memory.md written, metadata marks disabled
    TEST("reflect_enabled = false 跳过反思") {
        const QString agentId = QStringLiteral("agent_reflect_disabled_%1").arg(g_testCount);
        createdAgents.append(agentId);

        QJsonObject policy;
        policy.insert(QStringLiteral("memory_rules"), makePolicyRules(false, 2, 1));
        if (!writeJsonObjectAtomic(policyPath, policy))
            return 1;

        const QString dailyPath = dailyPathFor(persistence, agentId, QDate::currentDate());
        QString writeErr;
        const QString dailyContent = QStringLiteral(
            "## daily\n"
            "- user: 我偏好使用中文输出\n");
        if (!writeUtf8Atomic(dailyPath, dailyContent, &writeErr)) {
            qDebug().noquote() << "写入 daily 失败:" << writeErr;
            return 1;
        }

        QString summary;
        QString writtenPath;
        QJsonObject metadata;
        QString error;
        const bool ok = memoryManager.reflectAndScore(agentId, QStringLiteral("s"), QStringLiteral("t"), QStringLiteral("tr"), &summary, &writtenPath, &metadata, &error);
        if (!ok) {
            qDebug().noquote() << "reflectAndScore failed:" << error;
            return 1;
        }
        if (metadata.value(QStringLiteral("reflection_skipped")).toBool() != true)
            return 1;
        if (metadata.value(QStringLiteral("quality_level")).toString() != QStringLiteral("disabled"))
            return 1;

        const QString memoryPath = memoryPathFor(persistence, agentId);
        if (QFileInfo::exists(memoryPath))
            return 1;
        if (summary.trimmed() != QStringLiteral("反思任务已禁用"))
            return 1;
        return 0;
    }
    END_TEST

    // Test 3: max candidates limit is respected
    TEST("reflect_max_candidates_per_run 生效") {
        const QString agentId = QStringLiteral("agent_reflect_limit_%1").arg(g_testCount);
        createdAgents.append(agentId);

        QJsonObject policy;
        policy.insert(QStringLiteral("memory_rules"), makePolicyRules(true, 1, 1));
        if (!writeJsonObjectAtomic(policyPath, policy))
            return 1;

        const QString dailyPath = dailyPathFor(persistence, agentId, QDate::currentDate());
        QString writeErr;
        const QString dailyContent = QStringLiteral(
            "## daily\n"
            "- user: 我偏好使用中文输出\n"
            "- user: 我喜欢简洁\n"
            "- user: 我习惯先给结论\n");
        if (!writeUtf8Atomic(dailyPath, dailyContent, &writeErr))
            return 1;

        QJsonObject metadata;
        QString error;
        const bool ok = memoryManager.reflectAndScore(agentId, QStringLiteral("s"), QStringLiteral("t"), QStringLiteral("tr"), nullptr, nullptr, &metadata, &error);
        if (!ok) {
            qDebug().noquote() << "reflectAndScore failed:" << error;
            return 1;
        }

        if (metadata.value(QStringLiteral("stable_candidates")).toInt() != 1)
            return 1;
        if (metadata.value(QStringLiteral("max_candidates_per_run")).toInt() != 1)
            return 1;
        if (metadata.value(QStringLiteral("longMemoryAdded")).toInt() != 1)
            return 1;
        return 0;
    }
    END_TEST

    // Test 4: scan daily files limit works (only latest is considered)
    TEST("reflect_scan_daily_files 生效") {
        const QString agentId = QStringLiteral("agent_reflect_scan_%1").arg(g_testCount);
        createdAgents.append(agentId);

        QJsonObject policy;
        policy.insert(QStringLiteral("memory_rules"), makePolicyRules(true, 4, 1));
        if (!writeJsonObjectAtomic(policyPath, policy))
            return 1;

        const QDate today = QDate::currentDate();
        const QDate yesterday = today.addDays(-1);

        QString writeErr;
        if (!writeUtf8Atomic(dailyPathFor(persistence, agentId, yesterday), QStringLiteral("- user: 我偏好昨天\n"), &writeErr))
            return 1;
        if (!writeUtf8Atomic(dailyPathFor(persistence, agentId, today), QStringLiteral("- user: 我偏好今天\n"), &writeErr))
            return 1;

        QJsonObject metadata;
        QString error;
        const bool ok = memoryManager.reflectAndScore(agentId, QStringLiteral("s"), QStringLiteral("t"), QStringLiteral("tr"), nullptr, nullptr, &metadata, &error);
        if (!ok) {
            qDebug().noquote() << "reflectAndScore failed:" << error;
            return 1;
        }
        const QString memoryPath = memoryPathFor(persistence, agentId);
        bool memOk = false;
        const QString memoryContent = readUtf8(memoryPath, &memOk);
        if (!memOk)
            return 1;
        if (!memoryContent.contains(QStringLiteral("反思提炼：我偏好今天")))
            return 1;
        if (memoryContent.contains(QStringLiteral("反思提炼：我偏好昨天")))
            return 1;
        return 0;
    }
    END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);

    restore();
    return (g_passCount == g_testCount) ? 0 : 1;
}
