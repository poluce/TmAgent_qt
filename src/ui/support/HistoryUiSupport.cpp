#include "HistoryUiSupport.h"

#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include <QComboBox>
#include <QDateTime>
#include <QHash>

namespace {

bool isHeartbeatPromptMessageText(const QString& text)
{
    return text.trimmed().startsWith(QStringLiteral("【系统心跳任务】"));
}

bool isHeartbeatNoChangeReplyText(const QString& text)
{
    const QString t = text.trimmed();
    return t == QStringLiteral("当前无关键更新。")
        || t == QStringLiteral("当前无关键更新")
        || t == QStringLiteral("无关键更新。")
        || t == QStringLiteral("无关键更新");
}

QList<Message> filterVisibleSessionMessages(const QList<Message>& allMessages, bool filterHeartbeatMessages, int* filteredHeartbeatCount)
{
    if (filteredHeartbeatCount)
        *filteredHeartbeatCount = 0;
    if (!filterHeartbeatMessages)
        return allMessages;

    QSet<QString> heartbeatTraceIds;
    for (const Message& msg : allMessages) {
        if (!msg.traceId.trimmed().isEmpty() && isHeartbeatPromptMessageText(msg.content.text))
            heartbeatTraceIds.insert(msg.traceId.trimmed());
    }

    QList<Message> visibleMessages;
    visibleMessages.reserve(allMessages.size());
    int filteredCount = 0;
    for (const Message& msg : allMessages) {
        const QString traceId = msg.traceId.trimmed();
        if (isHeartbeatPromptMessageText(msg.content.text)) {
            ++filteredCount;
            continue;
        }
        if (!traceId.isEmpty()
            && heartbeatTraceIds.contains(traceId)
            && isHeartbeatNoChangeReplyText(msg.content.text)) {
            ++filteredCount;
            continue;
        }
        visibleMessages.append(msg);
    }

    if (filteredHeartbeatCount)
        *filteredHeartbeatCount = filteredCount;
    return visibleMessages;
}

} // namespace

