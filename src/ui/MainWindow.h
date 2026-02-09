#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/agent/ToolTypes.h"
#include <QHash>
#include <QWidget>

class QStackedWidget;
class QPushButton;
class ChatService;
class IdentityTabBar;
class IdentityView;
class ToolLogWidget;

/**
 * @brief 顶层主窗口——持有 TabBar + StackedWidget，管理多个 IdentityView
 *
 * 替代 AgentChatWidget 作为主窗口。用户可以通过顶部 Tab 切换到不同 Identity 的视角。
 */
class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onTabChanged(int index);
    void onCreateAgentClicked();
    void onAgentTabCloseRequested(int index, const QString& identityId);

    // IdentityView 信号处理
    void onModelConfigImportClicked();
    void onMcpConfigClicked();
    void onToolLogClicked();

    // ChatService 信号路由
    void onConversationEvent(const QJsonObject& event);
    void onStreamData(const QString& sessionId, const QString& data);
    void onFinished(const QString& sessionId, const QString& fullContent);
    void onError(const QString& sessionId, const QString& errorMsg);
    void onToolCallsStarted(const QString& sessionId);
    void onToolEvent(const QString& sessionId, const ToolExecutionEvent& event);
    void onSessionCreated(const QString& sessionId);
    void onSessionRemoved(const QString& sessionId);

private:
    void setupUI();
    void setupConnections();
    void restorePersistedSessions();
    void connectViewSignals(IdentityView* view);

    IdentityView* ensureIdentityView(const QString& identityId);
    QList<IdentityView*> viewsForSession(const QString& sessionId) const;

    ChatService* m_chatService = nullptr;
    IdentityTabBar* m_tabBar = nullptr;
    QStackedWidget* m_stackedWidget = nullptr;
    QPushButton* m_createAgentBtn = nullptr;
    ToolLogWidget* m_toolLogWindow = nullptr;

    QHash<QString, IdentityView*> m_views; // identityId -> IdentityView*
};

#endif // MAINWINDOW_H
