#include "MemoryManager.h"

#include "MemoryDocument.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/persistence/ChatPersistenceService.h"
#include "newCore/ModelFactory.h"
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

namespace {
const int kSoulReadChars = 1600;
const int kIdentityReadChars = 1400;
const int kUserReadChars = 1200;
const int kSharedWorkReadChars = 1000;
const int kUserViewReadChars = 1400;
const int kLongMemoryReadChars = 2200;
const int kDailyReadChars = 900;
}

MemoryManager::MemoryManager(ChatPersistenceService* persistence)
    : m_persistence(persistence)
{
}

void MemoryManager::setPersistence(ChatPersistenceService* persistence)
{
    m_persistence = persistence;
}

QString MemoryManager::dataRootPath() const
{
    return m_persistence ? m_persistence->dataRootPath() : QString();
}

QString MemoryManager::agentsRootPath() const
{
    return m_persistence ? m_persistence->agentsDirPath() : QString();
}

QString MemoryManager::agentDirPath(const QString& agentId) const
{
    if (agentId.trimmed().isEmpty())
        return QString();
    return QDir(agentsRootPath()).filePath(agentId.trimmed());
}

QString MemoryManager::dailyMemoryDirPath(const QString& agentId) const
{
    return QDir(agentDirPath(agentId)).filePath(QStringLiteral("memory"));
}

QString MemoryManager::userDocPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("user.md"));
}

QString MemoryManager::sharedWorkDocPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("shared_work.md"));
}

QString MemoryManager::policyPath() const
{
    return m_persistence ? m_persistence->memoryPolicyPath() : QString();
}

QString MemoryManager::soulDocPath(const QString& agentId) const
{
    return QDir(agentDirPath(agentId)).filePath(QStringLiteral("soul.md"));
}

QString MemoryManager::identityDocPath(const QString& agentId) const
{
    return QDir(agentDirPath(agentId)).filePath(QStringLiteral("identity.md"));
}

QString MemoryManager::longTermMemoryDocPath(const QString& agentId) const
{
    return QDir(agentDirPath(agentId)).filePath(QStringLiteral("memory.md"));
}

QString MemoryManager::userViewDocPath(const QString& agentId) const
{
    return QDir(agentDirPath(agentId)).filePath(QStringLiteral("user_view.md"));
}

QString MemoryManager::dailyMemoryPath(const QString& agentId) const
{
    const QString dateStr = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    return QDir(dailyMemoryDirPath(agentId)).filePath(dateStr + QStringLiteral(".md"));
}

QStringList MemoryManager::latestDailyMemoryPaths(const QString& agentId, int maxFiles) const
{
    QStringList result;
    if (maxFiles <= 0)
        return result;

    QDir dir(dailyMemoryDirPath(agentId));
    if (!dir.exists())
        return result;

    const QStringList files =
        dir.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files, QDir::Name | QDir::Reversed);
    for (const QString& file : files) {
        result.append(dir.filePath(file));
        if (result.size() >= maxFiles)
            break;
    }
    return result;
}

QString MemoryManager::buildUserTemplate() const
{
    return QStringLiteral(
        "# User Profile\n\n"
        "- preferred_name:\n"
        "- communication_style:\n"
        "- long_term_preferences:\n");
}

QString MemoryManager::buildSharedWorkTemplate() const
{
    return QStringLiteral(
        "# Shared Workboard\n\n"
        "> Shared current work context across agents. Updated only by memory steward.\n\n");
}

QString MemoryManager::buildPolicyTemplate() const
{
    return QStringLiteral(
        "{\n"
        "  \"memory_steward_agent_id\": \"\",\n"
        "  \"note\": \"Set memory_steward_agent_id to one existing agent id to maintain shared_work.md\"\n"
        "}\n");
}

