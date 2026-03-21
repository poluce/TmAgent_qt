#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "core/logging/LogCatalog.h"
#include "core/logging/LogFollower.h"
#include "core/logging/LogHealthCheck.h"
#include "core/logging/LogQueryEngine.h"
#include "core/observability/AlertManager.h"
#include "core/observability/MetricsCollector.h"
#include "core/tools/EventLogTool.h"

static int g_testCount = 0;
static int g_passCount = 0;

#define TEST(name) \
    ++g_testCount; \
    qDebug().noquote() << QStringLiteral("[测试 %1] %2").arg(g_testCount).arg(name); \
    if (int _test_result = [&]() -> int

#define END_TEST \
    (); _test_result != 0) { \
        qDebug().noquote() << QStringLiteral("  ❌ 失败"); \
    } else { \
        ++g_passCount; \
        qDebug().noquote() << QStringLiteral("  ✅ 通过"); \
    }

static int fail(const QString& message)
{
    qDebug().noquote() << QStringLiteral("  [错误]") << message;
    return 1;
}

static bool execSql(QSqlDatabase& db, const QString& sql, QString* error = nullptr)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    return true;
}

static bool prepareAndExec(QSqlDatabase& db,
                           const QString& sql,
                           const QList<QVariant>& binds,
                           QString* error = nullptr)
{
    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    for (const QVariant& bind : binds)
        query.addBindValue(bind);
    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return false;
    }
    return true;
}

