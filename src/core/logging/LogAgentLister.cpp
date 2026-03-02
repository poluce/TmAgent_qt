#include "LogAgentLister.h"

#include "LogDbUtils.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace LogAgentLister {

namespace {

QString humanReadableSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString padRight(const QString& text, int width)
{
    if (text.size() >= width)
        return text;
    return text + QString(width - text.size(), QLatin1Char(' '));
}

QString clipId(const QString& id, int maxLen)
{
    if (id.size() <= maxLen)
        return id;
    return id.left(maxLen);
}

QDateTime parseTimestamp(const QString& str)
{
    if (str.isEmpty())
        return QDateTime();
    QDateTime dt = QDateTime::fromString(str, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(str, Qt::ISODate);
    return dt;
}

QStringList jsonArrayToStringList(const QJsonArray& arr)
{
    QStringList list;
    for (const QJsonValue& v : arr)
        list.append(v.toString());
    return list;
}

QVector<SessionSummary> loadSessionsForAgent(QSqlDatabase& db, const QString& agentId)
{
    QVector<SessionSummary> sessions;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT "
        "  s.id, "
        "  s.title, "
        "  s.type, "
        "  COALESCE(m.message_count, 0), "
        "  COALESCE(NULLIF(s.last_active_at, ''), NULLIF(m.last_message_ts, ''), '') "
        "FROM sessions s "
        "INNER JOIN session_participants sp ON sp.session_id = s.id "
        "LEFT JOIN ("
        "  SELECT session_id, COUNT(*) AS message_count, MAX(timestamp) AS last_message_ts "
        "  FROM messages "
        "  GROUP BY session_id"
        ") m ON m.session_id = s.id "
        "WHERE sp.identity_id = ? "
        "ORDER BY COALESCE(NULLIF(s.last_active_at, ''), NULLIF(m.last_message_ts, ''), NULLIF(s.created_at, '')) DESC"));
    q.addBindValue(agentId);

    if (!q.exec())
        return sessions;

    while (q.next()) {
        SessionSummary ss;
        ss.sessionId = q.value(0).toString();
        ss.title = q.value(1).toString();
        ss.type = q.value(2).toString();
        ss.messageCount = q.value(3).toLongLong();
        ss.lastActiveAt = parseTimestamp(q.value(4).toString());
        sessions.append(ss);
    }

    return sessions;
}

} // anonymous namespace

ListResult listAgents(const QueryOptions& options)
{
    ListResult result;

    QString dbError;
    QSqlDatabase db = LogDbUtils::openConnection(options.dataRootPath, &dbError);
    if (!db.isValid() || !db.isOpen()) {
        result.warnings.append(
            QStringLiteral("SQLite 连接不可用，无法查询 Agent 信息: %1").arg(dbError));
        return result;
    }

    QString sql = QStringLiteral(
        "SELECT id, type, name, avatar, profile "
        "FROM identities "
        "WHERE LOWER(type) = 'agent'");

    if (!options.filterAgentId.trimmed().isEmpty())
        sql += QStringLiteral(" AND id = ?");
    if (!options.filterAgentName.trimmed().isEmpty())
        sql += QStringLiteral(" AND LOWER(name) LIKE LOWER(?)");
    sql += QStringLiteral(" ORDER BY LOWER(name), id");

    QSqlQuery q(db);
    q.prepare(sql);
    if (!options.filterAgentId.trimmed().isEmpty())
        q.addBindValue(options.filterAgentId.trimmed());
    if (!options.filterAgentName.trimmed().isEmpty())
        q.addBindValue(QStringLiteral("%") + options.filterAgentName.trimmed() + QStringLiteral("%"));

    if (!q.exec()) {
        result.warnings.append(
            QStringLiteral("Agent 查询失败: %1").arg(q.lastError().text()));
        return result;
    }

    while (q.next()) {
        AgentInfo info;
        info.agentId = q.value(0).toString();
        info.name = q.value(2).toString();
        info.avatar = q.value(3).toString();

        const QString profileJson = q.value(4).toString();
        if (!profileJson.trimmed().isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument profileDoc = QJsonDocument::fromJson(profileJson.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && profileDoc.isObject()) {
                const QJsonObject profile = profileDoc.object();
                info.configId = profile.value(QStringLiteral("configId")).toString();
                info.providerInstanceId = profile.value(QStringLiteral("providerInstanceId")).toString();
                info.selectedModelId = profile.value(QStringLiteral("selectedModelId")).toString();
                info.description = profile.value(QStringLiteral("description")).toString();
                info.systemPrompt = profile.value(QStringLiteral("systemPrompt")).toString();
                info.recursionDepth = profile.value(QStringLiteral("recursionDepth")).toInt(3);
                info.delegateEnabled = profile.value(QStringLiteral("delegateEnabled")).toBool(true);
                info.allowedTools = jsonArrayToStringList(profile.value(QStringLiteral("allowedTools")).toArray());
            }
        }

        info.sessions = loadSessionsForAgent(db, info.agentId);
        result.agents.append(info);
    }

    return result;
}