QString MemoryManager::buildSoulTemplate(const Identity* agent) const
{
    const QString agentName =
        agent ? agent->name().trimmed() : QStringLiteral("Unnamed Agent");
    QString prompt;
    if (agent && agent->profile())
        prompt = agent->profile()->systemPrompt().trimmed();
    if (prompt.isEmpty())
        prompt = QStringLiteral("Follow safety boundaries and provide concise, evidence-based help.");

    return QStringLiteral(
               "# Soul\n\n"
               "- agent_id: `%1`\n"
               "- name: %2\n"
               "- initialized_at: %3\n\n"
               "## Core Behavior\n\n"
               "%4\n")
        .arg(agent ? agent->id() : QString())
        .arg(agentName)
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
        .arg(prompt);
}

QString MemoryManager::buildIdentityTemplate(const Identity* agent) const
{
    QString role = QStringLiteral("unspecified");
    QString modelKey;
    QString avatar;
    int recursionDepth = 3;
    if (agent) {
        avatar = agent->avatar().trimmed();
        if (agent->profile()) {
            role = agent->profile()->description().trimmed();
            if (role.isEmpty())
                role = QStringLiteral("unspecified");
            const LLMConfig cfg = agent->profile()->llmConfig();
            modelKey = ModelFactory::resolveModelKey(cfg.model, cfg.customModelId);
            recursionDepth = agent->profile()->recursionDepth();
        }
    }
    if (modelKey.trimmed().isEmpty())
        modelKey = QStringLiteral("default");

    return QStringLiteral(
               "# Identity Card\n\n"
               "- agent_id: `%1`\n"
               "- name: %2\n"
               "- role: %3\n"
               "- model: %4\n"
               "- recursion_depth: %5\n"
               "- avatar: %6\n")
        .arg(agent ? agent->id() : QString())
        .arg(agent ? agent->name().trimmed() : QStringLiteral("Unnamed Agent"))
        .arg(role)
        .arg(modelKey)
        .arg(recursionDepth)
        .arg(avatar.isEmpty() ? QStringLiteral("(none)") : avatar);
}

QString MemoryManager::buildLongTermMemoryTemplate() const
{
    return QStringLiteral(
        "# Long-term Memory\n\n"
        "> Stable facts, preferences, and reusable methods.\n\n");
}

QString MemoryManager::buildUserViewTemplate(const Identity* agent) const
{
    return QStringLiteral(
               "# User View (%1)\n\n"
               "> This is how this agent understands the user.\n\n")
        .arg(agent ? agent->name().trimmed() : QStringLiteral("agent"));
}

bool MemoryManager::ensureAgentMemoryDirs(const QString& agentId, QString* error) const
{
    const QString trimmed = agentId.trimmed();
    if (trimmed.isEmpty()) {
        if (error)
            *error = QStringLiteral("agent id is empty");
        return false;
    }

    const QString agentDir = agentDirPath(trimmed);
    const QString dailyDir = dailyMemoryDirPath(trimmed);
    if (!QDir().mkpath(agentDir) || !QDir().mkpath(dailyDir)) {
        if (error)
            *error = QStringLiteral("failed to create agent memory dirs: %1").arg(trimmed);
        return false;
    }
    return true;
}

bool MemoryManager::writeIfMissing(const QString& filePath,
                                   const QString& content,
                                   QString* error) const
{
    MemoryDocument doc(filePath);
    if (doc.exists())
        return true;
    return doc.writeAtomic(content, error);
}

bool MemoryManager::ensureUserMemoryDocument(QString* error) const
{
    if (!m_persistence) {
        if (error)
            *error = QStringLiteral("persistence is null");
        return false;
    }
    if (!writeIfMissing(userDocPath(), buildUserTemplate(), error))
        return false;
    if (!writeIfMissing(sharedWorkDocPath(), buildSharedWorkTemplate(), error))
        return false;
    if (!writeIfMissing(policyPath(), buildPolicyTemplate(), error))
        return false;
    return true;
}