static bool setupDatabase(const QString& rootPath, QString* error = nullptr)
{
    QDir(rootPath).mkpath(QStringLiteral("."));

    const QString dbPath = QDir(rootPath).filePath(QStringLiteral("tmagent.db"));
    const QString connName = QStringLiteral("logging_toolchain_%1")
                                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            if (error)
                *error = db.lastError().text();
            return false;
        }

        const QStringList ddl = {
            QStringLiteral(
                "CREATE TABLE events ("
                "id INTEGER PRIMARY KEY, "
                "timestamp TEXT, "
                "timestamp_ms INTEGER, "
                "session_id TEXT, "
                "trace_id TEXT, "
                "turn_id TEXT, "
                "run_id TEXT, "
                "request_id TEXT, "
                "tool_call_id TEXT, "
                "actor_id TEXT, "
                "tool_name TEXT, "
                "event_type TEXT, "
                "level TEXT, "
                "duration_ms INTEGER, "
                "success INTEGER, "
                "summary TEXT, "
                "raw_json TEXT)"),
            QStringLiteral(
                "CREATE TABLE messages ("
                "id TEXT, "
                "session_id TEXT, "
                "trace_id TEXT, "
                "turn_id TEXT, "
                "sender_id TEXT, "
                "content_type TEXT, "
                "content_text TEXT, "
                "content_payload TEXT, "
                "timestamp TEXT, "
                "status TEXT, "
                "source TEXT)"),
            QStringLiteral(
                "CREATE TABLE sessions ("
                "id TEXT PRIMARY KEY, "
                "owner_id TEXT, "
                "title TEXT, "
                "created_at TEXT, "
                "last_active_at TEXT, "
                "type TEXT)"),
            QStringLiteral(
                "CREATE TABLE session_participants ("
                "session_id TEXT, "
                "identity_id TEXT)"),
            QStringLiteral(
                "CREATE TABLE identities ("
                "id TEXT PRIMARY KEY, "
                "type TEXT, "
                "name TEXT, "
                "avatar TEXT, "
                "profile TEXT)")
        };

        for (const QString& sql : ddl) {
            if (!execSql(db, sql, error))
                return false;
        }

        const QString timestamp = QStringLiteral("2026-03-21T10:11:12.345Z");
        const qint64 timestampMs = 1774087872345LL;

        const QJsonObject eventRaw {
            {QStringLiteral("timestamp"), timestamp},
            {QStringLiteral("session_id"), QStringLiteral("session-1")},
            {QStringLiteral("trace_id"), QStringLiteral("trace-1")},
            {QStringLiteral("turn_id"), QStringLiteral("turn-1")},
            {QStringLiteral("run_id"), QStringLiteral("run-1")},
            {QStringLiteral("request_id"), QStringLiteral("req-1")},
            {QStringLiteral("tool_call_id"), QStringLiteral("call-1")},
            {QStringLiteral("actor_id"), QStringLiteral("agent-1")},
            {QStringLiteral("tool_name"), QStringLiteral("shell")},
            {QStringLiteral("type"), QStringLiteral("tool_failed")},
            {QStringLiteral("level"), QStringLiteral("error")},
            {QStringLiteral("duration_ms"), 123},
            {QStringLiteral("success"), false},
            {QStringLiteral("summary"), QStringLiteral("shell failed")}
        };

        const QJsonObject profile {
            {QStringLiteral("configId"), QStringLiteral("cfg-1")},
            {QStringLiteral("providerInstanceId"), QStringLiteral("provider-1")},
            {QStringLiteral("selectedModelId"), QStringLiteral("gpt-test")},
            {QStringLiteral("description"), QStringLiteral("assistant")},
            {QStringLiteral("systemPrompt"), QStringLiteral("help the user")},
            {QStringLiteral("recursionDepth"), 4},
            {QStringLiteral("delegateEnabled"), true},
            {QStringLiteral("allowedTools"), QJsonArray{QStringLiteral("shell"), QStringLiteral("event_log")}}
        };

        if (!prepareAndExec(
                db,
                QStringLiteral(
                    "INSERT INTO events "
                    "(id, timestamp, timestamp_ms, session_id, trace_id, turn_id, run_id, request_id, "
                    "tool_call_id, actor_id, tool_name, event_type, level, duration_ms, success, summary, raw_json) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                {1, timestamp, timestampMs, QStringLiteral("session-1"), QStringLiteral("trace-1"),
                 QStringLiteral("turn-1"), QStringLiteral("run-1"), QStringLiteral("req-1"),
                 QStringLiteral("call-1"), QStringLiteral("agent-1"), QStringLiteral("shell"),
                 QStringLiteral("tool_failed"), QStringLiteral("error"), 123, 0,
                 QStringLiteral("shell failed"),
                 QString::fromUtf8(QJsonDocument(eventRaw).toJson(QJsonDocument::Compact))},
                error)
            || !prepareAndExec(
                db,
                QStringLiteral(
                    "INSERT INTO messages "
                    "(id, session_id, trace_id, turn_id, sender_id, content_type, content_text, "
                    "content_payload, timestamp, status, source) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                {QStringLiteral("msg-1"), QStringLiteral("session-1"), QStringLiteral("trace-1"),
                 QStringLiteral("turn-1"), QStringLiteral("agent-1"), QStringLiteral("text"),
                 QStringLiteral("hello world"),
                 QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("run_id"), QStringLiteral("run-1")}})
                                       .toJson(QJsonDocument::Compact)),
                 timestamp, QStringLiteral("done"), QStringLiteral("runtime")},
                error)
            || !prepareAndExec(
                db,
                QStringLiteral(
                    "INSERT INTO sessions (id, owner_id, title, created_at, last_active_at, type) "
                    "VALUES (?, ?, ?, ?, ?, ?)"),
                {QStringLiteral("session-1"), QStringLiteral("agent-1"), QStringLiteral("Alpha Session"),
                 timestamp, timestamp, QStringLiteral("private")},
                error)
            || !prepareAndExec(
                db,
                QStringLiteral("INSERT INTO session_participants (session_id, identity_id) VALUES (?, ?)"),
                {QStringLiteral("session-1"), QStringLiteral("agent-1")},
                error)
            || !prepareAndExec(
                db,
                QStringLiteral(
                    "INSERT INTO identities (id, type, name, avatar, profile) VALUES (?, ?, ?, ?, ?)"),
                {QStringLiteral("agent-1"), QStringLiteral("agent"), QStringLiteral("Alpha Agent"),
                 QString(), QString::fromUtf8(QJsonDocument(profile).toJson(QJsonDocument::Compact))},
                error)) {
            return false;
        }

        db.close();
    }
    QSqlDatabase::removeDatabase(connName);
    return true;
}

