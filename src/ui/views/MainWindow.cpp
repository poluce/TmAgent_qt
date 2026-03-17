#include "MainWindow.h"
#include "AgentLifecycleSupport.h"
#include "AvatarUtils.h"
#include "CommandPolicyDialog.h"
#include "ConversationEventUiSupport.h"
#include "InformationSettingsDialog.h"
#include "IdentityView.h"
#include "McpConfigDialog.h"
#include "ModelConfigDialog.h"
#include "ToolLogUiSupport.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/Session.h"
#include "ChatService.h"
#include "core/tools/ShellTool.h"
#include <QCoreApplication>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <algorithm>

// ==================== 构造函数 ====================

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent)
{
    auto* service = new ChatService(this);
    service->initialize();
    m_caps = makeMainWindowCapabilities(service);

    // 注册 ShellTool 的命令确认回调（将 core 层的确认请求桥接到 UI 层）
    ShellTool::setConfirmCallback([](const QString& command, const QString& workDir) -> bool {
        return QMessageBox::question(
                   nullptr,
                   QObject::tr("执行确认"),
                   QObject::tr("Agent 请求执行以下命令：\n\n%1\n\n工作目录：%2\n\n是否允许执行？")
                       .arg(command, workDir),
                   QMessageBox::Yes | QMessageBox::No,
                   QMessageBox::No)
            == QMessageBox::Yes;
    });

    setupUI();
    setupConnections();
    restorePersistedSessions();

    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        if (m_caps.persistence) {
            m_caps.persistence->saveTabState(m_openAgentIds, m_activeIdentityId);
            m_caps.persistence->saveSessionsToDisk();
        }
    });
}

// ==================== setupUI ====================