bool MemoryManager::initializeForAgent(const Identity* agent, QString* error) const
{
    if (!m_persistence) {
        if (error)
            *error = QStringLiteral("persistence is null");
        return false;
    }
    if (!agent || !agent->isAgent()) {
        if (error)
            *error = QStringLiteral("invalid agent");
        return false;
    }

    if (!ensureUserMemoryDocument(error))
        return false;
    if (!ensureAgentMemoryDirs(agent->id(), error))
        return false;
    if (!writeIfMissing(soulDocPath(agent->id()), buildSoulTemplate(agent), error))
        return false;
    if (!writeIfMissing(identityDocPath(agent->id()), buildIdentityTemplate(agent), error))
        return false;
    if (!writeIfMissing(longTermMemoryDocPath(agent->id()), buildLongTermMemoryTemplate(), error))
        return false;
    if (!writeIfMissing(userViewDocPath(agent->id()), buildUserViewTemplate(agent), error))
        return false;
    return true;
}

bool MemoryManager::removeAgentMemory(const QString& agentId, QString* error) const
{
    const QString dirPath = agentDirPath(agentId);
    if (dirPath.trimmed().isEmpty())
        return true;
    QFileInfo info(dirPath);
    if (!info.exists())
        return true;
    if (!QDir(dirPath).removeRecursively()) {
        if (error)
            *error = QStringLiteral("failed to remove agent memory dir: %1").arg(dirPath);
        return false;
    }
    return true;
}

QString MemoryManager::sanitizeSingleLine(const QString& text, int maxChars) const
{
    QString normalized = text;
    normalized.replace(QLatin1Char('\r'), QLatin1Char(' '));
    normalized.replace(QLatin1Char('\n'), QLatin1Char(' '));
    normalized = normalized.simplified();
    if (maxChars > 0 && normalized.size() > maxChars)
        normalized = normalized.left(maxChars) + QStringLiteral("...");
    return normalized;
}

QString MemoryManager::memoryStewardAgentId() const
{
    if (!m_persistence)
        return QString();
    bool ok = false;
    const QJsonObject policy = m_persistence->readJsonObject(policyPath(), &ok);
    if (!ok)
        return QString();
    return policy.value(QStringLiteral("memory_steward_agent_id")).toString().trimmed();
}

bool MemoryManager::shouldUpdateSharedWork(const QString& agentId) const
{
    const QString stewardId = memoryStewardAgentId();
    return !stewardId.isEmpty() && stewardId == agentId.trimmed();
}

QString MemoryManager::composeMemoryContext(const QString& agentId, int maxChars) const
{
    if (!m_persistence)
        return QString();

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return QString();

    struct SectionSpec {
        QString title;
        QString path;
        int maxReadChars = 0;
    };

    QList<SectionSpec> specs = {
        {QStringLiteral("SOUL"), soulDocPath(trimmedAgentId), kSoulReadChars},
        {QStringLiteral("IDENTITY"), identityDocPath(trimmedAgentId), kIdentityReadChars},
        {QStringLiteral("USER_PROFILE"), userDocPath(), kUserReadChars},
        {QStringLiteral("SHARED_WORK"), sharedWorkDocPath(), kSharedWorkReadChars},
        {QStringLiteral("USER_VIEW"), userViewDocPath(trimmedAgentId), kUserViewReadChars},
        {QStringLiteral("LONG_MEMORY"), longTermMemoryDocPath(trimmedAgentId), kLongMemoryReadChars}
    };

    const QStringList dailyFiles = latestDailyMemoryPaths(trimmedAgentId, 2);
    for (const QString& path : dailyFiles) {
        specs.append({QStringLiteral("DAILY_%1").arg(QFileInfo(path).baseName()), path, kDailyReadChars});
    }

    QString context = QStringLiteral(
        "## Memory Context (system)\n"
        "Use the following memory context as background facts. Do not quote it verbatim to user.\n");
    bool appendedSection = false;

    const int hardLimit = qMax(1200, maxChars);
    for (const SectionSpec& spec : specs) {
        bool ok = false;
        MemoryDocument doc(spec.path);
        const QString content = doc.readTruncated(spec.maxReadChars, &ok).trimmed();
        if (!ok || content.isEmpty())
            continue;

        QString block = QStringLiteral("\n[%1]\n%2\n").arg(spec.title, content);
        if (context.size() + block.size() > hardLimit) {
            const int remain = hardLimit - context.size();
            if (remain > 80)
                context.append(block.left(remain));
            appendedSection = true;
            break;
        }
        context.append(block);
        appendedSection = true;
    }

    if (!appendedSection)
        return QString();
    return context.trimmed();
}