QString formatTable(const ListResult& result)
{
    QStringList lines;

    if (!result.warnings.isEmpty()) {
        for (const QString& w : result.warnings)
            lines << QStringLiteral("WARNING: %1").arg(w);
        lines << QString();
    }

    if (result.agents.isEmpty()) {
        lines << QStringLiteral("(未找到 Agent)");
        return lines.join(QLatin1Char('\n'));
    }

    lines << QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
                 .arg(padRight(QStringLiteral("ID"), 10),
                      padRight(QStringLiteral("NAME"), 26),
                      padRight(QStringLiteral("MODEL"), 26),
                      padRight(QStringLiteral("ROLE"), 14),
                      padRight(QStringLiteral("DELEGATE"), 10),
                      padRight(QStringLiteral("TOOLS"), 6),
                      QStringLiteral("SESSIONS"));

    for (const AgentInfo& a : result.agents) {
        const QString model = a.selectedModelId.trimmed();
        lines << QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
                     .arg(padRight(clipId(a.agentId, 8), 10),
                          padRight(a.name.isEmpty() ? QStringLiteral("-") : clipId(a.name, 24), 26),
                          padRight(model.isEmpty() ? QStringLiteral("-") : clipId(model, 24), 26),
                          padRight(a.description.isEmpty() ? QStringLiteral("-") : clipId(a.description, 12), 14),
                          padRight(a.delegateEnabled ? QStringLiteral("yes") : QStringLiteral("no"), 10),
                          padRight(QString::number(a.allowedTools.size()), 6),
                          QString::number(a.sessions.size()));
    }

    lines << QString();
    lines << QStringLiteral("共 %1 个 Agent").arg(result.agents.size());
    return lines.join(QLatin1Char('\n'));
}