void MainWindow::setupUI()
{
    setWindowTitle("TmAgent - Team of Agents");
    resize(1200, 700);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 一级功能分类（当前先提供"登录"页）
    m_menuTabs = new QTabWidget(this);
    m_menuTabs->setDocumentMode(true);
    if (QTabBar* bar = m_menuTabs->tabBar())
        bar->setDrawBase(false);
    m_menuCollapseBtn = new QToolButton(m_menuTabs);
    m_menuCollapseBtn->setAutoRaise(true);
    m_menuCollapseBtn->setText(QStringLiteral("▴"));
    m_menuCollapseBtn->setToolTip(tr("收起顶部栏"));
    m_menuCollapseBtn->setCursor(Qt::PointingHandCursor);
    m_menuCollapseBtn->setStyleSheet(
        "QToolButton { border: 1px solid #e5e7eb; border-radius: 8px; background: #ffffff; "
        "padding: 1px 7px; color: #374151; }"
        "QToolButton:hover { background: #f3f4f6; }"
        "QToolButton:pressed { background: #e5e7eb; }");
    connect(m_menuCollapseBtn, &QToolButton::clicked, this, [this]() {
        setMenuTabsCollapsed(!m_menuTabsCollapsed);
    });
    m_menuTabs->setCornerWidget(m_menuCollapseBtn, Qt::TopRightCorner);
    m_loginTab = new QWidget(m_menuTabs);
    m_loginTabLayout = new QVBoxLayout(m_loginTab);
    m_loginTabLayout->setContentsMargins(0, 0, 0, 0);
    m_loginTabLayout->setSpacing(0);

    m_loginScrollArea = new QScrollArea(m_loginTab);
    m_loginScrollArea->setFrameShape(QFrame::NoFrame);
    m_loginScrollArea->setWidgetResizable(true);
    m_loginScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_loginScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_loginIdentityBar = new QWidget(m_loginScrollArea);
    m_loginIdentityLayout = new QHBoxLayout(m_loginIdentityBar);
    m_loginIdentityLayout->setContentsMargins(8, 0, 0, 0);
    m_loginIdentityLayout->setSpacing(8);
    m_loginScrollArea->setWidget(m_loginIdentityBar);
    m_loginTabLayout->addWidget(m_loginScrollArea, 0);

    m_menuTabs->addTab(m_loginTab, tr("登录"));

    // 一级功能分类：工具页（从原左侧按钮迁移）
    m_toolsTab = new QWidget(m_menuTabs);
    m_toolsTabLayout = new QVBoxLayout(m_toolsTab);
    m_toolsTabLayout->setContentsMargins(0, 0, 0, 0);
    m_toolsTabLayout->setSpacing(0);

    m_toolsScrollArea = new QScrollArea(m_toolsTab);
    m_toolsScrollArea->setFrameShape(QFrame::NoFrame);
    m_toolsScrollArea->setWidgetResizable(true);
    m_toolsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_toolsScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_toolsActionBar = new QWidget(m_toolsScrollArea);
    m_toolsActionLayout = new QHBoxLayout(m_toolsActionBar);
    m_toolsActionLayout->setContentsMargins(8, 0, 0, 0);
    m_toolsActionLayout->setSpacing(8);

    constexpr int kMenuCardAvatarSide = 54;
    constexpr int kMenuCardRadius = 12;
    const QFontMetrics menuFm(font());
    const int menuTextLineHeight = menuFm.lineSpacing();
    const QSize menuIconSize(kMenuCardAvatarSide, kMenuCardAvatarSide);
    const int menuCardMinWidth = qMax(80, kMenuCardAvatarSide + 20);
    const int menuCardMinHeight = kMenuCardAvatarSide + menuTextLineHeight + 18;
    const int menuCardMaxHeight = menuCardMinHeight + 4;

    auto makeToolButton = [this, menuIconSize, menuCardMinWidth, menuCardMinHeight, menuCardMaxHeight](
                              const QString& text, const QString& tip, const QIcon& icon) {
        auto* btn = new QToolButton(m_toolsActionBar);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setAutoRaise(true);
        btn->setText(text);
        btn->setToolTip(tip);
        btn->setIcon(icon);
        btn->setIconSize(menuIconSize);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setMinimumWidth(menuCardMinWidth);
        btn->setMinimumHeight(menuCardMinHeight);
        btn->setMaximumHeight(menuCardMaxHeight);
        btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        btn->setStyleSheet(
            "QToolButton { border: 1px solid #e5e7eb; border-radius: 12px; padding: 6px 4px 6px 4px; "
            "background: #ffffff; color: #111827; }"
            "QToolButton:hover { background: #f8fafc; }"
            "QToolButton:pressed { background: #eff6ff; border-color: #93c5fd; }"
            "QToolButton:disabled { color: #9ca3af; border-color: #e5e7eb; background: #f9fafb; }");
        return btn;
    };

    m_modelImportBtn = makeToolButton(
        tr("导入模型"),
        tr("使用 DeepSeek / OpenAI / Claude / Ollama / Gemini 等预设填写 Base URL、API Key、模型"),
        AvatarUtils::makeGlyphIcon(QStringLiteral("导"), QColor(QStringLiteral("#60a5fa")), kMenuCardAvatarSide, kMenuCardRadius));
    m_mcpConfigBtn = makeToolButton(
        tr("配置 MCP"),
        tr("配置 MCP 工具服务（可选）"),
        AvatarUtils::makeGlyphIcon(QStringLiteral("M"), QColor(QStringLiteral("#34d399")), kMenuCardAvatarSide, kMenuCardRadius));
    m_toolLogBtn = makeToolButton(
        tr("工具日志"),
        tr("打开工具执行日志窗口"),
        AvatarUtils::makeGlyphIcon(QStringLiteral("志"), QColor(QStringLiteral("#f59e0b")), kMenuCardAvatarSide, kMenuCardRadius));
    m_infoSettingsBtn = makeToolButton(
        tr("信息设置"),
        tr("配置记忆管家、模型与用户信息"),
        AvatarUtils::makeGlyphIcon(QStringLiteral("设"), QColor(QStringLiteral("#a78bfa")), kMenuCardAvatarSide, kMenuCardRadius));
    m_commandPolicyBtn = makeToolButton(
        tr("命令权限"),
        tr("查看并编辑 execute_command 的白名单、黑名单与执行策略"),
        AvatarUtils::makeGlyphIcon(QStringLiteral("权"), QColor(QStringLiteral("#0ea5e9")), kMenuCardAvatarSide, kMenuCardRadius));

    connect(m_modelImportBtn, &QToolButton::clicked, this, &MainWindow::onModelConfigImportClicked);
    connect(m_mcpConfigBtn, &QToolButton::clicked, this, &MainWindow::onMcpConfigClicked);
    connect(m_toolLogBtn, &QToolButton::clicked, this, &MainWindow::onToolLogClicked);
    connect(m_infoSettingsBtn, &QToolButton::clicked, this, &MainWindow::onInfoSettingsClicked);
    connect(m_commandPolicyBtn, &QToolButton::clicked, this, &MainWindow::onCommandPolicyClicked);

    m_toolsActionLayout->addWidget(m_modelImportBtn);
    m_toolsActionLayout->addWidget(m_mcpConfigBtn);
    m_toolsActionLayout->addWidget(m_toolLogBtn);
    m_toolsActionLayout->addWidget(m_infoSettingsBtn);
    m_toolsActionLayout->addWidget(m_commandPolicyBtn);
    m_toolsActionLayout->addStretch(1);

    const QMargins toolsMargins = m_toolsActionLayout->contentsMargins();
    const int toolsBarHeight = menuCardMinHeight + toolsMargins.top() + toolsMargins.bottom();
    m_toolsActionBar->setMinimumHeight(toolsBarHeight);
    m_toolsActionBar->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);

    int toolsScrollHeight = toolsBarHeight + (m_toolsScrollArea->frameWidth() * 2);
    if (QScrollBar* hbar = m_toolsScrollArea->horizontalScrollBar())
        toolsScrollHeight += hbar->sizeHint().height();
    m_toolsScrollArea->setMinimumHeight(toolsScrollHeight);
    m_toolsScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    m_toolsScrollArea->setWidget(m_toolsActionBar);
    m_toolsTabLayout->addWidget(m_toolsScrollArea, 0);

    m_menuTabs->addTab(m_toolsTab, tr("设置"));
    mainLayout->addWidget(m_menuTabs);

    // 内容区
    m_stackedWidget = new QStackedWidget(this);
    mainLayout->addWidget(m_stackedWidget, 1);

    // 创建用户视角（固定）
    QString userId = IdentityManager::instance()->userIdentity()->id();
    auto* userView = new IdentityView(userId, m_caps.identityView, this);
    m_stackedWidget->addWidget(userView);
    m_views.insert(userId, userView);
    m_activeIdentityId = userId;
    connectViewSignals(userView);
    refreshLoginIdentityButtons();
    refreshToolsTabButtonsState();
    updateMenuTabsGeometry();
    QTimer::singleShot(0, this, [this]() { refreshLoginIdentityButtons(); });
}

