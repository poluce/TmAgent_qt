#ifndef CHATUIFLOWSUPPORT_H
#define CHATUIFLOWSUPPORT_H

#include <functional>
#include <QString>

class ChatWidget;
class ThinkingIndicatorWidget;
struct ToolExecutionEvent;

namespace ChatUiFlowSupport {

enum class StreamCompletionMode {
    UpdatePlaceholder,
    ReplaceLastPlaceholder
};

void activateConversationView(ChatWidget* chatWidget,
                              const std::function<void()>& restoreAction,
                              const std::function<void()>& finalizeAction = {});
void clearConversationView(ChatWidget* chatWidget,
                           const std::function<void()>& clearAction = {});
void appendStreamingDelta(ChatWidget* chatWidget,
                          const QString& data,
                          const std::function<void()>& ensurePlaceholder);
int appendStreamingPlaceholder(ChatWidget* chatWidget,
                               const QString& senderId,
                               const QString& displayName,
                               const QString& avatarPath = QString());
void appendSystemMessage(ChatWidget* chatWidget, const QString& content);
void appendAssistantTextMessage(ChatWidget* chatWidget,
                                const QString& content,
                                const QString& senderId,
                                const QString& displayName,
                                const QString& avatarPath = QString());
bool appendSendFileToolResult(ChatWidget* chatWidget,
                              const ToolExecutionEvent& event,
                              const QString& senderId,
                              const QString& displayName,
                              const QString& avatarPath = QString(),
                              bool isMine = false);
void finalizeErrorUi(const std::function<void()>& resetUi,
                     const std::function<void()>& renderError,
                     const std::function<void()>& refreshUi,
                     ThinkingIndicatorWidget* indicator);
void completeStreamingResponse(ChatWidget* chatWidget,
                               StreamCompletionMode mode,
                               bool hadPending,
                               int pendingRow,
                               const QString& fullContent,
                               const QString& senderId,
                               const QString& displayName,
                               const QString& avatarPath = QString());
void beginToolPhase(const std::function<void()>& resetStreamUi,
                    const std::function<void()>& refreshUi,
                    ThinkingIndicatorWidget* indicator);
void beginReasoningPhase(ThinkingIndicatorWidget* indicator);
void showThinkingIndicator(ThinkingIndicatorWidget* indicator, bool active, const QString& text);
void restoreInputDraft(ChatWidget* chatWidget, const QString& text);
void hideThinkingIndicator(ThinkingIndicatorWidget* indicator);
void finalizeAbortUi(ChatWidget* chatWidget,
                     bool wasStreaming,
                     const QString& rolledBackUserMsg,
                     const std::function<void()>& showInterruptNotice,
                     const std::function<void()>& refreshUi,
                     ThinkingIndicatorWidget* indicator);
void finalizeUiUpdate(const std::function<void()>& refreshUi, ThinkingIndicatorWidget* indicator);

} // namespace ChatUiFlowSupport

#endif // CHATUIFLOWSUPPORT_H
