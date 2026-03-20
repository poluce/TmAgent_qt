#ifndef AGENTCHATWIDGET_H
#define AGENTCHATWIDGET_H

#include "AppFacade.h"
#include "ExecutionHistoryModel.h"
#include "core/agent/ToolTypes.h"
#include <QColor>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class ChatListWidget;
class ChatWidget;
class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class Session;
class ToolLogWidget;
class ThinkingIndicatorWidget;

class AgentChatWidget : public QWidget {
    Q_OBJECT
public:
    explicit AgentChatWidget(IAppFacade& app, QWidget* parent = nullptr);

private slots:
    void onModelConfigImportClicked();
    void onMcpConfigClicked();
    void onNewChatRequested();
    void onChatItemActivated(const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount);
    void onChatItemRemoved(int row);
    void onChatItemRenamed(int row, const QString& name);
    void onUserMessageSent(const QString& content);
    void onAbortClicked();
    void onClearHistoryClicked();
    void onRemoveCurrentChatRequested();

    // ApplicationServices 信号处理
    void onServiceStreamData(const QString& sessionId, const QString& data);
    void onServiceFinished(const QString& sessionId, const QString& fullContent);
    void onServiceError(const QString& sessionId, const QString& errorMsg);
    void onServiceToolCallsStarted(const QString& sessionId);
    void onServiceToolEvent(const QString& sessionId, const ToolExecutionEvent& event);
    void onServiceReasoningStarted(const QString& sessionId);
    void onServiceReasoningStopped(const QString& sessionId);

    // ChatWidget 输入子组件信号（语音等）
    void onVoiceStartRequested();
    void onVoiceStopRequested();

    // 头像点击，弹出 Agent 信息卡片
    void onAvatarClicked(const QString& sender, bool isMine, int row);

private:
    void showSessionInView(Session* session);
    void clearCurrentSessionView();
    bool switchToSessionView(const QString& sessionId);
    void setupUI();
    void updateSendingState();
    void setSendingState(bool isSending);
    void restoreChatFromSession(Session* session);
    void restoreChatFromHistory(const QJsonArray& history);
    void clearChatMessages();
    void updateHistoryDisplay();
    void updateHistoryDisplayFrom(const QJsonArray& history);
    void refreshHistoryTree();
    void appendHistoryEntryNode(QTreeWidgetItem* parent, const ExecutionHistory::Record& record);

    // 行号 <-> Session ID 转换辅助
    QString sessionIdForRow(int row) const;
    int rowForSessionId(const QString& sessionId) const;
    void updateChatListItem(const QString& sessionId, const QString& preview);

    // 对话历史显示
    QTreeWidget* m_historyDisplay = nullptr;
    QPushButton* m_clearHistoryBtn = nullptr;
    QLabel* m_historyLabel = nullptr;
    QComboBox* m_historyFilterCombo = nullptr;
    QComboBox* m_historyRecentCombo = nullptr;
    QVector<ExecutionHistory::Record> m_historyRecords;
    QVector<int> m_visibleHistoryIndexes;

    IAppFacade& m_app;
    IWorkspaceService* m_workspace = nullptr;
    IConversationService* m_conversation = nullptr;
    IGovernanceService* m_governance = nullptr;
    AppEventHub* m_events = nullptr;

    // UI 组件
    ToolLogWidget* m_toolLogWidget = nullptr;
    ChatWidget* m_chatWidget = nullptr;
    ChatListWidget* m_chatListWidget = nullptr;
    QLabel* m_currentChatTitleLabel = nullptr;
    ThinkingIndicatorWidget* m_thinkingIndicator = nullptr;

    const QString m_botAvatarPath = QStringLiteral(":/icons/bot.png");
    // 当前选中的会话 UUID（替代 m_currentSessionRow）
    QString m_currentSessionId;

    // UI 显示模式（由 UI 自行管理，与 Agent 无关）
    bool m_isDebugMode = false;
};

#endif // AGENTCHATWIDGET_H