void MainWindow::setupConnections()
{
    // ChatService 统一事件流路由（UI 与后端执行流程解耦）
    if (!m_caps.subscriptions)
        return;
    m_caps.subscriptions->subscribeConversationEvent(this, [this](const QJsonObject& event) {
        onConversationEvent(event);
    });
    m_caps.subscriptions->subscribeReasoningStarted(this, [this](const QString& sessionId) {
        onReasoningStarted(sessionId);
    });
    m_caps.subscriptions->subscribeReasoningStopped(this, [this](const QString& sessionId) {
        onReasoningStopped(sessionId);
    });
    m_caps.subscriptions->subscribeSessionCreated(this, [this](const QString& sessionId) {
        onSessionCreated(sessionId);
    });
    m_caps.subscriptions->subscribeSessionRemoved(this, [this](const QString& sessionId) {
        onSessionRemoved(sessionId);
    });
}

void MainWindow::connectViewSignals(IdentityView* view)
{
    connect(view, &IdentityView::modelConfigImportRequested, this, &MainWindow::onModelConfigImportClicked);
    connect(view, &IdentityView::mcpConfigRequested, this, &MainWindow::onMcpConfigClicked);
    connect(view, &IdentityView::toolLogRequested, this, &MainWindow::onToolLogClicked);
}