namespace HistoryUiSupport {

QList<ChatWidget::HistoryMessage> buildSessionHistoryMessages(const QList<Message>& allMessages,
                                                              const SessionRestoreOptions& options)
{
    int filteredHeartbeatCount = 0;
    const QList<Message> visibleMessages =
        filterVisibleSessionMessages(allMessages, options.filterHeartbeatMessages, &filteredHeartbeatCount);

    QList<Message> messages = visibleMessages;
    if (options.maxRestoreMessages > 0 && visibleMessages.size() > options.maxRestoreMessages)
        messages = visibleMessages.mid(visibleMessages.size() - options.maxRestoreMessages);

    QList<ChatWidget::HistoryMessage> historyMessages;
    historyMessages.reserve(messages.size() + 1);

    const bool shouldShowRestoreNotice =
        options.maxRestoreMessages > 0
        && (visibleMessages.size() > options.maxRestoreMessages || filteredHeartbeatCount > 0);
    if (shouldShowRestoreNotice) {
        ChatWidget::HistoryMessage notice;
        notice.messageType = ChatWidgetMessage::MessageType::System;
        notice.senderId = QStringLiteral("system");
        notice.displayName = QStringLiteral("System");
        notice.timestamp = QDateTime::currentDateTime();
        if (filteredHeartbeatCount > 0) {
            notice.content = QObject::tr("（仅显示最近 %1 条可见消息，共 %2 条；已过滤心跳消息 %3 条）")
                                 .arg(options.maxRestoreMessages)
                                 .arg(allMessages.size())
                                 .arg(filteredHeartbeatCount);
        } else {
            notice.content = QObject::tr("（仅显示最近 %1 条消息，共 %2 条）")
                                 .arg(options.maxRestoreMessages)
                                 .arg(visibleMessages.size());
        }
        historyMessages.append(notice);
    }

    QHash<QString, Identity*> senderIdentityCache;
    senderIdentityCache.reserve(qMax(8, messages.size()));

    for (const Message& msg : messages) {
        if (msg.content.type == MessageContent::Type::ToolCall
            || msg.content.type == MessageContent::Type::ToolResult) {
            continue;
        }
        if (msg.content.type != MessageContent::Type::File && msg.content.text.trimmed().isEmpty())
            continue;

        ChatWidget::HistoryMessage historyMsg;
        historyMsg.messageId = msg.id;
        historyMsg.timestamp = msg.timestamp.isValid() ? msg.timestamp : QDateTime::currentDateTime();

        if (msg.content.type == MessageContent::Type::System || msg.senderId == QLatin1String("system")) {
            historyMsg.messageType = ChatWidgetMessage::MessageType::System;
            historyMsg.senderId = QStringLiteral("system");
            historyMsg.displayName = QStringLiteral("System");
        } else {
            Identity* senderIdentity = senderIdentityCache.value(msg.senderId, nullptr);
            if (!senderIdentityCache.contains(msg.senderId)) {
                senderIdentity = options.identityResolver ? options.identityResolver(msg.senderId) : nullptr;
                senderIdentityCache.insert(msg.senderId, senderIdentity);
            }

            const bool isSelf = (!options.viewerIdentityId.isEmpty() && msg.senderId == options.viewerIdentityId);
            if (!senderIdentity || senderIdentity->isUser()) {
                historyMsg.senderId = options.userSenderId;
                historyMsg.displayName = options.userDisplayName;
                historyMsg.avatarPath = options.userAvatarPath;
                historyMsg.isMine = options.userMessagesAreMine;
            } else if (isSelf) {
                historyMsg.senderId = options.viewerIdentityId;
                historyMsg.displayName = senderIdentity->name().trimmed().isEmpty()
                    ? options.selfDisplayName
                    : senderIdentity->name();
                historyMsg.avatarPath = senderIdentity->avatar().trimmed();
                historyMsg.isMine = true;
            } else {
                historyMsg.senderId = senderIdentity->id();
                historyMsg.displayName = senderIdentity->name().trimmed().isEmpty()
                    ? options.defaultAssistantDisplayName
                    : senderIdentity->name().trimmed();
                historyMsg.avatarPath = senderIdentity->avatar().trimmed();
            }
        }

        if (msg.content.type == MessageContent::Type::File) {
            const QString filePath = msg.content.payload.value(QStringLiteral("file_path")).toString();
            const QString fileName = msg.content.payload.value(QStringLiteral("file_name")).toString();
            if (options.requireCompleteFilePayload && (filePath.isEmpty() || fileName.isEmpty()))
                continue;

            historyMsg.messageType = ChatWidgetMessage::MessageType::File;
            historyMsg.filePath = filePath;
            historyMsg.fileName = fileName;
            historyMsg.fileSize = static_cast<qint64>(msg.content.payload.value(QStringLiteral("file_size")).toDouble());
            historyMsg.content = msg.content.text.isEmpty() ? fileName : msg.content.text;
        } else {
            historyMsg.messageType = ChatWidgetMessage::MessageType::Text;
            historyMsg.content = msg.content.text;
        }

        historyMessages.append(historyMsg);
    }

    return historyMessages;
}

QList<ChatWidget::HistoryMessage> buildRawHistoryMessages(const QJsonArray& history,
                                                          const RawHistoryRestoreOptions& options)
{
    QList<ChatWidget::HistoryMessage> historyMessages;
    historyMessages.reserve(history.size());

    for (const QJsonValue& value : history) {
        const QJsonObject obj = value.toObject();
        const QString role = obj.value(QStringLiteral("role")).toString();
        const QString content = obj.value(QStringLiteral("content")).toString();
        if (content.isEmpty() || role == QLatin1String("tool"))
            continue;

        const bool isUser = (role == QLatin1String("user"));
        ChatWidget::HistoryMessage msg;
        msg.messageId = obj.value(QStringLiteral("message_id")).toString().trimmed();
        msg.content = content;
        msg.timestamp = QDateTime::currentDateTime();
        msg.senderId = isUser ? options.userSenderId : options.fallbackAssistantSenderId;
        msg.displayName = isUser ? options.userDisplayName : options.assistantDisplayName;
        msg.avatarPath = isUser ? options.userAvatarPath : options.assistantAvatarPath;
        msg.isMine = isUser && options.userMessagesAreMine;
        historyMessages.append(msg);
    }

    return historyMessages;
}

void populateFilterCombo(QComboBox* combo)
{
    if (!combo)
        return;
    combo->clear();
    combo->addItem(ExecutionHistory::filterModeText(ExecutionHistory::FilterMode::All),
                   static_cast<int>(ExecutionHistory::FilterMode::All));
    combo->addItem(ExecutionHistory::filterModeText(ExecutionHistory::FilterMode::FailuresOnly),
                   static_cast<int>(ExecutionHistory::FilterMode::FailuresOnly));
    combo->addItem(ExecutionHistory::filterModeText(ExecutionHistory::FilterMode::ToolCallsOnly),
                   static_cast<int>(ExecutionHistory::FilterMode::ToolCallsOnly));
    combo->addItem(ExecutionHistory::filterModeText(ExecutionHistory::FilterMode::EventsOnly),
                   static_cast<int>(ExecutionHistory::FilterMode::EventsOnly));
    combo->addItem(ExecutionHistory::filterModeText(ExecutionHistory::FilterMode::ActiveOnly),
                   static_cast<int>(ExecutionHistory::FilterMode::ActiveOnly));
}

void populateRecentCombo(QComboBox* combo)
{
    if (!combo)
        return;
    combo->clear();
    combo->addItem(QObject::tr("全部"), 0);
    combo->addItem(QObject::tr("10 条"), 10);
    combo->addItem(QObject::tr("20 条"), 20);
    combo->addItem(QObject::tr("50 条"), 50);
}

ExecutionHistory::FilterMode selectedFilterMode(const QComboBox* combo)
{
    if (!combo)
        return ExecutionHistory::FilterMode::All;
    return static_cast<ExecutionHistory::FilterMode>(combo->currentData().toInt());
}

int selectedRecentLimit(const QComboBox* combo)
{
    return combo ? combo->currentData().toInt() : 0;
}

QVector<int> buildVisibleHistoryIndexes(const QVector<ExecutionHistory::Record>& records,
                                        const QComboBox* filterCombo,
                                        const QComboBox* recentCombo)
{
    return ExecutionHistory::filterRecordIndexes(records,
                                                 selectedFilterMode(filterCombo),
                                                 selectedRecentLimit(recentCombo));
}

ExecutionHistoryState buildExecutionHistoryState(const QJsonArray& history,
                                                 const QComboBox* filterCombo,
                                                 const QComboBox* recentCombo)
{
    ExecutionHistoryState state;
    state.records = ExecutionHistory::buildRecords(history);
    state.visibleIndexes = buildVisibleHistoryIndexes(state.records, filterCombo, recentCombo);
    return state;
}

QJsonArray runtimeIoHistoryForSession(const IConversationService* viewQueries, const QString& sessionId)
{
    if (!viewQueries || sessionId.trimmed().isEmpty())
        return QJsonArray();
    return viewQueries->ioHistoryForSession(sessionId);
}

void clearConversationHistory(IConversationService* viewCommands, const QString& sessionId)
{
    if (!viewCommands || sessionId.trimmed().isEmpty())
        return;
    viewCommands->clearConversationHistory(sessionId);
}

} // namespace HistoryUiSupport
