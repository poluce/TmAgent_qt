#include "LogAgentLister.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

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

QString readSnippet(const QString& filePath, int maxLines = 20, int maxChars = 600)
{
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QString();
    QString result;
    int lines = 0;
    while (!file.atEnd() && lines < maxLines && result.size() < maxChars) {
        result += QString::fromUtf8(file.readLine());
        ++lines;
    }
    return result.trimmed();
}

qint64 dirTotalSize(const QString& dirPath)
{
    qint64 total = 0;
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QJsonObject readJsonFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QJsonObject();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

QStringList jsonArrayToStringList(const QJsonArray& arr)
{
    QStringList list;
    for (const QJsonValue& v : arr)
        list.append(v.toString());
    return list;
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

} // anonymous namespace

// ── 核心：列出 Agent ──────────────────────────────────────────────

ListResult listAgents(const QueryOptions& options)
{
    ListResult result;

    const QString rootPath = options.dataRootPath.isEmpty()
        ? QDir::home().filePath(QStringLiteral(".tmagent"))
        : options.dataRootPath;

    // 1. 扫描 agent 目录
    const QString agentsPath = QDir(rootPath).filePath(QStringLiteral("identities/agents"));
    QDir agentsDir(agentsPath);
    if (!agentsDir.exists()) {
        result.warnings.append(
            QStringLiteral("Agent 目录不存在: %1").arg(QDir::toNativeSeparators(agentsPath)));
        return result;
    }

    const QStringList agentDirs = agentsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    // 2. 预加载会话索引（用于关联 agent 与会话）
    QJsonArray sessionsArr;
    {
        const QString indexPath = QDir(rootPath).filePath(QStringLiteral("sessions/index.json"));
        const QJsonObject indexObj = readJsonFile(indexPath);
        sessionsArr = indexObj.value(QStringLiteral("sessions")).toArray();
    }

    // 如果 index.json 不存在或为空，回退扫描 meta.json
    QVector<QJsonObject> metaFallback;
    if (sessionsArr.isEmpty()) {
        const QString sessionsDataPath = QDir(rootPath).filePath(QStringLiteral("sessions/data"));
        QDir sessionsDataDir(sessionsDataPath);
        if (sessionsDataDir.exists()) {
            const QStringList sessionDirs = sessionsDataDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& sid : sessionDirs) {
                const QString metaPath = sessionsDataDir.filePath(sid + QStringLiteral("/meta.json"));
                QJsonObject meta = readJsonFile(metaPath);
                if (!meta.isEmpty()) {
                    if (!meta.contains(QStringLiteral("id")))
                        meta.insert(QStringLiteral("id"), sid);
                    metaFallback.append(meta);
                }
            }
        }
    }

    // 3. 遍历每个 agent
    for (const QString& agentUuid : agentDirs) {
        const QString agentPath = agentsDir.filePath(agentUuid);

        // 读取 profile.json
        const QString profilePath = QDir(agentPath).filePath(QStringLiteral("profile.json"));
        const QJsonObject profileObj = readJsonFile(profilePath);
        if (profileObj.isEmpty()) {
            result.warnings.append(
                QStringLiteral("无法读取 profile.json: %1").arg(agentUuid));
            continue;
        }

        AgentInfo info;
        info.agentId = profileObj.value(QStringLiteral("id")).toString(agentUuid);
        info.name = profileObj.value(QStringLiteral("name")).toString();
        info.avatar = profileObj.value(QStringLiteral("avatar")).toString();

        const QJsonObject profile = profileObj.value(QStringLiteral("profile")).toObject();
        info.configId = profile.value(QStringLiteral("configId")).toString();
        info.providerInstanceId = profile.value(QStringLiteral("providerInstanceId")).toString();
        info.selectedModelId = profile.value(QStringLiteral("selectedModelId")).toString();
        info.description = profile.value(QStringLiteral("description")).toString();
        info.systemPrompt = profile.value(QStringLiteral("systemPrompt")).toString();
        info.recursionDepth = profile.value(QStringLiteral("recursionDepth")).toInt(3);
        info.delegateEnabled = profile.value(QStringLiteral("delegateEnabled")).toBool(true);
        info.allowedTools = jsonArrayToStringList(profile.value(QStringLiteral("allowedTools")).toArray());

        info.profileLastModified = QFileInfo(profilePath).lastModified();

        // 过滤
        if (!options.filterAgentId.isEmpty()) {
            if (info.agentId != options.filterAgentId)
                continue;
        }
        if (!options.filterAgentName.isEmpty()) {
            if (!info.name.contains(options.filterAgentName, Qt::CaseInsensitive))
                continue;
        }

        // detail 模式：加载记忆摘要
        if (options.detail) {
            info.identitySnippet = readSnippet(QDir(agentPath).filePath(QStringLiteral("identity.md")));
            info.soulSnippet = readSnippet(QDir(agentPath).filePath(QStringLiteral("soul.md")));
            info.memorySnippet = readSnippet(QDir(agentPath).filePath(QStringLiteral("memory.md")));
            info.userViewSnippet = readSnippet(QDir(agentPath).filePath(QStringLiteral("user_view.md")));

            // memory/ 目录统计
            QDir memoryDir(QDir(agentPath).filePath(QStringLiteral("memory")));
            if (memoryDir.exists())
                info.dailyMemoryFileCount = memoryDir.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files).size();

            info.hasMemoryIndex = QFileInfo::exists(QDir(agentPath).filePath(QStringLiteral("memory_index.sqlite")));
            info.totalDiskBytes = dirTotalSize(agentPath);
        }

        // 关联会话
        auto matchSessions = [&](const QJsonObject& sessionObj) {
            const QJsonArray participants = sessionObj.value(QStringLiteral("participants")).toArray();
            for (const QJsonValue& p : participants) {
                if (p.toString() == info.agentId) {
                    SessionSummary ss;
                    ss.sessionId = sessionObj.value(QStringLiteral("id")).toString();
                    ss.title = sessionObj.value(QStringLiteral("title")).toString();
                    ss.type = sessionObj.value(QStringLiteral("type")).toString();
                    ss.messageCount = sessionObj.value(QStringLiteral("messageCount")).toInt(0);
                    ss.lastActiveAt = parseTimestamp(sessionObj.value(QStringLiteral("lastActiveAt")).toString());
                    info.sessions.append(ss);
                    break;
                }
            }
        };

        if (!sessionsArr.isEmpty()) {
            for (const QJsonValue& v : sessionsArr)
                matchSessions(v.toObject());
        } else {
            for (const QJsonObject& meta : metaFallback)
                matchSessions(meta);
        }

        result.agents.append(info);
    }

    // 按名称排序
    std::sort(result.agents.begin(), result.agents.end(),
        [](const AgentInfo& a, const AgentInfo& b) {
            return a.name.toLower() < b.name.toLower();
        });

    return result;
}