void MainWindow::refreshLoginIdentityButtons()
{
    if (!m_loginIdentityLayout)
        return;

    constexpr int kAlignedAvatarSide = 54;   // 与左侧会话列表一致
    constexpr int kAlignedAvatarRadius = 12; // 与左侧会话列表一致

    const QFontMetrics fm(font());
    const int textLineHeight = fm.lineSpacing();
    const int loginAvatarSide = kAlignedAvatarSide;
    const QSize loginAvatarSize(loginAvatarSide, loginAvatarSide);
    const int cardMinWidth = qMax(80, loginAvatarSide + 20);
    const int cardMinHeight = loginAvatarSide + textLineHeight + 18;
    const int cardMaxHeight = cardMinHeight + 4;

    IdentityManager* identityMgr = IdentityManager::instance();
    const QString userId = identityMgr->userIdentity()->id();

    QList<Identity*> agents = identityMgr->allAgents();
    agents.erase(std::remove_if(agents.begin(), agents.end(), [](Identity* identity) { return identity == nullptr; }), agents.end());
    std::sort(agents.begin(), agents.end(), [](Identity* a, Identity* b) {
        const int byName = a->name().localeAwareCompare(b->name());
        if (byName != 0)
            return byName < 0;
        return a->id() < b->id();
    });
    m_openAgentIds.clear();
    for (Identity* agent : agents)
        m_openAgentIds.append(agent->id());

    while (QLayoutItem* item = m_loginIdentityLayout->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    QStringList identities;
    identities.append(userId);
    identities.append(m_openAgentIds);

    int cardsHeightHint = 0;
    for (const QString& identityId : identities) {
        if (identityId.isEmpty())
            continue;

        Identity* identity = IdentityManager::instance()->findById(identityId);
        QString displayName;
        if (identity) {
            if (identity->isUser())
                displayName = tr("我");
            else
                displayName = identity->name().trimmed();
        }
        if (displayName.isEmpty())
            displayName = tr("未命名");

        QString buttonText = displayName;
        if (buttonText.size() > 5)
            buttonText = buttonText.left(5) + QStringLiteral("…");

        auto* button = new QToolButton(m_loginIdentityBar);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setCheckable(true);
        button->setAutoRaise(true);
        const QString avatarPath = identity ? identity->avatar().trimmed() : QString();
        button->setIcon(AvatarUtils::makeAvatarIcon(identityId, displayName, avatarPath, loginAvatarSide, kAlignedAvatarRadius));
        button->setIconSize(loginAvatarSize);
        button->setMinimumWidth(cardMinWidth);
        button->setMinimumHeight(cardMinHeight);
        button->setMaximumHeight(cardMaxHeight);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        button->setText(buttonText);
        button->setToolTip(displayName);
        button->setProperty("identityId", identityId);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(
            "QToolButton { border: 1px solid #e5e7eb; border-radius: 12px; padding: 6px 4px 6px 4px; "
            "background: #ffffff; color: #111827; }"
            "QToolButton:checked { border-color: #3b82f6; background: #eff6ff; }"
            "QToolButton:hover { background: #f8fafc; }");

        connect(button, &QToolButton::clicked, this, [this, identityId]() {
            switchToIdentity(identityId);
        });

        QWidget* cardWidget = button;
        if (identity && identity->isAgent()) {
            auto* card = new QWidget(m_loginIdentityBar);
            card->setMinimumWidth(cardMinWidth);
            card->setMinimumHeight(cardMinHeight);
            card->setMaximumHeight(cardMaxHeight);
            card->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

            auto* overlay = new QGridLayout(card);
            overlay->setContentsMargins(0, 0, 0, 0);
            overlay->setSpacing(0);
            button->setParent(card);
            overlay->addWidget(button, 0, 0);

            auto* removeBtn = new QToolButton(card);
            removeBtn->setText(QStringLiteral("×"));
            removeBtn->setToolTip(tr("删除助手"));
            removeBtn->setCursor(Qt::PointingHandCursor);
            removeBtn->setAutoRaise(true);
            removeBtn->setFixedSize(18, 18);
            removeBtn->setStyleSheet(
                "QToolButton { border: 1px solid #fecaca; border-radius: 9px; "
                "background: #ffffff; color: #dc2626; font-weight: 700; padding: 0px; }"
                "QToolButton:hover { background: #fef2f2; }"
                "QToolButton:pressed { background: #fee2e2; }");
            connect(removeBtn, &QToolButton::clicked, this, [this, identityId]() {
                onDeleteAgentClicked(identityId);
            });
            overlay->addWidget(removeBtn, 0, 0, Qt::AlignTop | Qt::AlignRight);
            removeBtn->raise();

            cardWidget = card;
        }

        m_loginIdentityLayout->addWidget(cardWidget);
        cardsHeightHint = qMax(cardsHeightHint, cardWidget->sizeHint().height());
    }

    auto* createButton = new QToolButton(m_loginIdentityBar);
    createButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    createButton->setAutoRaise(true);
    createButton->setText(QStringLiteral("+"));
    createButton->setMinimumWidth(cardMinWidth);
    createButton->setMinimumHeight(cardMinHeight);
    createButton->setMaximumHeight(cardMaxHeight);
    createButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    createButton->setToolTip(tr("创建 Agent"));
    createButton->setCursor(Qt::PointingHandCursor);
    QFont plusFont = createButton->font();
    plusFont.setPixelSize(qBound(20, loginAvatarSide / 2, 28));
    plusFont.setWeight(QFont::Medium);
    createButton->setFont(plusFont);
    createButton->setStyleSheet(
        "QToolButton { border: 1px dashed #cbd5e1; border-radius: 12px; "
        "background: #ffffff; color: #64748b; padding: 0px; }"
        "QToolButton:hover { background: #f8fafc; color: #334155; }"
        "QToolButton:pressed { background: #eef2ff; color: #1e3a8a; }");
    connect(createButton, &QToolButton::clicked, this, &MainWindow::onCreateAgentClicked);
    m_loginIdentityLayout->addWidget(createButton);
    cardsHeightHint = qMax(cardsHeightHint, createButton->sizeHint().height());

    m_loginIdentityLayout->addStretch(1);

    // 通过布局与 sizeHint驱动高度，只保留最小高度约束，避免固定尺寸裁剪。
    const QMargins barMargins = m_loginIdentityLayout->contentsMargins();
    const int barHeight = qMax(cardsHeightHint, cardMinHeight) + barMargins.top() + barMargins.bottom();
    m_loginIdentityBar->setMinimumHeight(barHeight);
    m_loginIdentityBar->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);

    if (m_loginScrollArea) {
        int scrollHeight = barHeight + (m_loginScrollArea->frameWidth() * 2);
        if (QScrollBar* hbar = m_loginScrollArea->horizontalScrollBar())
            scrollHeight += hbar->sizeHint().height();
        m_loginScrollArea->setMinimumHeight(scrollHeight);
        m_loginScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

        if (m_menuTabs && m_loginTabLayout) {
            const QMargins tabMargins = m_loginTabLayout->contentsMargins();
            const int pageHeight = scrollHeight + tabMargins.top() + tabMargins.bottom();
            const int tabHeaderHeight = m_menuTabs->tabBar()
                ? m_menuTabs->tabBar()->sizeHint().height()
                : 30;
            const int frameHeight = m_menuTabs->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, m_menuTabs) * 2;
            const int requiredTabsHeight = tabHeaderHeight + pageHeight + frameHeight;
            int requiredToolsHeight = requiredTabsHeight;
            if (m_toolsScrollArea && m_toolsTabLayout) {
                const QMargins toolsMargins = m_toolsTabLayout->contentsMargins();
                const int toolsPageHeight = m_toolsScrollArea->minimumHeight() + toolsMargins.top() + toolsMargins.bottom();
                requiredToolsHeight = tabHeaderHeight + toolsPageHeight + frameHeight;
            }
            m_menuTabsExpandedMinHeight = qMax(requiredTabsHeight, requiredToolsHeight);
            updateMenuTabsGeometry();
        }
    }

    syncLoginIdentitySelection();
}

