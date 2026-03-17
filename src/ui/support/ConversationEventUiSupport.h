#ifndef CONVERSATIONEVENTUISUPPORT_H
#define CONVERSATIONEVENTUISUPPORT_H

#include "core/agent/ToolTypes.h"
#include <QJsonObject>
#include <QString>

namespace ConversationEventUiSupport {

enum class EventKind {
    Ignore,
    StreamDelta,
    TurnCompleted,
    TurnFailed,
    ToolCallsStarted,
    ToolEvent,
    TurnCleared,
    TurnRejected,
    MemoryNotice,
    SyncMessagesInjected,
    TurnStarted
};

struct ParsedEvent {
    EventKind kind = EventKind::Ignore;
    QString sessionId;
    QString delta;
    QString content;
    QString error;
    QString displayError;
    ToolExecutionEvent toolEvent;
    bool refreshHistory = false;
    bool refreshSendingState = false;
};

namespace detail {

inline ToolExecutionEvent parseToolEvent(const QJsonObject& obj)
{
    ToolExecutionEvent toolEvent;
    toolEvent.toolName = obj.value(QStringLiteral("toolName")).toString();
    toolEvent.toolId = obj.value(QStringLiteral("toolId")).toString();
    toolEvent.status = obj.value(QStringLiteral("status")).toString();
    toolEvent.success = obj.value(QStringLiteral("success")).toBool(true);
    toolEvent.data = obj.value(QStringLiteral("data")).toObject();
    toolEvent.rawResult = obj.value(QStringLiteral("rawResult")).toString();
    toolEvent.formattedResult = obj.value(QStringLiteral("formattedResult")).toString();
    return toolEvent;
}

} // namespace detail

inline ParsedEvent parseConversationEvent(const QJsonObject& event)
{
    ParsedEvent parsed;
    parsed.sessionId = event.value(QStringLiteral("sessionId")).toString();
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type.isEmpty() || parsed.sessionId.isEmpty())
        return parsed;

    if (type == QLatin1String("turn_delta")) {
        parsed.kind = EventKind::StreamDelta;
        parsed.delta = event.value(QStringLiteral("delta")).toString();
        return parsed;
    }

    if (type == QLatin1String("turn_completed")) {
        parsed.kind = EventKind::TurnCompleted;
        parsed.content = event.value(QStringLiteral("fullContent")).toString();
        return parsed;
    }

    if (type == QLatin1String("turn_failed")) {
        parsed.kind = EventKind::TurnFailed;
        parsed.error = event.value(QStringLiteral("error")).toString();
        return parsed;
    }

    if (type == QLatin1String("turn_tool_calls_started")) {
        parsed.kind = EventKind::ToolCallsStarted;
        return parsed;
    }

    if (type == QLatin1String("turn_tool_event")) {
        parsed.kind = EventKind::ToolEvent;
        parsed.toolEvent = detail::parseToolEvent(event.value(QStringLiteral("toolEvent")).toObject());
        return parsed;
    }

    if (type == QLatin1String("turn_cancelled") || type == QLatin1String("turn_interrupted")) {
        parsed.kind = EventKind::TurnCleared;
        return parsed;
    }

    if (type == QLatin1String("turn_rejected")) {
        parsed.kind = EventKind::TurnRejected;
        const QString reason = event.value(QStringLiteral("reason")).toString();
        if (reason == QLatin1String("queue_overflow")) {
            const int queueDepth = event.value(QStringLiteral("queueDepth")).toInt();
            const int queueHardLimit = event.value(QStringLiteral("queueHardLimit")).toInt();
            parsed.displayError = QStringLiteral("队列已满（%1/%2），请稍后重试。").arg(queueDepth).arg(queueHardLimit);
        } else {
            parsed.displayError = QStringLiteral("请求被拒绝。");
        }
        return parsed;
    }

    if (type.startsWith(QLatin1String("memory."))) {
        parsed.kind = EventKind::MemoryNotice;
        parsed.refreshHistory = true;
        if (type == QLatin1String("memory.error")) {
            const QString memoryErr = event.value(QStringLiteral("error")).toString().trimmed();
            if (!memoryErr.isEmpty())
                parsed.displayError = QStringLiteral("记忆写入失败: %1").arg(memoryErr);
        } else if (type == QLatin1String("memory.index.error")) {
            const QString indexErr = event.value(QStringLiteral("error")).toString().trimmed();
            if (!indexErr.isEmpty())
                parsed.displayError = QStringLiteral("记忆索引更新失败: %1").arg(indexErr);
        }
        return parsed;
    }

    if (type == QLatin1String("sync_messages_injected")) {
        parsed.kind = EventKind::SyncMessagesInjected;
        parsed.refreshHistory = true;
        return parsed;
    }

    if (type == QLatin1String("turn_started")) {
        parsed.kind = EventKind::TurnStarted;
        parsed.refreshSendingState = true;
        return parsed;
    }

    return parsed;
}

} // namespace ConversationEventUiSupport

#endif // CONVERSATIONEVENTUISUPPORT_H
