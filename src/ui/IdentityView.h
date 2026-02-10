#ifndef IDENTITYVIEW_H
#define IDENTITYVIEW_H

#include "core/agent/ToolTypes.h"
#include <QColor>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

class ToolLogWidget;
class ChatWidget;
class ChatListWidget;
class ChatService;
class Session;

/**
 * @brief 单个 Identity 视角的完整 UI
 *
 * 从 AgentChatWidget 提取，包含三栏布局（会话列表 + 聊天区 + 历史面板）。
 * 每个 IdentityView 对应一个 Identity（用户或 Agent），显示该 Identity 参与的会话。
 */
class IdentityView : public QWidget {
    Q_OBJECT
public:
    explicit IdentityView(const QString& identityId,
                          ChatService* chatService,
                          QWidget* parent = nullptr);

    QString identityId() const { return m_identityId; }
    bool isUserView() const;
    bool isActive() const { return m_isActive; }
    QString currentSessionId() const { return m_currentSessionId; }

    /** 激活视角——切换到此 Tab 时调用 */
    void activate();
    /** 停用视角——离开此 Tab 时调用 */
    void deactivate();

    /** 处理流式数据（由 MainWindow 路由） */
    void handleStreamData(const QString& sessionId, const QString& data);
    /** 处理完成事件 */
    void handleFinished(const QString& sessionId, const QString& fullContent);
    /** 处理错误事件 */
    void handleError(const QString& sessionId, const QString& errorMsg);
    /** 处理工具调用开始 */
    void handleToolCallsStarted(const QString& sessionId);
    /** 处理工具事件 */
    void handleToolEvent(const QString& sessionId, const ToolExecutionEvent& event);

    /** 刷新会话列表 */
    void reloadSessionList();
    /** 标记会话列表过期（下次激活时按需刷新） */
    void markSessionListDirty();
    /** 同步后端发送状态到 UI（供外部事件路由调用） */
    void refreshSendingState();

signals:
    /** 请求打开模型配置导入 */
    void modelConfigImportRequested();
    /** 请求打开 MCP 配置 */
    void mcpConfigRequested();
    /** 请求打开工具日志 */
    void toolLogRequested();

private slots:
    void onNewChatRequested();
    void onChatItemActivated(const QString& name, const QString& message, const QString& time,
                             const QColor& avatarColor, int unreadCount);
    void onChatItemRemoved(int row);
    void onChatItemRenamed(int row, const QString& name);
    void onUserMessageSent(const QString& content);
    void onAbortClicked();
    void onClearHistoryClicked();
    void onRemoveCurrentChatRequested();
    void onAvatarClicked(const QString& sender, bool isMine, int row);
    void onVoiceStartRequested();
    void onVoiceStopRequested();

private:
    void setupUI();
    void syncInputAvailability();
    void updateSendingState();
    void setSendingState(bool isSending);
    void restoreChatFromSession(Session* session);
    void restoreChatFromHistory(const QJsonArray& history);
    void clearChatMessages();
    void updateHistoryDisplay();
    void updateHistoryDisplayFrom(const QJsonArray& history);
    QString identityAvatarPath(const QString& identityId) const;
    QString streamAgentIdentityId(const QString& sessionId) const;
    QString sessionDisplayName(Session* session) const;
    QString sessionAvatarPath(Session* session) const;

    // 行号 <-> Session ID 转换（基于过滤列表）
    QString sessionIdForRow(int row) const;
    int rowForSessionId(const QString& sessionId) const;
    void updateChatListItem(const QString& sessionId, const QString& preview);

    // 成员
    QString m_identityId;
    ChatService* m_chatService = nullptr;
    bool m_isActive = false;

    // 过滤后的会话 ID 列表
    QStringList m_filteredSessionIds;

    // 当前选中的会话 UUID
    QString m_currentSessionId;

    // 对话历史显示
    QTreeWidget* m_historyDisplay = nullptr;
    QPushButton* m_clearHistoryBtn = nullptr;
    QLabel* m_historyLabel = nullptr;

    // UI 组件
    ChatWidget* m_chatWidget = nullptr;
    ChatListWidget* m_chatListWidget = nullptr;

    // 本地流式渲染状态（每个 View 独立，不依赖共享的 Session::StreamState）
    bool m_hasPendingStreamMsg = false;
    int m_pendingStreamMsgRow = -1;

    // 列表缓存状态（减少 Tab 切换时全量刷新）
    bool m_sessionListLoaded = false;
    bool m_sessionListDirty = true;
};

#endif // IDENTITYVIEW_H