void MainWindow::setMenuTabsCollapsed(bool collapsed)
{
    m_menuTabsCollapsed = collapsed;
    if (m_loginScrollArea)
        m_loginScrollArea->setVisible(!collapsed);
    if (m_toolsScrollArea)
        m_toolsScrollArea->setVisible(!collapsed);
    if (m_menuCollapseBtn) {
        m_menuCollapseBtn->setText(collapsed ? QStringLiteral("▾") : QStringLiteral("▴"));
        m_menuCollapseBtn->setToolTip(collapsed ? tr("展开顶部栏") : tr("收起顶部栏"));
    }
    updateMenuTabsGeometry();
}

void MainWindow::updateMenuTabsGeometry()
{
    if (!m_menuTabs)
        return;

    const int tabHeaderHeight = m_menuTabs->tabBar() ? m_menuTabs->tabBar()->sizeHint().height() : 30;
    const int frameHeight = m_menuTabs->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, m_menuTabs) * 2;

    if (m_menuTabsCollapsed) {
        const int collapsedHeight = tabHeaderHeight + frameHeight;
        m_menuTabs->setMinimumHeight(collapsedHeight);
        m_menuTabs->setMaximumHeight(collapsedHeight);
        m_menuTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return;
    }

    const int fallbackExpandedHeight = tabHeaderHeight + frameHeight + 64;
    const int expandedHeight = qMax(fallbackExpandedHeight, m_menuTabsExpandedMinHeight);
    m_menuTabs->setMaximumHeight(QWIDGETSIZE_MAX);
    m_menuTabs->setMinimumHeight(expandedHeight);
    m_menuTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
}

