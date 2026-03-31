#ifndef IDENTITYVIEW_H
#define IDENTITYVIEW_H

#include "AppFacade.h"
#include "core/agent/ToolTypes.h"
#include <QColor>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QWidget>

class ChatListWidget;
class ChatWidget;
class Session;
class ThinkingIndicatorWidget;

/**
 * @brief 单个 Identity 视角的完整 UI
 *
 * 从旧单视图聊天窗口演进而来，包含两栏布局（会话列表 + 聊天区）。
 * 每个 IdentityView 对应一个 Identity（用户或 Agent），显示该 Identity 参与的会话。
 */
class IdentityView : public QWidget {
    Q_OBJECT
public:
    explicit IdentityView(const QString& identityId, IAppFacade& app, QWidget* parent = nullptr);

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
    /** 处理思考开始 */
    void handleReasoningStarted(const QString& sessionId);
    /** 处理思考结束 */
    void handleReasoningStopped(const QString& sessionId);

    /** 刷新会话列表 */
    void reloadSessionList();
    /** 标记会话列表过期（下次激活时按需刷新） */
    void markSessionListDirty();
    /** 同步后端发送状态到 UI（供外部事件路由调用） */
    void refreshSendingState();
    /** 刷新当前会话聊天内容（供外部事件路由调用） */
    void refreshSessionContent(const QString& sessionId);
    /** 刷新会话列表中的心跳状态 */
    void refreshSessionHeartbeatBadges();

signals:
    /** 请求打开模型配置管理 */
    void modelConfigImportRequested();
    /** 请求打开 MCP 配置 */
    void mcpConfigRequested();
    /** 请求打开工具日志 */
    void toolLogRequested();

private slots:
    void onNewChatRequested();
    void onChatItemActivated(const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount);
    void onChatItemRemoved(int row);
    void onChatItemRenamed(int row, const QString& name);
    void onUserMessageSent(const QString& content);
    void onAbortClicked();
    void onMessageActionRequested(const QString& action, const QString& messageId, const QString& content);
    void onRemoveCurrentChatRequested();
    void onAvatarClicked(const QString& sender, bool isMine, int row);
    void onVoiceStartRequested();
    void onVoiceStopRequested();

private:
    void showSessionInView(Session* session, bool deferHistoryRefresh = false);
    void clearCurrentSessionView();
    bool switchToSessionView(const QString& sessionId, bool deferHistoryRefresh = false);
    void setupUI();
    void syncInputAvailability();
    void updateSendingState();
    void setSendingState(bool isSending);
    void restoreChatFromSession(Session* session);
    void restoreChatFromHistory(const QJsonArray& history);
    void clearChatMessages();
    void resetStreamState();
    void applyUserSendingOverride();
    void selectSessionRow(int row);
    QString identityAvatarPath(const QString& identityId) const;
    QString streamAgentIdentityId(const QString& sessionId) const;
    QString sessionDisplayName(Session* session) const;
    QString sessionAvatarPath(Session* session) const;
    QString sessionHeartbeatAgentId(Session* session) const;
    void applyHeartbeatDecoration(Session* session, int row);

    // 行号 <-> Session ID 转换（基于过滤列表）
    QString sessionIdForRow(int row) const;
    int rowForSessionId(const QString& sessionId) const;
    void updateChatListItem(const QString& sessionId, const QString& preview);

    // 成员
    QString m_identityId;
    IAppFacade& m_app;
    IWorkspaceService* m_workspace = nullptr;
    IConversationService* m_conversation = nullptr;
    IGovernanceService* m_governance = nullptr;
    IMemoryService* m_memory = nullptr;
    bool m_isActive = false;

    // 过滤后的会话 ID 列表
    QStringList m_filteredSessionIds;

    // 当前选中的会话 UUID
    QString m_currentSessionId;

    // UI 组件
    ChatWidget* m_chatWidget = nullptr;
    ChatListWidget* m_chatListWidget = nullptr;
    ThinkingIndicatorWidget* m_thinkingIndicator = nullptr;

    // 本地流式渲染状态（每个 View 独立，不依赖共享的 Session::StreamState）
    bool m_hasPendingStreamMsg = false;
    int m_pendingStreamMsgRow = -1;

    // 列表缓存状态（减少 Tab 切换时全量刷新）
    bool m_sessionListLoaded = false;
    bool m_sessionListDirty = true;
};

#endif // IDENTITYVIEW_H

