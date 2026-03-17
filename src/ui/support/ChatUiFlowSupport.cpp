#include "ChatUiFlowSupport.h"

#include "ThinkingIndicatorWidget.h"
#include "core/agent/ToolTypes.h"
#include "chat_widget.h"
#include "chat_widget_input.h"
#include <QTextEdit>

namespace ChatUiFlowSupport {

void activateConversationView(ChatWidget* chatWidget,
                              const std::function<void()>& restoreAction,
                              const std::function<void()>& finalizeAction)
{
    if (!chatWidget)
        return;
    chatWidget->setEmptyStateVisible(false);
    if (restoreAction)
        restoreAction();
    if (finalizeAction)
        finalizeAction();
}

void clearConversationView(ChatWidget* chatWidget,
                           const std::function<void()>& clearAction)
{
    if (clearAction)
        clearAction();
    if (chatWidget)
        chatWidget->setEmptyStateVisible(true);
}

void appendStreamingDelta(ChatWidget* chatWidget,
                          const QString& data,
                          const std::function<void()>& ensurePlaceholder)
{
    if (!chatWidget)
        return;
    if (ensurePlaceholder)
        ensurePlaceholder();
    chatWidget->streamOutput(data);
}

int appendStreamingPlaceholder(ChatWidget* chatWidget,
                               const QString& senderId,
                               const QString& displayName,
                               const QString& avatarPath)
{
    if (!chatWidget)
        return -1;

    ChatWidget::MessageParams params;
    params.content = QString();
    params.senderId = senderId;
    params.displayName = displayName;
    params.avatarPath = avatarPath;
    chatWidget->addMessage(params);
    const int row = chatWidget->messageCount() - 1;
    chatWidget->setStreamTargetRow(row);
    return row;
}

void appendSystemMessage(ChatWidget* chatWidget, const QString& content)
{
    if (!chatWidget || content.isEmpty())
        return;

    ChatWidget::MessageParams params;
    params.content = content;
    params.senderId = QStringLiteral("system");
    params.displayName = QStringLiteral("System");
    chatWidget->addMessage(params);
}

void appendAssistantTextMessage(ChatWidget* chatWidget,
                                const QString& content,
                                const QString& senderId,
                                const QString& displayName,
                                const QString& avatarPath)
{
    if (!chatWidget || content.isEmpty())
        return;

    ChatWidget::MessageParams params;
    params.content = content;
    params.senderId = senderId;
    params.displayName = displayName;
    params.avatarPath = avatarPath;
    chatWidget->addMessage(params);
}

bool appendSendFileToolResult(ChatWidget* chatWidget,
                              const ToolExecutionEvent& event,
                              const QString& senderId,
                              const QString& displayName,
                              const QString& avatarPath,
                              bool isMine)
{
    if (!chatWidget)
        return false;
    if (event.toolName != QLatin1String("send_file")
        || event.status != QLatin1String("completed")
        || !event.success
        || event.data.isEmpty()) {
        return false;
    }

    const QString filePath = event.data.value(QStringLiteral("file_path")).toString();
    const QString fileName = event.data.value(QStringLiteral("file_name")).toString();
    const qint64 fileSize = static_cast<qint64>(event.data.value(QStringLiteral("file_size")).toDouble());
    const QString description = event.data.value(QStringLiteral("description")).toString();
    if (filePath.isEmpty() || fileName.isEmpty())
        return false;

    ChatWidget::MessageParams params;
    params.content = description.isEmpty() ? fileName : description;
    params.senderId = senderId;
    params.displayName = displayName;
    params.avatarPath = avatarPath;
    params.isMine = isMine;
    chatWidget->addFileMessage(params, filePath, fileName, fileSize);
    return true;
}

void finalizeErrorUi(const std::function<void()>& resetUi,
                     const std::function<void()>& renderError,
                     const std::function<void()>& refreshUi,
                     ThinkingIndicatorWidget* indicator)
{
    if (resetUi)
        resetUi();
    if (renderError)
        renderError();
    finalizeUiUpdate(refreshUi, indicator);
}

void completeStreamingResponse(ChatWidget* chatWidget,
                               StreamCompletionMode mode,
                               bool hadPending,
                               int pendingRow,
                               const QString& fullContent,
                               const QString& senderId,
                               const QString& displayName,
                               const QString& avatarPath)
{
    if (!chatWidget)
        return;

    if (hadPending) {
        if (mode == StreamCompletionMode::UpdatePlaceholder) {
            if (fullContent.isEmpty())
                chatWidget->removeMessageAt(pendingRow);
            else
                chatWidget->updateMessageContentAtRow(pendingRow, fullContent);
            return;
        }
        chatWidget->removeLastMessage();
    }

    if (!fullContent.isEmpty())
        appendAssistantTextMessage(chatWidget, fullContent, senderId, displayName, avatarPath);
}

void showThinkingIndicator(ThinkingIndicatorWidget* indicator, bool active, const QString& text)
{
    if (active && indicator)
        indicator->showThinking(text);
}

void beginToolPhase(const std::function<void()>& resetStreamUi,
                    const std::function<void()>& refreshUi,
                    ThinkingIndicatorWidget* indicator)
{
    if (resetStreamUi)
        resetStreamUi();
    showThinkingIndicator(indicator, true, QStringLiteral("🔧 工具调用与反思中..."));
    if (refreshUi)
        refreshUi();
}

void beginReasoningPhase(ThinkingIndicatorWidget* indicator)
{
    showThinkingIndicator(indicator, true, QStringLiteral("💡 正在深度思考中..."));
}

void restoreInputDraft(ChatWidget* chatWidget, const QString& text)
{
    if (!chatWidget || text.isEmpty())
        return;

    if (auto* input = qobject_cast<ChatWidgetInput*>(chatWidget->inputWidget())) {
        if (auto* edit = input->findChild<QTextEdit*>(QStringLiteral("chatWidgetInputEdit"))) {
            edit->setPlainText(text);
            edit->setFocus();
        }
    }
}

void hideThinkingIndicator(ThinkingIndicatorWidget* indicator)
{
    if (indicator)
        indicator->hideIndicator();
}

void finalizeAbortUi(ChatWidget* chatWidget,
                     bool wasStreaming,
                     const QString& rolledBackUserMsg,
                     const std::function<void()>& showInterruptNotice,
                     const std::function<void()>& refreshUi,
                     ThinkingIndicatorWidget* indicator)
{
    if (chatWidget && wasStreaming) {
        if (showInterruptNotice)
            showInterruptNotice();
        restoreInputDraft(chatWidget, rolledBackUserMsg);
    }
    finalizeUiUpdate(refreshUi, indicator);
}

void finalizeUiUpdate(const std::function<void()>& refreshUi, ThinkingIndicatorWidget* indicator)
{
    if (refreshUi)
        refreshUi();
    hideThinkingIndicator(indicator);
}

} // namespace ChatUiFlowSupport
