#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "AppFacade.h"
#include "core/agent/ToolTypes.h"
#include <QHash>
#include <QStringList>
#include <QWidget>

class IdentityView;
class QHBoxLayout;
class QScrollArea;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QVBoxLayout;
class ToolLogWidget;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(IAppFacade& app, QWidget* parent = nullptr);

private slots:
    void onCreateAgentClicked();
    void onDeleteAgentClicked(const QString& agentIdentityId);

    // IdentityView 信号处理
    void onModelConfigImportClicked();
    void onMcpConfigClicked();
    void onToolLogClicked();
    void onInfoSettingsClicked();
    void onCommandPolicyClicked();
    // 应用事件路由
    void onConversationEvent(const QJsonObject& event);
    void onStreamData(const QString& sessionId, const QString& data);
    void onFinished(const QString& sessionId, const QString& fullContent);
    void onError(const QString& sessionId, const QString& errorMsg);
    void onToolCallsStarted(const QString& sessionId);
    void onToolEvent(const QString& sessionId, const ToolExecutionEvent& event);
    void onReasoningStarted(const QString& sessionId);
    void onReasoningStopped(const QString& sessionId);
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
    void setMenuTabsCollapsed(bool collapsed);
    void updateMenuTabsGeometry();

    IdentityView* ensureIdentityView(const QString& identityId);
    QList<IdentityView*> viewsForSession(const QString& sessionId) const;

    IAppFacade& m_app;
    IWorkspaceService* m_workspace = nullptr;
    IConversationService* m_conversation = nullptr;
    IGovernanceService* m_governance = nullptr;
    IMemoryService* m_memory = nullptr;
    AppEventHub* m_events = nullptr;
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
    QToolButton* m_menuCollapseBtn = nullptr;
    QToolButton* m_modelImportBtn = nullptr;
    QToolButton* m_mcpConfigBtn = nullptr;
    QToolButton* m_toolLogBtn = nullptr;
    QToolButton* m_infoSettingsBtn = nullptr;
    QToolButton* m_commandPolicyBtn = nullptr;
    QStackedWidget* m_stackedWidget = nullptr;
    ToolLogWidget* m_toolLogWindow = nullptr;
    QStringList m_openAgentIds;
    QString m_activeIdentityId;
    bool m_menuTabsCollapsed = false;
    int m_menuTabsExpandedMinHeight = 0;

    QHash<QString, IdentityView*> m_views; // identityId -> IdentityView*
};

#endif // MAINWINDOW_H