// ── Table 格式 ────────────────────────────────────────────────────

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

// ── JSON 格式 ─────────────────────────────────────────────────────

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

        // 记忆（如果有）
        if (!a.identitySnippet.isEmpty() || !a.soulSnippet.isEmpty()
            || !a.memorySnippet.isEmpty() || !a.userViewSnippet.isEmpty()) {
            QJsonObject mem;
            mem.insert(QStringLiteral("identity_snippet"), a.identitySnippet);
            mem.insert(QStringLiteral("soul_snippet"), a.soulSnippet);
            mem.insert(QStringLiteral("memory_snippet"), a.memorySnippet);
            mem.insert(QStringLiteral("user_view_snippet"), a.userViewSnippet);
            mem.insert(QStringLiteral("daily_memory_files"), a.dailyMemoryFileCount);
            mem.insert(QStringLiteral("has_memory_index"), a.hasMemoryIndex);
            obj.insert(QStringLiteral("memory"), mem);
        }

        // 会话
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

// ── Report 格式 ───────────────────────────────────────────────────

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

        // 模型配置
        lines << QString() << QStringLiteral("--- 模型配置 ---");
        const QString model = a.selectedModelId.trimmed();
        lines << QStringLiteral("模型:     %1").arg(model.isEmpty() ? QStringLiteral("-") : model);
        if (!a.providerInstanceId.isEmpty())
            lines << QStringLiteral("接入点:   %1").arg(a.providerInstanceId);
        if (!a.configId.isEmpty() && a.configId != a.providerInstanceId)
            lines << QStringLiteral("配置ID:   %1").arg(a.configId);
        lines << QStringLiteral("递归深度: %1").arg(a.recursionDepth);
        lines << QStringLiteral("委派:     %1").arg(a.delegateEnabled ? QStringLiteral("启用") : QStringLiteral("禁用"));

        // 角色描述
        lines << QString() << QStringLiteral("--- 角色描述 ---");
        lines << (a.description.isEmpty() ? QStringLiteral("(无)") : a.description);

        // 系统提示词
        lines << QString() << QStringLiteral("--- 系统提示词 ---");
        if (a.systemPrompt.isEmpty()) {
            lines << QStringLiteral("(无)");
        } else {
            const QString prompt = a.systemPrompt.length() > 500
                ? a.systemPrompt.left(500) + QStringLiteral("...（共 %1 字符）").arg(a.systemPrompt.length())
                : a.systemPrompt;
            lines << prompt;
        }

        // 工具权限
        lines << QString() << QStringLiteral("--- 工具权限 (%1) ---").arg(a.allowedTools.size());
        if (a.allowedTools.isEmpty()) {
            lines << QStringLiteral("(无)");
        } else {
            lines << a.allowedTools.join(QStringLiteral(", "));
        }

        // 记忆信息（detail 模式才有内容）
        if (!a.identitySnippet.isEmpty()) {
            lines << QString() << QStringLiteral("--- 身份卡片 (identity.md) ---");
            lines << a.identitySnippet;
        }
        if (!a.soulSnippet.isEmpty()) {
            lines << QString() << QStringLiteral("--- 灵魂定义 (soul.md) ---");
            lines << a.soulSnippet;
        }
        if (!a.memorySnippet.isEmpty()) {
            lines << QString() << QStringLiteral("--- 长期记忆 (memory.md) ---");
            lines << a.memorySnippet;
        }
        if (!a.userViewSnippet.isEmpty()) {
            lines << QString() << QStringLiteral("--- 用户理解 (user_view.md) ---");
            lines << a.userViewSnippet;
        }

        if (a.dailyMemoryFileCount > 0 || a.hasMemoryIndex) {
            lines << QString() << QStringLiteral("--- 日常记忆 ---");
            lines << QStringLiteral("memory/ 目录下共 %1 个日记文件").arg(a.dailyMemoryFileCount);
            lines << QStringLiteral("记忆索引: %1").arg(a.hasMemoryIndex ? QStringLiteral("存在") : QStringLiteral("不存在"));
        }

        // 参与会话
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
