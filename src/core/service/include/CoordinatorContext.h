#ifndef COORDINATORCONTEXT_H
#define COORDINATORCONTEXT_H

#include "core/model/Message.h"
#include "TurnManager.h"
#include <QJsonObject>
#include <QString>
#include <functional>

/**
 * @brief 协调器共享上下文
 *
 * 提取各协调器 Dependencies 中高频重复的回调字段，
 * 由 ChatCoordinatorFactory 一次性构建，各协调器共享引用。
 */
struct CoordinatorContext {
    // ── Pipeline 事件发射（7参数版，出现在 8 个协调器中）──
    std::function<void(const QString& sessionId,
                       const QString& type,
                       const TurnTask* turn,
                       const QString& delta,
                       const QString& error,
                       const QJsonObject& extra,
                       bool persistToDisk)> emitPipelineEvent;

    // ── 任务状态更新（出现在 6 个协调器中）──
    std::function<void(const QString& sessionId,
                       const QString& state,
                       const TurnTask* turn,
                       const QJsonObject& extra)> updateTaskState;

    // ── 任务状态文本预览（出现在 6 个协调器中）──
    std::function<QString(const QString& text, int maxChars)> taskStateTextPreview;

    // ── Agent 身份解析（出现在 5 个协调器中）──
    std::function<QString(const QString& sessionId)> agentIdentityIdForSession;

    // ── Pulse 进度上报（出现在 5 个协调器中）──
    std::function<void(const QString& agentId, const QString& summary)> reportPulseProgress;

    // ── 消息投递（出现在 4 个协调器中）──
    std::function<void(const QString& sessionId, const Message& message)> postMessage;

    // ── 心跳/后台消息判定（出现在 3-4 个协调器中）──
    std::function<bool(const QString& clientMessageId)> isBackgroundClientMessage;
    std::function<bool(const QString& clientMessageId)> isHeartbeatClientMessage;
};

#endif // COORDINATORCONTEXT_H
