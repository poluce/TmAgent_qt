#ifndef AGENTCHATWIDGET_H
#define AGENTCHATWIDGET_H

#include "core/agent/ToolTypes.h"
#include <QColor>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QWidget>

class ChatListWidget;
class ChatService;
class ChatWidget;
class QLabel;
class QPushButton;
class QTreeWidget;
class Session;
class ToolLogWidget;

class AgentChatWidget : public QWidget {
    Q_OBJECT
public:
    explicit AgentChatWidget(QWidget* parent = nullptr);

private slots:
    void onModelConfigImportClicked();
    void onMcpConfigClicked();
    void onNewChatRequested();
    void onChatItemActivated(const QString &name, const QString &message, const QString &time,
                             const QColor &avatarColor, int unreadCount);
    void onChatItemRemoved(int row);
    void onChatItemRenamed(int row, const QString &name);
    void onUserMessageSent(const QString& content);
    void onAbortClicked();
    void onClearHistoryClicked();
    void onRemoveCurrentChatRequested();

    // ChatService 信号处理
    void onServiceStreamData(const QString& sessionId, const QString& data);
    void onServiceFinished(const QString& sessionId, const QString& fullContent);
    void onServiceError(const QString& sessionId, const QString& errorMsg);
    void onServiceToolCallsStarted(const QString& sessionId);
    void onServiceToolEvent(const QString& sessionId, const ToolExecutionEvent& event);

    // ChatWidget 输入子组件信号（语音等）
    void onVoiceStartRequested();
    void onVoiceStopRequested();

    // 头像点击，弹出 Agent 信息卡片
    void onAvatarClicked(const QString& sender, bool isMine, int row);

private:
    void setupUI();
    void updateSendingState();
    void setSendingState(bool isSending);
    void restoreChatFromSession(Session* session);
    void restoreChatFromHistory(const QJsonArray& history);
    void clearChatMessages();
    void updateHistoryDisplay();
    void updateHistoryDisplayFrom(const QJsonArray& history);

    // 行号 <-> Session ID 转换辅助
    QString sessionIdForRow(int row) const;
    int rowForSessionId(const QString& sessionId) const;
    void updateChatListItem(const QString& sessionId, const QString& preview);

    // 对话历史显示
    QTreeWidget* m_historyDisplay = nullptr;
    QPushButton* m_clearHistoryBtn = nullptr;
    QLabel* m_historyLabel = nullptr;

    // 核心服务（所有业务逻辑委托给 ChatService）
    ChatService* m_chatService = nullptr;

    // UI 组件
    ToolLogWidget* m_toolLogWindow = nullptr;
    ChatWidget* m_chatWidget = nullptr;
    ChatListWidget* m_chatListWidget = nullptr;

    // 当前选中的会话 UUID（替代 m_currentSessionRow）
    QString m_currentSessionId;

    // UI 显示模式（由 UI 自行管理，与 Agent 无关）
    bool m_isDebugMode = false;
};

#endif // AGENTCHATWIDGET_H
