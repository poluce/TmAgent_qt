#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/agent/ToolTypes.h"
#include <QHash>
#include <QStringList>
#include <QWidget>

class ChatService;
class IdentityView;
class QHBoxLayout;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QVBoxLayout;
class QPlainTextEdit;
class ToolLogWidget;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onCreateAgentClicked();
    void onDeleteAgentClicked(const QString& agentIdentityId);

    // IdentityView 信号处理
    void onModelConfigImportClicked();
    void onMcpConfigClicked();
    void onToolLogClicked();
    void onInfoSettingsClicked();
    void onCommandPolicyClicked();
    void onMemoryStewardChanged(int index);

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
    void openMemorySettingsDialog();
    void reloadMemorySettingsUi();
    bool saveMemorySettingsUi(QString* error = nullptr);
    void setMenuTabsCollapsed(bool collapsed);
    void updateMenuTabsGeometry();

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
    QToolButton* m_menuCollapseBtn = nullptr;
    QToolButton* m_modelImportBtn = nullptr;
    QToolButton* m_mcpConfigBtn = nullptr;
    QToolButton* m_toolLogBtn = nullptr;
    QToolButton* m_infoSettingsBtn = nullptr;
    QToolButton* m_commandPolicyBtn = nullptr;
    QComboBox* m_memoryStewardCombo = nullptr;
    QComboBox* m_memoryStewardModelCombo = nullptr;
    QLineEdit* m_userPreferredNameEdit = nullptr;
    QLineEdit* m_userIdentityEdit = nullptr;
    QCheckBox* m_memoryAutoExtractCheck = nullptr;
    QSpinBox* m_memoryMinCharsSpin = nullptr;
    QSpinBox* m_memoryMaxCandidatesSpin = nullptr;
    QPushButton* m_memoryReindexBtn = nullptr;
    QPlainTextEdit* m_userGoalsEdit = nullptr;
    QPlainTextEdit* m_userPreferencesEdit = nullptr;
    QPlainTextEdit* m_companyCultureEdit = nullptr;
    QPlainTextEdit* m_userNotesEdit = nullptr;
    bool m_memoryUiLoading = false;
    QStackedWidget* m_stackedWidget = nullptr;
    ToolLogWidget* m_toolLogWindow = nullptr;
    QStringList m_openAgentIds;
    QString m_activeIdentityId;
    bool m_menuTabsCollapsed = false;
    int m_menuTabsExpandedMinHeight = 0;

    QHash<QString, IdentityView*> m_views; // identityId -> IdentityView*
};

#endif // MAINWINDOW_H