static bool insertEventRow(const QString& rootPath,
                           qint64 id,
                           const QString& summary,
                           QString* error = nullptr)
{
    const QString connName = QStringLiteral("logging_insert_%1")
                                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(QDir(rootPath).filePath(QStringLiteral("tmagent.db")));
        if (!db.open()) {
            if (error)
                *error = db.lastError().text();
            return false;
        }

        const QString timestamp = QStringLiteral("2026-03-21T10:11:13.000Z");
        const qint64 timestampMs = 1774087873000LL + id;
        const QJsonObject eventRaw {
            {QStringLiteral("timestamp"), timestamp},
            {QStringLiteral("session_id"), QStringLiteral("session-1")},
            {QStringLiteral("trace_id"), QStringLiteral("trace-1")},
            {QStringLiteral("turn_id"), QStringLiteral("turn-1")},
            {QStringLiteral("run_id"), QStringLiteral("run-1")},
            {QStringLiteral("request_id"), QStringLiteral("req-2")},
            {QStringLiteral("tool_call_id"), QStringLiteral("call-2")},
            {QStringLiteral("actor_id"), QStringLiteral("agent-1")},
            {QStringLiteral("tool_name"), QStringLiteral("shell")},
            {QStringLiteral("type"), QStringLiteral("tool_finished")},
            {QStringLiteral("level"), QStringLiteral("info")},
            {QStringLiteral("duration_ms"), 77},
            {QStringLiteral("success"), true},
            {QStringLiteral("summary"), summary}
        };

        ok = prepareAndExec(
            db,
            QStringLiteral(
                "INSERT INTO events "
                "(id, timestamp, timestamp_ms, session_id, trace_id, turn_id, run_id, request_id, "
                "tool_call_id, actor_id, tool_name, event_type, level, duration_ms, success, summary, raw_json) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
            {id, timestamp, timestampMs, QStringLiteral("session-1"), QStringLiteral("trace-1"),
             QStringLiteral("turn-1"), QStringLiteral("run-1"), QStringLiteral("req-2"),
             QStringLiteral("call-2"), QStringLiteral("agent-1"), QStringLiteral("shell"),
             QStringLiteral("tool_finished"), QStringLiteral("info"), 77, 1, summary,
             QString::fromUtf8(QJsonDocument(eventRaw).toJson(QJsonDocument::Compact))},
            error);

        db.close();
    }
    QSqlDatabase::removeDatabase(connName);
    return ok;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tempRoot;
    if (!tempRoot.isValid()) {
        qDebug().noquote() << QStringLiteral("无法创建临时目录");
        return 1;
    }

    QString error;
    if (!setupDatabase(tempRoot.path(), &error)) {
        qDebug().noquote() << QStringLiteral("初始化日志数据库失败:") << error;
        return 1;
    }

    TEST("LogQueryEngine - events 查询与格式化") {
        LogQueryEngine::Query query;
        query.dataRootPath = tempRoot.path();
        query.source = QStringLiteral("events");
        query.sessionId = QStringLiteral("session-1");
        query.level = QStringLiteral("error");
        query.format = LogQueryEngine::OutputFormat::Table;
        const LogQueryEngine::Result result = LogQueryEngine::execute(query);
        if (result.hits.size() != 1)
            return fail(QStringLiteral("events 查询结果数不为 1"));
        if (result.hits.first().toolName != QStringLiteral("shell")
            || result.hits.first().eventType != QStringLiteral("tool_failed")) {
            return fail(QStringLiteral("events 命中内容不符合预期"));
        }
        const QString formatted = LogQueryEngine::formatResult(result);
        if (!formatted.contains(QStringLiteral("tool_failed"))
            || !formatted.contains(QStringLiteral("shell"))) {
            return fail(QStringLiteral("table 输出缺少关键字段"));
        }
        return 0;
    } END_TEST

    TEST("LogQueryEngine - messages 与 raw 输出") {
        LogQueryEngine::Query query;
        query.dataRootPath = tempRoot.path();
        query.source = QStringLiteral("messages");
        query.actorId = QStringLiteral("agent-1");
        query.keyword = QStringLiteral("hello");
        query.includeRaw = true;
        query.format = LogQueryEngine::OutputFormat::Raw;
        const LogQueryEngine::Result result = LogQueryEngine::execute(query);
        if (result.hits.size() != 1)
            return fail(QStringLiteral("messages 查询结果数不为 1"));
        const QString raw = LogQueryEngine::formatResult(result);
        if (!raw.contains(QStringLiteral("\"senderId\":\"agent-1\""))
            || !raw.contains(QStringLiteral("\"text\":\"hello world\""))) {
            return fail(QStringLiteral("raw 输出缺少消息原文"));
        }
        return 0;
    } END_TEST

    TEST("EventLogTool - search/sessions/agents JSON 输出") {
        const QString searchRaw = EventLogTool::execute(QJsonObject{
            {QStringLiteral("action"), QStringLiteral("search")},
            {QStringLiteral("data_root"), tempRoot.path()},
            {QStringLiteral("source"), QStringLiteral("events")},
            {QStringLiteral("session_id"), QStringLiteral("session-1")},
            {QStringLiteral("format"), QStringLiteral("json")}
        });
        const QJsonDocument searchDoc = QJsonDocument::fromJson(searchRaw.toUtf8());
        if (!searchDoc.isObject()
            || searchDoc.object().value(QStringLiteral("status")).toString() != QStringLiteral("successful")
            || searchDoc.object().value(QStringLiteral("data")).toObject().value(QStringLiteral("count")).toInt() != 1) {
            return fail(QStringLiteral("search 输出不符合预期"));
        }

        const QString sessionsRaw = EventLogTool::execute(QJsonObject{
            {QStringLiteral("action"), QStringLiteral("sessions")},
            {QStringLiteral("data_root"), tempRoot.path()}
        });
        const QJsonDocument sessionsDoc = QJsonDocument::fromJson(sessionsRaw.toUtf8());
        if (!sessionsDoc.isObject()
            || sessionsDoc.object().value(QStringLiteral("data")).toObject().value(QStringLiteral("count")).toInt() != 1) {
            return fail(QStringLiteral("sessions 输出不符合预期"));
        }

        const QString agentsRaw = EventLogTool::execute(QJsonObject{
            {QStringLiteral("action"), QStringLiteral("agents")},
            {QStringLiteral("data_root"), tempRoot.path()}
        });
        const QJsonDocument agentsDoc = QJsonDocument::fromJson(agentsRaw.toUtf8());
        if (!agentsDoc.isObject()
            || agentsDoc.object().value(QStringLiteral("data")).toObject().value(QStringLiteral("count")).toInt() != 1) {
            return fail(QStringLiteral("agents 输出不符合预期"));
        }

        return 0;
    } END_TEST

    TEST("LogCatalog - sessions 与 agents 列举") {
        const LogCatalog::SessionListResult sessions = LogCatalog::listSessions(tempRoot.path());
        const LogCatalog::AgentListResult agents = LogCatalog::listAgents(
            LogCatalog::AgentQueryOptions{tempRoot.path(), QString(), QString(), false});
        if (sessions.sessions.size() != 1 || agents.agents.size() != 1)
            return fail(QStringLiteral("LogCatalog 列举结果数不符合预期"));
        if (agents.agents.first().sessions.size() != 1
            || agents.agents.first().selectedModelId != QStringLiteral("gpt-test")) {
            return fail(QStringLiteral("LogCatalog Agent 详情不符合预期"));
        }
        return 0;
    } END_TEST

    TEST("LogHealthCheck / AlertManager / MetricsCollector 冒烟") {
        const LogHealthCheck::HealthStatus health = LogHealthCheck::check(tempRoot.path());
        if (health.eventBackend != QStringLiteral("sqlite"))
            return fail(QStringLiteral("LogHealthCheck 未识别 sqlite backend"));

        AlertManager* alerts = AlertManager::instance();
        AlertManager::AlertRule rule;
        rule.name = QStringLiteral("tool-fail");
        rule.toolName = QStringLiteral("shell");
        rule.eventType = QStringLiteral("tool_failed");
        rule.failureThreshold = 1;
        rule.windowSeconds = 60;
        alerts->addRule(rule);
        alerts->processEvent(QJsonObject{
            {QStringLiteral("tool_name"), QStringLiteral("shell")},
            {QStringLiteral("event_type"), QStringLiteral("tool_failed")},
            {QStringLiteral("success"), false}
        });
        const QVector<AlertManager::Alert> recent = alerts->recentAlerts(1);
        alerts->removeRule(rule.name);
        if (recent.isEmpty() || recent.last().ruleName != rule.name)
            return fail(QStringLiteral("AlertManager 未触发预期告警"));

        MetricsCollector* metrics = MetricsCollector::instance();
        metrics->reset();
        metrics->recordToolCall(QStringLiteral("shell"), false, 123);
        const MetricsCollector::ToolMetrics shellMetrics = metrics->toolMetrics(QStringLiteral("shell"));
        if (shellMetrics.totalCalls != 1 || shellMetrics.failureCount != 1 || shellMetrics.avgDurationMs <= 0.0)
            return fail(QStringLiteral("MetricsCollector 指标不符合预期"));
        return 0;
    } END_TEST

    TEST("LogFollower - smoke") {
        LogQueryEngine::Query filter;
        filter.dataRootPath = tempRoot.path();
        filter.source = QStringLiteral("events");
        filter.sessionId = QStringLiteral("session-1");

        LogFollower follower(filter, tempRoot.path());
        follower.start();

        if (!insertEventRow(tempRoot.path(), 2, QStringLiteral("shell recovered"), &error))
            return fail(QStringLiteral("插入 follow 事件失败: %1").arg(error));
        if (!QMetaObject::invokeMethod(&follower, "pollNewEvents", Qt::DirectConnection))
            return fail(QStringLiteral("无法触发 LogFollower::pollNewEvents"));
        return 0;
    } END_TEST

    qDebug().noquote() << QStringLiteral("测试结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    return g_passCount == g_testCount ? 0 : 1;
}