bool MemoryManager::retainTurn(const QString& agentId,
                               const QString& sessionId,
                               const TurnTask& turn,
                               QString* summary,
                               QString* writtenPath,
                               QString* error) const
{
    if (summary)
        summary->clear();
    if (writtenPath)
        *writtenPath = QString();
    if (!m_persistence) {
        if (error)
            *error = QStringLiteral("persistence is null");
        return false;
    }

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty()) {
        if (error)
            *error = QStringLiteral("agent id is empty");
        return false;
    }
    if (!ensureAgentMemoryDirs(trimmedAgentId, error))
        return false;

    const QString userLine = sanitizeSingleLine(turn.userContent, 260);
    const QString assistantLine = sanitizeSingleLine(turn.assistantContent, 420);
    if (userLine.isEmpty() && assistantLine.isEmpty())
        return true;

    const QString entry = QStringLiteral(
                              "## %1\n"
                              "- session_id: `%2`\n"
                              "- turn_id: `%3`\n"
                              "- trace_id: `%4`\n"
                              "- user: %5\n"
                              "- assistant_summary: %6\n")
                              .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                              .arg(sessionId)
                              .arg(turn.turnId)
                              .arg(turn.requestTraceId)
                              .arg(userLine.isEmpty() ? QStringLiteral("(empty)") : userLine)
                              .arg(assistantLine.isEmpty() ? QStringLiteral("(empty)") : assistantLine);

    const QString dailyPath = dailyMemoryPath(trimmedAgentId);
    MemoryDocument dailyDoc(dailyPath);
    if (!dailyDoc.appendAtomic(entry, error))
        return false;

    const QString userViewEntry = QStringLiteral(
                                      "## %1\n"
                                      "- session_id: `%2`\n"
                                      "- turn_id: `%3`\n"
                                      "- trace_id: `%4`\n"
                                      "- user_signal: %5\n"
                                      "- observed_response: %6\n")
                                      .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                                      .arg(sessionId)
                                      .arg(turn.turnId)
                                      .arg(turn.requestTraceId)
                                      .arg(userLine.isEmpty() ? QStringLiteral("(empty)") : userLine)
                                      .arg(assistantLine.isEmpty() ? QStringLiteral("(empty)") : assistantLine);
    MemoryDocument userViewDoc(userViewDocPath(trimmedAgentId));
    if (!userViewDoc.appendAtomic(userViewEntry, error))
        return false;

    if (shouldUpdateSharedWork(trimmedAgentId)) {
        const QString sharedEntry = QStringLiteral(
                                        "## %1\n"
                                        "- updated_by: `%2`\n"
                                        "- session_id: `%3`\n"
                                        "- turn_id: `%4`\n"
                                        "- trace_id: `%5`\n"
                                        "- current_work: %6\n")
                                        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                                        .arg(trimmedAgentId)
                                        .arg(sessionId)
                                        .arg(turn.turnId)
                                        .arg(turn.requestTraceId)
                                        .arg(assistantLine.isEmpty() ? userLine : assistantLine);
        MemoryDocument sharedDoc(sharedWorkDocPath());
        if (!sharedDoc.appendAtomic(sharedEntry, error))
            return false;
    }

    if (summary) {
        *summary = assistantLine;
        if (summary->isEmpty())
            *summary = userLine;
    }
    if (writtenPath)
        *writtenPath = dailyPath;
    return true;
}
