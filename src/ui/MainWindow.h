#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/agent/ToolTypes.h"
#include <QHash>
#include <QStringList>
#include <QWidget>

class QStackedWidget;
class QTabWidget;
class QHBoxLayout;
class QVBoxLayout;
class QScrollArea;
class QToolButton;
class ChatService;
class IdentityView;
class ToolLogWidget;

/**
 * @brief 顶层主窗口——持有登录菜单 + StackedWidget，管理多个 IdentityView
 *
 * 替代 AgentChatWidget 作为主窗口。用户可在“登录”页通过头像按钮切换到不同 Identity 视角。
 */
class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onCreateAgentClicked();

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
    void removeAgentIdentityView(const QString& identityId);
    void switchToIdentity(const QString& identityId);
    void refreshLoginIdentityButtons();
    void refreshToolsTabButtonsState();
    void syncLoginIdentitySelection();

    IdentityView* ensureIdentityView(const QString& identityId);
    QList<IdentityView*> viewsForSession(const QString& sessionId) const;

    ChatService* m_chatService = nullptr;
    QTabWidget* m_menuTabs = nullptr;
    QWidget* m_loginTab = nullptr;
    QVBoxLayout* m_loginTabLayout = nullptr;
    QScrollArea* m_loginScrollArea = nullptr;
    QWidget* m_loginIdentityBar = nullptr;
    QHBoxLayout* m_loginIdentityLayout = nullptr;
    QWidget* m_toolsTab = nullptr;
    QVBoxLayout* m_toolsTabLayout = nullptr;
    QScrollArea* m_toolsScrollArea = nullptr;
    QWidget* m_toolsActionBar = nullptr;
    QHBoxLayout* m_toolsActionLayout = nullptr;
    QToolButton* m_modelImportBtn = nullptr;
    QToolButton* m_mcpConfigBtn = nullptr;
    QToolButton* m_toolLogBtn = nullptr;
    QStackedWidget* m_stackedWidget = nullptr;
    ToolLogWidget* m_toolLogWindow = nullptr;
    QStringList m_openAgentIds;
    QString m_activeIdentityId;

    QHash<QString, IdentityView*> m_views; // identityId -> IdentityView*
};

#endif // MAINWINDOW_H