void MainWindow::syncLoginIdentitySelection()
{
    if (!m_loginIdentityLayout)
        return;

    auto syncCheckState = [this](QToolButton* button) {
        if (!button)
            return;
        const QString buttonIdentityId = button->property("identityId").toString().trimmed();
        if (buttonIdentityId.isEmpty())
            return;
        button->setChecked(buttonIdentityId == m_activeIdentityId);
    };

    for (int i = 0; i < m_loginIdentityLayout->count(); ++i) {
        QLayoutItem* item = m_loginIdentityLayout->itemAt(i);
        if (!item)
            continue;
        QWidget* widget = item->widget();
        if (!widget)
            continue;

        if (QToolButton* directButton = qobject_cast<QToolButton*>(widget))
            syncCheckState(directButton);

        const QList<QToolButton*> childButtons = widget->findChildren<QToolButton*>();
        for (QToolButton* child : childButtons)
            syncCheckState(child);
    }
}

void MainWindow::switchToIdentity(const QString& identityId)
{
    if (identityId.isEmpty())
        return;

    if (!m_views.contains(identityId)) {
        Identity* identity = IdentityManager::instance()->findById(identityId);
        if (!identity)
            return;
    }

    const QString userId = IdentityManager::instance()->userIdentity()->id();
    if (identityId != userId && !m_openAgentIds.contains(identityId))
        m_openAgentIds.append(identityId);

    // 停用所有视角
    for (auto* view : m_views)
        if (view->isActive())
            view->deactivate();

    // 激活新视角（懒加载）
    IdentityView* view = ensureIdentityView(identityId);
    if (view) {
        m_stackedWidget->setCurrentWidget(view);
        view->activate();
    }
    m_activeIdentityId = identityId;
    syncLoginIdentitySelection();
    refreshToolsTabButtonsState();
}

void MainWindow::refreshToolsTabButtonsState()
{
    const bool canManageGlobalConfig = m_caps.sessionQueries
        ? m_caps.sessionQueries->canIdentityManageGlobalConfig(m_activeIdentityId)
        : false;
    if (m_modelImportBtn)
        m_modelImportBtn->setEnabled(canManageGlobalConfig);
    if (m_mcpConfigBtn)
        m_mcpConfigBtn->setEnabled(canManageGlobalConfig);
    if (m_toolLogBtn)
        m_toolLogBtn->setEnabled(true);
    if (m_infoSettingsBtn)
        m_infoSettingsBtn->setEnabled(canManageGlobalConfig);
    if (m_commandPolicyBtn)
        m_commandPolicyBtn->setEnabled(canManageGlobalConfig);
}

IdentityView* MainWindow::ensureIdentityView(const QString& identityId)
{
    if (IdentityView* existing = m_views.value(identityId, nullptr))
        return existing;

    auto* view = new IdentityView(identityId, m_caps.identityView, this);
    m_stackedWidget->addWidget(view);
    m_views.insert(identityId, view);
    connectViewSignals(view);
    return view;
}

void MainWindow::removeAgentIdentityView(const QString& identityId)
{
    if (identityId.isEmpty())
        return;

    m_openAgentIds.removeAll(identityId);

    IdentityView* view = m_views.take(identityId);
    if (view) {
        if (m_stackedWidget)
            m_stackedWidget->removeWidget(view);
        view->deleteLater();
    }

    if (m_activeIdentityId == identityId)
        m_activeIdentityId = IdentityManager::instance()->userIdentity()->id();

    switchToIdentity(m_activeIdentityId);
    refreshLoginIdentityButtons();
}

// ==================== 创建 Agent ====================