QString formatJson(const ListResult& result)
{
    QJsonObject root;

    QJsonArray warnings;
    for (const QString& w : result.warnings)
        warnings.append(w);
    root.insert(QStringLiteral("warnings"), warnings);

    QJsonArray agents;
    for (const AgentInfo& a : result.agents) {
        QJsonObject obj;
        obj.insert(QStringLiteral("agent_id"), a.agentId);
        obj.insert(QStringLiteral("name"), a.name);
        obj.insert(QStringLiteral("avatar"), a.avatar);

        QJsonObject model;
        model.insert(QStringLiteral("config_id"), a.configId);
        model.insert(QStringLiteral("provider_instance_id"), a.providerInstanceId);
        model.insert(QStringLiteral("selected_model_id"), a.selectedModelId);
        obj.insert(QStringLiteral("model"), model);

        QJsonObject profileObj;
        profileObj.insert(QStringLiteral("description"), a.description);
        profileObj.insert(QStringLiteral("system_prompt"), a.systemPrompt);
        profileObj.insert(QStringLiteral("recursion_depth"), a.recursionDepth);
        profileObj.insert(QStringLiteral("delegate_enabled"), a.delegateEnabled);
        QJsonArray tools;
        for (const QString& t : a.allowedTools)
            tools.append(t);
        profileObj.insert(QStringLiteral("allowed_tools"), tools);
        obj.insert(QStringLiteral("profile"), profileObj);

        QJsonArray sessArr;
        for (const SessionSummary& s : a.sessions) {
            QJsonObject so;
            so.insert(QStringLiteral("session_id"), s.sessionId);
            so.insert(QStringLiteral("title"), s.title);
            so.insert(QStringLiteral("type"), s.type);
            so.insert(QStringLiteral("message_count"), s.messageCount);
            if (s.lastActiveAt.isValid())
                so.insert(QStringLiteral("last_active_at"), s.lastActiveAt.toUTC().toString(Qt::ISODateWithMs));
            sessArr.append(so);
        }
        obj.insert(QStringLiteral("sessions"), sessArr);

        obj.insert(QStringLiteral("disk_size_bytes"), a.totalDiskBytes);
        if (a.profileLastModified.isValid())
            obj.insert(QStringLiteral("profile_last_modified"),
                       a.profileLastModified.toUTC().toString(Qt::ISODateWithMs));

        agents.append(obj);
    }

    root.insert(QStringLiteral("agents"), agents);
    root.insert(QStringLiteral("count"), result.agents.size());

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString formatReport(const ListResult& result)
{
    QStringList lines;

    if (!result.warnings.isEmpty()) {
        for (const QString& w : result.warnings)
            lines << QStringLiteral("WARNING: %1").arg(w);
        lines << QString();
    }

    if (result.agents.isEmpty()) {
        lines << QStringLiteral("(未找到 Agent)");
        return lines.join(QLatin1Char('\n'));
    }

    for (int i = 0; i < result.agents.size(); ++i) {
        const AgentInfo& a = result.agents[i];

        if (i > 0)
            lines << QString() << QStringLiteral("════════════════════════════════════════════════════════════");

        lines << QStringLiteral("=== Agent 详情 ===");
        lines << QStringLiteral("ID:       %1").arg(a.agentId);
        lines << QStringLiteral("名称:     %1").arg(a.name);
        lines << QStringLiteral("头像:     %1").arg(a.avatar.isEmpty() ? QStringLiteral("-") : a.avatar);
        if (a.totalDiskBytes > 0)
            lines << QStringLiteral("磁盘占用: %1").arg(humanReadableSize(a.totalDiskBytes));
        if (a.profileLastModified.isValid())
            lines << QStringLiteral("最后修改: %1").arg(a.profileLastModified.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));

        lines << QString() << QStringLiteral("--- 模型配置 ---");
        const QString model = a.selectedModelId.trimmed();
        lines << QStringLiteral("模型:     %1").arg(model.isEmpty() ? QStringLiteral("-") : model);
        if (!a.providerInstanceId.isEmpty())
            lines << QStringLiteral("接入点:   %1").arg(a.providerInstanceId);
        if (!a.configId.isEmpty() && a.configId != a.providerInstanceId)
            lines << QStringLiteral("配置ID:   %1").arg(a.configId);
        lines << QStringLiteral("递归深度: %1").arg(a.recursionDepth);
        lines << QStringLiteral("委派:     %1").arg(a.delegateEnabled ? QStringLiteral("启用") : QStringLiteral("禁用"));

        lines << QString() << QStringLiteral("--- 角色描述 ---");
        lines << (a.description.isEmpty() ? QStringLiteral("(无)") : a.description);

        lines << QString() << QStringLiteral("--- 系统提示词 ---");
        if (a.systemPrompt.isEmpty()) {
            lines << QStringLiteral("(无)");
        } else {
            const QString prompt = a.systemPrompt.length() > 500
                ? a.systemPrompt.left(500) + QStringLiteral("...（共 %1 字符）").arg(a.systemPrompt.length())
                : a.systemPrompt;
            lines << prompt;
        }

        lines << QString() << QStringLiteral("--- 工具权限 (%1) ---").arg(a.allowedTools.size());
        if (a.allowedTools.isEmpty()) {
            lines << QStringLiteral("(无)");
        } else {
            lines << a.allowedTools.join(QStringLiteral(", "));
        }

        lines << QString() << QStringLiteral("--- 参与会话 (%1) ---").arg(a.sessions.size());
        if (a.sessions.isEmpty()) {
            lines << QStringLiteral("(无会话记录)");
        } else {
            for (const SessionSummary& s : a.sessions) {
                const QString lastActive = s.lastActiveAt.isValid()
                    ? s.lastActiveAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                    : QStringLiteral("-");
                lines << QStringLiteral("  [%1] %2 (%3, %4 msgs, 最后活跃: %5)")
                             .arg(clipId(s.sessionId, 8),
                                  s.title.isEmpty() ? QStringLiteral("(无标题)") : s.title,
                                  s.type.isEmpty() ? QStringLiteral("-") : s.type,
                                  QString::number(s.messageCount),
                                  lastActive);
            }
        }
    }

    lines << QString();
    lines << QStringLiteral("共 %1 个 Agent").arg(result.agents.size());
    return lines.join(QLatin1Char('\n'));
}

} // namespace LogAgentLister