void MainWindow::onCreateAgentClicked()
{
    const QString agentId = AgentLifecycleSupport::createAgentWithDialog(
        this,
        m_caps.agentLifecycle);
    if (agentId.isEmpty())
        return;
    if (!m_openAgentIds.contains(agentId))
        m_openAgentIds.append(agentId);
    refreshLoginIdentityButtons();
}

void MainWindow::onDeleteAgentClicked(const QString& agentIdentityId)
{
    const QString trimmedId = agentIdentityId.trimmed();
    if (trimmedId.isEmpty())
        return;
    if (!AgentLifecycleSupport::deleteAgentWithConfirmation(
            this,
            m_caps.agentLifecycle.sessionCommands,
            m_caps.agentLifecycle.memoryCommands,
            m_caps.agentLifecycle.persistence,
            trimmedId))
        return;
    removeAgentIdentityView(trimmedId);
    refreshLoginIdentityButtons();
}

// ==================== ChatService 信号路由 ====================

void MainWindow::onConversationEvent(const QJsonObject& event)
{
    const ConversationEventUiSupport::ParsedEvent parsed =
        ConversationEventUiSupport::parseConversationEvent(event);
    const QString& sessionId = parsed.sessionId;
    if (parsed.kind == ConversationEventUiSupport::EventKind::Ignore)
        return;

    switch (parsed.kind) {
    case ConversationEventUiSupport::EventKind::StreamDelta:
        onStreamData(sessionId, parsed.delta);
        return;
    case ConversationEventUiSupport::EventKind::TurnCompleted:
        onFinished(sessionId, parsed.content);
        return;
    case ConversationEventUiSupport::EventKind::TurnFailed:
        onError(sessionId, parsed.error);
        return;
    case ConversationEventUiSupport::EventKind::ToolCallsStarted:
        onToolCallsStarted(sessionId);
        return;
    case ConversationEventUiSupport::EventKind::ToolEvent:
        onToolEvent(sessionId, parsed.toolEvent);
        return;
    case ConversationEventUiSupport::EventKind::TurnCleared:
        onFinished(sessionId, QString());
        return;
    case ConversationEventUiSupport::EventKind::TurnRejected:
        onError(sessionId, parsed.displayError);
        return;
    case ConversationEventUiSupport::EventKind::MemoryNotice:
        if (!parsed.displayError.isEmpty()) {
            for (IdentityView* view : viewsForSession(sessionId))
                view->handleError(sessionId, parsed.displayError);
        }
        if (parsed.refreshHistory) {
            for (IdentityView* view : viewsForSession(sessionId))
                view->refreshHistoryForSession(sessionId);
        }
        return;
    case ConversationEventUiSupport::EventKind::SyncMessagesInjected:
        for (IdentityView* view : viewsForSession(sessionId))
            view->refreshHistoryForSession(sessionId);
        return;
    case ConversationEventUiSupport::EventKind::TurnStarted:
        for (IdentityView* view : viewsForSession(sessionId))
            view->refreshSendingState();
        return;
    case ConversationEventUiSupport::EventKind::Ignore:
        return;
    }
}

QList<IdentityView*> MainWindow::viewsForSession(const QString& sessionId) const
{
    QList<IdentityView*> result;
    Session* session = SessionManager::instance()->findById(sessionId);
    if (!session)
        return result;

    // 用户视角始终包含
    QString userId = IdentityManager::instance()->userIdentity()->id();
    if (IdentityView* uv = m_views.value(userId, nullptr))
        result.append(uv);

    // Agent 视角
    for (const QString& pid : session->participantIds()) {
        if (pid == userId)
            continue;
        if (IdentityView* view = m_views.value(pid, nullptr))
            result.append(view);
    }
    return result;
}

void MainWindow::onStreamData(const QString& sessionId, const QString& data)
{
    // 阶段 2 稳定化：仅可见视图实时渲染流式 delta，后台视图只依赖数据模型恢复。
    for (IdentityView* view : viewsForSession(sessionId)) {
        if (!view || !view->isActive())
            continue;
        view->handleStreamData(sessionId, data);
    }
}

void MainWindow::onFinished(const QString& sessionId, const QString& fullContent)
{
    // 每个 turn 完成只落盘一次，避免多视角重复触发重写导致卡顿。
    if (m_caps.persistence)
        m_caps.persistence->saveSessionsToDisk();

    for (IdentityView* view : viewsForSession(sessionId))
        view->handleFinished(sessionId, fullContent);
}

void MainWindow::onError(const QString& sessionId, const QString& errorMsg)
{
    for (IdentityView* view : viewsForSession(sessionId))
        view->handleError(sessionId, errorMsg);
}

void MainWindow::onToolCallsStarted(const QString& sessionId)
{
    for (IdentityView* view : viewsForSession(sessionId))
        view->handleToolCallsStarted(sessionId);
}

void MainWindow::onToolEvent(const QString& sessionId, const ToolExecutionEvent& event)
{
    for (IdentityView* view : viewsForSession(sessionId))
        view->handleToolEvent(sessionId, event);
}

void MainWindow::onReasoningStarted(const QString& sessionId)
{
    const QList<IdentityView*> targets = viewsForSession(sessionId);
    for (IdentityView* view : targets)
        view->handleReasoningStarted(sessionId);
}

void MainWindow::onReasoningStopped(const QString& sessionId)
{
    const QList<IdentityView*> targets = viewsForSession(sessionId);
    for (IdentityView* view : targets)
        view->handleReasoningStopped(sessionId);
}

void MainWindow::onSessionCreated(const QString& sessionId)
{
    Session* session = SessionManager::instance()->findById(sessionId);
    if (!session)
        return;

    // 找到该 Session 中的 Agent 参与者
    QString agentId;
    for (const QString& pid : session->participantIds()) {
        Identity* identity = IdentityManager::instance()->findById(pid);
        if (identity && identity->isAgent()) {
            agentId = pid;
            break;
        }
    }

    if (!agentId.isEmpty() && !m_openAgentIds.contains(agentId))
        m_openAgentIds.append(agentId);
    refreshLoginIdentityButtons();

    // 通知所有相关 IdentityView 刷新会话列表
    for (IdentityView* view : viewsForSession(sessionId)) {
        if (view->isActive()) {
            view->reloadSessionList();
        } else {
            view->markSessionListDirty();
        }
    }
}

void MainWindow::onSessionRemoved(const QString& sessionId)
{
    Q_UNUSED(sessionId);

    for (IdentityView* view : m_views) {
        if (!view)
            continue;
        if (view->isActive())
            view->reloadSessionList();
        else
            view->markSessionListDirty();
    }
    refreshLoginIdentityButtons();
}

// ==================== 恢复持久化 ====================

void MainWindow::restorePersistedSessions()
{
    const bool loadedFromDisk = m_caps.persistence
        ? m_caps.persistence->loadSessionsFromDisk()
        : false;

    const QString userId = IdentityManager::instance()->userIdentity()->id();
    QString targetActiveId = userId;

    if (loadedFromDisk) {
        ChatTabState tabState = m_caps.persistence
            ? m_caps.persistence->loadTabState()
            : ChatTabState {};
        const QString activeIdentityId = tabState.activeIdentityId.trimmed();
        if (!activeIdentityId.isEmpty()) {
            Identity* activeIdentity = IdentityManager::instance()->findById(activeIdentityId);
            if (activeIdentity && (activeIdentity->isUser() || activeIdentity->isAgent()))
                targetActiveId = activeIdentity->id();
        }
    }

    refreshLoginIdentityButtons();
    switchToIdentity(targetActiveId);
}

// ==================== 工具日志 ====================

void MainWindow::onInfoSettingsClicked()
{
    InformationSettingsDialog::show(
        this,
        m_caps.informationSettings,
        m_activeIdentityId);
}

void MainWindow::onCommandPolicyClicked()
{
    CommandPolicyDialog::show(this,
                              m_caps.commandPolicy.governanceCommands,
                              m_caps.commandPolicy.governanceQueries);
}

void MainWindow::onToolLogClicked()
{
    ToolLogUiSupport::showToolLogWindow(m_toolLogWindow, this);
}

// ==================== MCP 配置 ====================

void MainWindow::onMcpConfigClicked()
{
    McpConfigDialog::show(this, m_caps.mcpConfig);
}

// ==================== 模型配置导入 ====================

void MainWindow::onModelConfigImportClicked()
{
    ModelConfigDialog::show(this, m_caps.modelConfig);
}


