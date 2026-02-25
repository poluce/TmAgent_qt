#include "MainWindow.h"
#include "AgentCreateDialog.h"
#include "AvatarUtils.h"
#include "IdentityView.h"
#include "ToolLogWidget.h"
#include "core/agent/ToolDispatcher.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "core/service/AgentRuntime.h"
#include "core/service/ChatService.h"
#include "core/tools/ShellTool.h"
#include "core/utils/DefaultPrompts.h"
#include "core/utils/KeychainHelper.h"
#include "core/utils/ModelConfigLoader.h"
#include "modelconfig/model_config_import_page.h"
#include "modelconfig/model_config_manager_page.h"
#include "llm/LLMTypes.h"
#include "llm/ModelFactory.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {

QString appDataRootPath()
{
    return QDir::home().filePath(QStringLiteral(".tmagent"));
}

QString memoryPolicyFilePath()
{
    return QDir(appDataRootPath()).filePath(QStringLiteral("config/memory_policy.json"));
}

QString userMemoryFilePath()
{
    return QDir(appDataRootPath()).filePath(QStringLiteral("user.md"));
}

QJsonObject readJsonFileObject(const QString& filePath, bool* ok = nullptr)
{
    if (ok)
        *ok = false;
    QFile file(filePath);
    if (!file.exists()) {
        if (ok)
            *ok = true;
        return QJsonObject();
    }
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QJsonObject();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    if (ok)
        *ok = true;
    return doc.object();
}

bool writeJsonFileObject(const QString& filePath, const QJsonObject& obj)
{
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath()))
        return false;
    QFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Text))
        return false;
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    const bool ok = (file.write(bytes) == bytes.size());
    file.close();
    return ok;
}

QHash<QString, QString> parseUserProfileFields(const QString& markdown)
{
    QHash<QString, QString> fields;
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    static const QRegularExpression lineRe(
        QStringLiteral("^\\s*-\\s*([A-Za-z0-9_]+)\\s*:\\s*(.*)\\s*$"));
    for (const QString& line : lines) {
        const QRegularExpressionMatch m = lineRe.match(line);
        if (!m.hasMatch())
            continue;
        const QString key = m.captured(1).trimmed();
        const QString value = m.captured(2).trimmed();
        if (!key.isEmpty())
            fields.insert(key, value);
    }
    return fields;
}

QString parseUserNotesSection(const QString& markdown)
{
    const QString marker = QStringLiteral("## User Notes");
    const int pos = markdown.indexOf(marker);
    if (pos < 0)
        return QString();
    QString notes = markdown.mid(pos + marker.size()).trimmed();
    if (notes.startsWith(QLatin1Char('\n')))
        notes.remove(0, 1);
    return notes.trimmed();
}

QString buildUserProfileMarkdown(const QHash<QString, QString>& fields, const QString& notes)
{
    auto val = [&fields](const QString& key) {
        return fields.value(key).trimmed();
    };

    QString text;
    text += QStringLiteral("# User Profile\n\n");
    text += QStringLiteral("- preferred_name: %1\n").arg(val(QStringLiteral("preferred_name")));
    text += QStringLiteral("- self_identity: %1\n").arg(val(QStringLiteral("self_identity")));
    text += QStringLiteral("- focus_goals: %1\n").arg(val(QStringLiteral("focus_goals")));
    text += QStringLiteral("- preference_traits: %1\n").arg(val(QStringLiteral("preference_traits")));
    text += QStringLiteral("- company_culture: %1\n").arg(val(QStringLiteral("company_culture")));
    text += QStringLiteral("- communication_style: %1\n").arg(val(QStringLiteral("communication_style")));
    text += QStringLiteral("- long_term_preferences: %1\n").arg(val(QStringLiteral("long_term_preferences")));
    text += QStringLiteral("\n## User Notes\n\n");
    text += notes.trimmed();
    text += QLatin1Char('\n');
    return text;
}
} // namespace

// ==================== 构造函数 ====================

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent)
{
    m_chatService = new ChatService(this);
    m_chatService->initialize();

    // 注册 ShellTool 的命令确认回调（将 core 层的确认请求桥接到 UI 层）
    ShellTool::setConfirmCallback([](const QString& command, const QString& workDir) -> bool {
        return QMessageBox::question(
            nullptr,
            QObject::tr("执行确认"),
            QObject::tr("Agent 请求执行以下命令：\n\n%1\n\n工作目录：%2\n\n是否允许执行？")
                .arg(command, workDir),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        ) == QMessageBox::Yes;
    });

    setupUI();
    setupConnections();
    restorePersistedSessions();

    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        m_chatService->saveTabState(m_openAgentIds, m_activeIdentityId);
        m_chatService->saveSessionsToDisk();
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

    // 一级功能分类（当前先提供“登录”页）
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
    auto* userView = new IdentityView(userId, m_chatService, this);
    m_stackedWidget->addWidget(userView);
    m_views.insert(userId, userView);
    m_activeIdentityId = userId;
    connectViewSignals(userView);
    refreshLoginIdentityButtons();
    refreshToolsTabButtonsState();
    updateMenuTabsGeometry();
    QTimer::singleShot(0, this, [this]() { refreshLoginIdentityButtons(); });
}

void MainWindow::openMemorySettingsDialog()
{
    if (!m_chatService)
        return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("信息设置"));
    dlg.setMinimumSize(700, 640);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("记忆与用户信息配置"), &dlg);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 1);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* desc = new QLabel(
        tr("在此配置记忆管家、模型和用户画像。记忆管家负责维护公共近期工作记忆。"),
        &dlg);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color: #4b5563;"));
    layout->addWidget(desc);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignTop);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

    m_memoryStewardCombo = new QComboBox(&dlg);
    m_memoryStewardCombo->setMinimumWidth(280);
    form->addRow(tr("记忆管家助手:"), m_memoryStewardCombo);

    m_memoryStewardModelCombo = new QComboBox(&dlg);
    m_memoryStewardModelCombo->setMinimumWidth(280);
    form->addRow(tr("管家使用模型:"), m_memoryStewardModelCombo);

    m_userPreferredNameEdit = new QLineEdit(&dlg);
    m_userPreferredNameEdit->setPlaceholderText(tr("例如：张总 / Alice"));
    form->addRow(tr("用户称呼:"), m_userPreferredNameEdit);

    m_userIdentityEdit = new QLineEdit(&dlg);
    m_userIdentityEdit->setPlaceholderText(tr("例如：创业公司负责人 / 技术团队主管"));
    form->addRow(tr("用户身份定位:"), m_userIdentityEdit);

    m_memoryAutoExtractCheck = new QCheckBox(tr("自动提炼长期记忆"), &dlg);
    m_memoryAutoExtractCheck->setToolTip(
        tr("开启后，系统会在每回合结束时根据规则自动写入 memory.md。"));
    form->addRow(tr("长期提炼开关:"), m_memoryAutoExtractCheck);

    m_memoryMinCharsSpin = new QSpinBox(&dlg);
    m_memoryMinCharsSpin->setRange(1, 4096);
    m_memoryMinCharsSpin->setSingleStep(1);
    m_memoryMinCharsSpin->setSuffix(tr(" 字"));
    m_memoryMinCharsSpin->setToolTip(
        tr("用户输入长度低于该值时，不执行自动长期提炼（手动“记住这条”不受影响）。"));
    form->addRow(tr("提炼最小长度:"), m_memoryMinCharsSpin);

    m_memoryMaxCandidatesSpin = new QSpinBox(&dlg);
    m_memoryMaxCandidatesSpin->setRange(1, 32);
    m_memoryMaxCandidatesSpin->setSingleStep(1);
    m_memoryMaxCandidatesSpin->setToolTip(
        tr("每回合最多写入的长期记忆候选条目数。"));
    form->addRow(tr("每回合提炼上限:"), m_memoryMaxCandidatesSpin);
    connect(m_memoryAutoExtractCheck, &QCheckBox::toggled, &dlg, [this](bool enabled) {
        if (m_memoryMinCharsSpin)
            m_memoryMinCharsSpin->setEnabled(enabled);
        if (m_memoryMaxCandidatesSpin)
            m_memoryMaxCandidatesSpin->setEnabled(enabled);
    });

    m_userGoalsEdit = new QPlainTextEdit(&dlg);
    m_userGoalsEdit->setPlaceholderText(tr("长期目标、阶段目标、近期要推进的事情"));
    m_userGoalsEdit->setMinimumHeight(80);
    form->addRow(tr("目标倾向:"), m_userGoalsEdit);

    m_userPreferencesEdit = new QPlainTextEdit(&dlg);
    m_userPreferencesEdit->setPlaceholderText(tr("沟通偏好、决策风格、输出格式偏好"));
    m_userPreferencesEdit->setMinimumHeight(80);
    form->addRow(tr("偏好倾向:"), m_userPreferencesEdit);

    m_companyCultureEdit = new QPlainTextEdit(&dlg);
    m_companyCultureEdit->setPlaceholderText(tr("企业文化、团队制度、做事原则"));
    m_companyCultureEdit->setMinimumHeight(90);
    form->addRow(tr("企业文化/制度:"), m_companyCultureEdit);

    m_userNotesEdit = new QPlainTextEdit(&dlg);
    m_userNotesEdit->setPlaceholderText(tr("可选补充说明（也可手动编辑 ~/.tmagent/user.md）"));
    m_userNotesEdit->setMinimumHeight(120);
    form->addRow(tr("补充说明:"), m_userNotesEdit);

    layout->addLayout(form, 1);

    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(8);
    m_memoryReindexBtn = new QPushButton(tr("重建记忆索引"), &dlg);
    m_memoryReindexBtn->setToolTip(tr("立即重建所有助手的记忆索引（SQLite FTS）。"));
    actionRow->addWidget(m_memoryReindexBtn);
    actionRow->addStretch(1);
    layout->addLayout(actionRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    QPushButton* saveBtn = buttons->button(QDialogButtonBox::Save);
    QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    if (saveBtn)
        saveBtn->setText(tr("保存"));
    if (cancelBtn)
        cancelBtn->setText(tr("取消"));
    layout->addWidget(buttons);

    connect(m_memoryStewardCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onMemoryStewardChanged);
    connect(m_memoryReindexBtn, &QPushButton::clicked, this, [this]() {
        if (!m_chatService)
            return;
        const QString userId = IdentityManager::instance()->userIdentity()->id();
        QJsonObject rebuildResult;
        QString rebuildError;
        const bool ok = m_chatService->rebuildMemoryIndexAs(userId, QString(), &rebuildResult, &rebuildError);

        const int total = rebuildResult.value(QStringLiteral("agents_total")).toInt();
        const int success = rebuildResult.value(QStringLiteral("agents_success")).toInt();
        const int failed = rebuildResult.value(QStringLiteral("agents_failed")).toInt();
        const int rows = rebuildResult.value(QStringLiteral("rows_indexed")).toInt();
        QString summary = tr("助手总数: %1\n成功: %2\n失败: %3\n索引行数: %4")
                              .arg(total)
                              .arg(success)
                              .arg(failed)
                              .arg(rows);
        if (!rebuildError.trimmed().isEmpty())
            summary += QStringLiteral("\n\n") + rebuildError.trimmed();

        if (ok)
            QMessageBox::information(this, tr("索引重建完成"), summary);
        else
            QMessageBox::warning(this, tr("索引重建部分失败"), summary);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, [this, &dlg]() {
        QString err;
        if (!saveMemorySettingsUi(&err)) {
            QMessageBox::warning(this, tr("保存失败"), err.isEmpty() ? tr("信息设置保存失败。") : err);
            return;
        }
        QMessageBox::information(this, tr("保存成功"), tr("信息设置已更新。"));
        dlg.accept();
    });

    reloadMemorySettingsUi();
    refreshToolsTabButtonsState();
    dlg.exec();

    m_memoryStewardCombo = nullptr;
    m_memoryStewardModelCombo = nullptr;
    m_userPreferredNameEdit = nullptr;
    m_userIdentityEdit = nullptr;
    m_memoryAutoExtractCheck = nullptr;
    m_memoryMinCharsSpin = nullptr;
    m_memoryMaxCandidatesSpin = nullptr;
    m_memoryReindexBtn = nullptr;
    m_userGoalsEdit = nullptr;
    m_userPreferencesEdit = nullptr;
    m_companyCultureEdit = nullptr;
    m_userNotesEdit = nullptr;
    m_memoryUiLoading = false;
}

void MainWindow::reloadMemorySettingsUi()
{
    if (!m_memoryStewardCombo || !m_memoryStewardModelCombo)
        return;

    m_memoryUiLoading = true;

    const QString previousStewardId = m_memoryStewardCombo->currentData().toString().trimmed();
    m_memoryStewardCombo->clear();
    m_memoryStewardCombo->addItem(tr("(不设置)"), QString());
    const QList<Identity*> agents = IdentityManager::instance()->allAgents();
    for (Identity* agent : agents) {
        if (!agent || !agent->isAgent())
            continue;
        const QString name = agent->name().trimmed().isEmpty()
            ? tr("未命名助手")
            : agent->name().trimmed();
        m_memoryStewardCombo->addItem(QStringLiteral("%1 (%2)").arg(name, agent->id()), agent->id());
    }

    bool policyOk = false;
    const QJsonObject policyObj = readJsonFileObject(memoryPolicyFilePath(), &policyOk);
    const QString policyStewardId = policyObj.value(QStringLiteral("memory_steward_agent_id")).toString().trimmed();
    QString stewardId = policyStewardId.isEmpty() ? previousStewardId : policyStewardId;
    int stewardIndex = m_memoryStewardCombo->findData(stewardId);
    if (stewardIndex < 0)
        stewardIndex = 0;
    m_memoryStewardCombo->setCurrentIndex(stewardIndex);

    const QJsonObject memoryRulesObj = policyObj.value(QStringLiteral("memory_rules")).toObject();
    const bool autoExtractEnabled = memoryRulesObj.value(QStringLiteral("auto_extract_enabled")).toBool(true);
    const int minUserCharsForExtract = qBound(
        1,
        memoryRulesObj.value(QStringLiteral("min_user_chars_for_extract")).toInt(12),
        4096);
    const int maxLongMemoryCandidates = qBound(
        1,
        memoryRulesObj.value(QStringLiteral("max_long_memory_candidates_per_turn")).toInt(3),
        32);
    if (m_memoryAutoExtractCheck)
        m_memoryAutoExtractCheck->setChecked(autoExtractEnabled);
    if (m_memoryMinCharsSpin)
        m_memoryMinCharsSpin->setValue(minUserCharsForExtract);
    if (m_memoryMaxCandidatesSpin)
        m_memoryMaxCandidatesSpin->setValue(maxLongMemoryCandidates);
    if (m_memoryMinCharsSpin)
        m_memoryMinCharsSpin->setEnabled(autoExtractEnabled);
    if (m_memoryMaxCandidatesSpin)
        m_memoryMaxCandidatesSpin->setEnabled(autoExtractEnabled);

    const QStringList configIds = m_chatService && m_chatService->modelFactory()
        ? m_chatService->modelFactory()->registeredConfigIds()
        : QStringList();
    m_memoryStewardModelCombo->clear();
    for (const QString& cid : configIds)
        m_memoryStewardModelCombo->addItem(cid, cid);

    const QString selectedStewardId = m_memoryStewardCombo->currentData().toString().trimmed();
    QString stewardConfigId;
    if (!selectedStewardId.isEmpty()) {
        Identity* steward = IdentityManager::instance()->findById(selectedStewardId);
        if (steward && steward->profile()) {
            const LLMConfig cfg = steward->profile()->llmConfig();
            stewardConfigId = ModelFactory::resolveConfigKey(cfg);
        }
    }
    if (stewardConfigId.isEmpty() && m_chatService)
        stewardConfigId = ModelFactory::resolveConfigKey(m_chatService->defaultAgentConfig());
    int modelIndex = m_memoryStewardModelCombo->findData(stewardConfigId);
    if (modelIndex < 0 && m_memoryStewardModelCombo->count() > 0)
        modelIndex = 0;
    if (modelIndex >= 0)
        m_memoryStewardModelCombo->setCurrentIndex(modelIndex);

    QFile userFile(userMemoryFilePath());
    QString userMd;
    if (userFile.open(QFile::ReadOnly | QFile::Text)) {
        userMd = QString::fromUtf8(userFile.readAll());
        userFile.close();
    }
    const QHash<QString, QString> fields = parseUserProfileFields(userMd);
    m_userPreferredNameEdit->setText(fields.value(QStringLiteral("preferred_name")));
    m_userIdentityEdit->setText(fields.value(QStringLiteral("self_identity")));
    m_userGoalsEdit->setPlainText(fields.value(QStringLiteral("focus_goals")));
    m_userPreferencesEdit->setPlainText(fields.value(QStringLiteral("preference_traits")));
    m_companyCultureEdit->setPlainText(fields.value(QStringLiteral("company_culture")));
    m_userNotesEdit->setPlainText(parseUserNotesSection(userMd));

    m_memoryUiLoading = false;
}

bool MainWindow::saveMemorySettingsUi(QString* error)
{
    if (error)
        error->clear();
    if (!m_chatService)
        return false;

    const QString stewardId = m_memoryStewardCombo
        ? m_memoryStewardCombo->currentData().toString().trimmed()
        : QString();
    const QString stewardModelId = m_memoryStewardModelCombo
        ? m_memoryStewardModelCombo->currentData().toString().trimmed()
        : QString();
    const bool autoExtractEnabled = m_memoryAutoExtractCheck
        ? m_memoryAutoExtractCheck->isChecked()
        : true;
    const int minUserCharsForExtract = m_memoryMinCharsSpin
        ? m_memoryMinCharsSpin->value()
        : 12;
    const int maxLongMemoryCandidates = m_memoryMaxCandidatesSpin
        ? m_memoryMaxCandidatesSpin->value()
        : 3;

    QJsonObject policyObj = readJsonFileObject(memoryPolicyFilePath(), nullptr);
    policyObj.insert(QStringLiteral("memory_steward_agent_id"), stewardId);
    QJsonObject memoryRulesObj = policyObj.value(QStringLiteral("memory_rules")).toObject();
    memoryRulesObj.insert(QStringLiteral("auto_extract_enabled"), autoExtractEnabled);
    memoryRulesObj.insert(QStringLiteral("min_user_chars_for_extract"), minUserCharsForExtract);
    memoryRulesObj.insert(QStringLiteral("max_long_memory_candidates_per_turn"), maxLongMemoryCandidates);
    policyObj.insert(QStringLiteral("memory_rules"), memoryRulesObj);
    if (!writeJsonFileObject(memoryPolicyFilePath(), policyObj)) {
        if (error)
            *error = tr("写入 memory_policy.json 失败");
        return false;
    }

    if (!stewardId.isEmpty() && !stewardModelId.isEmpty()) {
        Identity* steward = IdentityManager::instance()->findById(stewardId);
        if (steward && steward->isAgent() && steward->profile()) {
            LLMConfig cfg = steward->profile()->llmConfig();
            cfg.configId = stewardModelId;
            steward->profile()->setLlmConfig(cfg);
        }
    }

    QHash<QString, QString> fields;
    fields.insert(QStringLiteral("preferred_name"), m_userPreferredNameEdit ? m_userPreferredNameEdit->text() : QString());
    fields.insert(QStringLiteral("self_identity"), m_userIdentityEdit ? m_userIdentityEdit->text() : QString());
    fields.insert(QStringLiteral("focus_goals"), m_userGoalsEdit ? m_userGoalsEdit->toPlainText() : QString());
    fields.insert(QStringLiteral("preference_traits"), m_userPreferencesEdit ? m_userPreferencesEdit->toPlainText() : QString());
    fields.insert(QStringLiteral("company_culture"), m_companyCultureEdit ? m_companyCultureEdit->toPlainText() : QString());
    fields.insert(QStringLiteral("communication_style"), fields.value(QStringLiteral("preference_traits")));
    fields.insert(QStringLiteral("long_term_preferences"), fields.value(QStringLiteral("focus_goals")));
    const QString notes = m_userNotesEdit ? m_userNotesEdit->toPlainText() : QString();
    const QString userMarkdown = buildUserProfileMarkdown(fields, notes);
    const QString userPath = userMemoryFilePath();
    if (!QDir().mkpath(QFileInfo(userPath).absolutePath())) {
        if (error)
            *error = tr("创建用户记忆目录失败");
        return false;
    }
    QFile file(userPath);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        if (error)
            *error = tr("写入 user.md 失败");
        return false;
    }
    const QByteArray bytes = userMarkdown.toUtf8();
    if (file.write(bytes) != bytes.size()) {
        file.close();
        if (error)
            *error = tr("写入 user.md 内容失败");
        return false;
    }
    file.close();

    m_chatService->applyConfigToAllRuntimes();
    m_chatService->saveSessionsToDisk();
    return true;
}

// ==================== setupConnections ====================

void MainWindow::setupConnections()
{
    // ChatService 统一事件流路由（UI 与后端执行流程解耦）
    connect(m_chatService, &ChatService::conversationEvent, this, &MainWindow::onConversationEvent);
    connect(m_chatService, &ChatService::sessionCreated, this, &MainWindow::onSessionCreated);
    connect(m_chatService, &ChatService::sessionRemoved, this, &MainWindow::onSessionRemoved);
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

    // 通过布局与 sizeHint 驱动高度，只保留最小高度约束，避免固定尺寸裁剪。
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
    reloadMemorySettingsUi();
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
    if (!m_chatService)
        return;

    const bool canManageGlobalConfig = m_chatService->canIdentityManageGlobalConfig(m_activeIdentityId);
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
    if (m_memoryStewardCombo)
        m_memoryStewardCombo->setEnabled(canManageGlobalConfig);
    if (m_memoryStewardModelCombo)
        m_memoryStewardModelCombo->setEnabled(canManageGlobalConfig);
    if (m_userPreferredNameEdit)
        m_userPreferredNameEdit->setEnabled(canManageGlobalConfig);
    if (m_userIdentityEdit)
        m_userIdentityEdit->setEnabled(canManageGlobalConfig);
    if (m_memoryAutoExtractCheck)
        m_memoryAutoExtractCheck->setEnabled(canManageGlobalConfig);
    if (m_memoryMinCharsSpin)
        m_memoryMinCharsSpin->setEnabled(
            canManageGlobalConfig
            && (!m_memoryAutoExtractCheck || m_memoryAutoExtractCheck->isChecked()));
    if (m_memoryMaxCandidatesSpin)
        m_memoryMaxCandidatesSpin->setEnabled(
            canManageGlobalConfig
            && (!m_memoryAutoExtractCheck || m_memoryAutoExtractCheck->isChecked()));
    if (m_memoryReindexBtn)
        m_memoryReindexBtn->setEnabled(canManageGlobalConfig);
    if (m_userGoalsEdit)
        m_userGoalsEdit->setEnabled(canManageGlobalConfig);
    if (m_userPreferencesEdit)
        m_userPreferencesEdit->setEnabled(canManageGlobalConfig);
    if (m_companyCultureEdit)
        m_companyCultureEdit->setEnabled(canManageGlobalConfig);
    if (m_userNotesEdit)
        m_userNotesEdit->setEnabled(canManageGlobalConfig);
}

IdentityView* MainWindow::ensureIdentityView(const QString& identityId)
{
    if (IdentityView* existing = m_views.value(identityId, nullptr))
        return existing;

    auto* view = new IdentityView(identityId, m_chatService, this);
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
    QStringList configIds;
    ModelFactory* factory = m_chatService->modelFactory();
    if (factory)
        configIds = factory->registeredConfigIds();
    const LLMConfig defaultAgentCfg = m_chatService->defaultAgentConfig();
    const QString defaultConfigId = ModelFactory::resolveConfigKey(defaultAgentCfg);

    AgentCreateDialog dlg(configIds, defaultConfigId, this);

    // 填充新路径的接入点列表
    if (factory) {
        QList<AgentCreateDialog::ProviderEntry> providerEntries;
        const QStringList instanceIds = factory->enabledInstanceIds();
        for (const QString& instId : instanceIds) {
            AgentCreateDialog::ProviderEntry entry;
            entry.instanceId = instId;
            entry.displayName = factory->displayNameForInstance(instId);
            providerEntries.append(entry);
        }
        const QString defaultInstId = ModelFactory::resolveInstanceId(defaultAgentCfg);
        dlg.setProviderEntries(providerEntries, defaultInstId);

        // 连接：接入点切换时异步拉取模型列表
        connect(&dlg, &AgentCreateDialog::providerChanged, this, [factory](const QString& instId) {
            if (!instId.isEmpty())
                factory->fetchModelsAsync(instId);
        });

        // 连接：模型缓存更新时刷新 dialog 的模型下拉
        connect(factory, &ModelFactory::modelCacheUpdated, &dlg, [&dlg, factory, &defaultAgentCfg](const QString& instId) {
            const QList<AvailableModel> cached = factory->cachedModels(instId);
            QList<AgentCreateDialog::ModelEntry> modelEntries;
            for (const AvailableModel& am : cached) {
                AgentCreateDialog::ModelEntry me;
                me.modelId = am.modelId;
                me.displayName = am.displayName;
                modelEntries.append(me);
            }
            dlg.setModelEntries(instId, modelEntries, defaultAgentCfg.selectedModelId);
        });

        // 为每个接入点填充已缓存的模型列表，并触发异步拉取
        for (const QString& instId : instanceIds) {
            const QList<AvailableModel> cached = factory->cachedModels(instId);
            QList<AgentCreateDialog::ModelEntry> modelEntries;
            for (const AvailableModel& am : cached) {
                AgentCreateDialog::ModelEntry me;
                me.modelId = am.modelId;
                me.displayName = am.displayName;
                modelEntries.append(me);
            }
            dlg.setModelEntries(instId, modelEntries, defaultAgentCfg.selectedModelId);
        }

        // 对当前选中的接入点立即触发一次拉取
        const QString currentInstId = dlg.providerInstanceId();
        if (!currentInstId.isEmpty())
            factory->fetchModelsAsync(currentInstId);
    }

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString name = dlg.agentName();
    const QString prompt = dlg.systemPrompt();
    const QString roleName = dlg.roleName();
    const QString avatarPath = dlg.avatarPath();
    const QString selectedConfigId = dlg.configId();
    const QString selectedInstanceId = dlg.providerInstanceId();
    const QString selectedModelId = dlg.selectedModelId();
    const bool delegationEnabled = dlg.delegationEnabled();

    // 创建 Agent Identity
    auto* profile = new IdentityProfile();
    LLMConfig agentCfg = defaultAgentCfg;
    if (!selectedInstanceId.isEmpty()) {
        agentCfg.providerInstanceId = selectedInstanceId;
        agentCfg.configId = selectedInstanceId; // 保持兼容
    } else if (!selectedConfigId.isEmpty()) {
        agentCfg.configId = selectedConfigId;
    }
    if (!selectedModelId.isEmpty())
        agentCfg.selectedModelId = selectedModelId;
    if (!prompt.isEmpty())
        agentCfg.systemPrompt = prompt;
    profile->setLlmConfig(agentCfg);
    if (!prompt.isEmpty())
        profile->setSystemPrompt(prompt);
    if (!roleName.isEmpty())
        profile->setDescription(roleName);
    profile->setDelegateEnabled(delegationEnabled);
    if (ToolDispatcher* dispatcher = m_chatService->toolDispatcher()) {
        QStringList toolNames;
        const QList<Tool> tools = dispatcher->getAllToolSchemas();
        for (const Tool& tool : tools) {
            const QString name = tool.name.trimmed();
            if (!name.isEmpty())
                toolNames.append(name);
        }
        if (delegationEnabled && !toolNames.contains(QStringLiteral("delegate_task")))
            toolNames.append(QStringLiteral("delegate_task"));
        if (!delegationEnabled)
            toolNames.removeAll(QStringLiteral("delegate_task"));
        toolNames.removeDuplicates();
        profile->setAllowedTools(toolNames);
    }
    Identity* agent = IdentityManager::instance()->createAgent(name, profile);
    if (!avatarPath.isEmpty())
        agent->setAvatar(avatarPath);

    // 创建初始 Session
    m_chatService->createSessionForIdentity(agent->id(), name);

    if (!m_openAgentIds.contains(agent->id()))
        m_openAgentIds.append(agent->id());
    refreshLoginIdentityButtons();
}

void MainWindow::onDeleteAgentClicked(const QString& agentIdentityId)
{
    const QString trimmedId = agentIdentityId.trimmed();
    if (trimmedId.isEmpty())
        return;

    IdentityManager* identityMgr = IdentityManager::instance();
    Identity* agent = identityMgr ? identityMgr->findById(trimmedId) : nullptr;
    if (!agent || !agent->isAgent())
        return;

    const QString agentName = agent->name().trimmed().isEmpty()
        ? tr("未命名助手")
        : agent->name().trimmed();
    const QList<Session*> agentSessions = SessionManager::instance()->sessionsForIdentity(trimmedId);

    const QMessageBox::StandardButton confirm = QMessageBox::warning(
        this,
        tr("删除助手"),
        tr("将删除助手“%1”，并删除其相关会话（%2 个）。\n此操作不可恢复，是否继续？")
            .arg(agentName)
            .arg(agentSessions.size()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (confirm != QMessageBox::Yes)
        return;

    const QString userId = identityMgr->userIdentity()->id();
    QStringList sessionIds;
    for (Session* session : agentSessions) {
        if (session)
            sessionIds.append(session->id());
    }
    sessionIds.removeDuplicates();
    for (const QString& sessionId : sessionIds)
        m_chatService->removeSessionAs(userId, sessionId);

    if (!m_chatService->removeAgentMemoryAs(userId, trimmedId)) {
        QMessageBox::warning(
            this,
            tr("删除助手失败"),
            tr("助手“%1”的记忆目录删除失败，已中止删除操作。请检查目录权限后重试。")
                .arg(agentName));
        return;
    }

    if (!identityMgr->removeAgent(trimmedId))
        return;
    removeAgentIdentityView(trimmedId);

    m_chatService->saveSessionsToDisk();
    refreshLoginIdentityButtons();
}

// ==================== ChatService 信号路由 ====================

void MainWindow::onConversationEvent(const QJsonObject& event)
{
    const QString type = event.value(QStringLiteral("type")).toString();
    const QString sessionId = event.value(QStringLiteral("sessionId")).toString();
    if (type.isEmpty() || sessionId.isEmpty())
        return;

    if (type == QLatin1String("turn_delta")) {
        onStreamData(sessionId, event.value(QStringLiteral("delta")).toString());
        return;
    }

    if (type == QLatin1String("turn_completed")) {
        onFinished(sessionId, event.value(QStringLiteral("fullContent")).toString());
        return;
    }

    if (type == QLatin1String("turn_failed")) {
        onError(sessionId, event.value(QStringLiteral("error")).toString());
        return;
    }

    if (type == QLatin1String("turn_tool_calls_started")) {
        onToolCallsStarted(sessionId);
        return;
    }

    if (type == QLatin1String("turn_tool_event")) {
        const QJsonObject obj = event.value(QStringLiteral("toolEvent")).toObject();
        ToolExecutionEvent toolEvent;
        toolEvent.toolName = obj.value(QStringLiteral("toolName")).toString();
        toolEvent.toolId = obj.value(QStringLiteral("toolId")).toString();
        toolEvent.status = obj.value(QStringLiteral("status")).toString();
        toolEvent.success = obj.value(QStringLiteral("success")).toBool(true);
        toolEvent.data = obj.value(QStringLiteral("data")).toObject();
        toolEvent.rawResult = obj.value(QStringLiteral("rawResult")).toString();
        toolEvent.formattedResult = obj.value(QStringLiteral("formattedResult")).toString();
        onToolEvent(sessionId, toolEvent);
        return;
    }

    if (type == QLatin1String("turn_cancelled")) {
        // 取消时清理占位流消息，避免 UI 残留 pending 内容
        onFinished(sessionId, QString());
        return;
    }

    if (type == QLatin1String("turn_interrupted")) {
        // 插话打断时也清理占位流消息，避免 UI 残留 pending 内容
        onFinished(sessionId, QString());
        return;
    }

    if (type == QLatin1String("turn_rejected")) {
        const QString reason = event.value(QStringLiteral("reason")).toString();
        if (reason == QLatin1String("queue_overflow")) {
            const int queueDepth = event.value(QStringLiteral("queueDepth")).toInt();
            const int queueHardLimit = event.value(QStringLiteral("queueHardLimit")).toInt();
            onError(sessionId, QStringLiteral("队列已满（%1/%2），请稍后重试。").arg(queueDepth).arg(queueHardLimit));
        } else {
            onError(sessionId, QStringLiteral("请求被拒绝。"));
        }
        return;
    }

    if (type.startsWith(QLatin1String("memory."))) {
        if (type == QLatin1String("memory.error")) {
            const QString memoryErr = event.value(QStringLiteral("error")).toString().trimmed();
            if (!memoryErr.isEmpty()) {
                const QString displayErr = QStringLiteral("记忆写入失败: %1").arg(memoryErr);
                for (IdentityView* view : viewsForSession(sessionId))
                    view->handleError(sessionId, displayErr);
            }
        } else if (type == QLatin1String("memory.index.error")) {
            const QString indexErr = event.value(QStringLiteral("error")).toString().trimmed();
            if (!indexErr.isEmpty()) {
                const QString displayErr = QStringLiteral("记忆索引更新失败: %1").arg(indexErr);
                for (IdentityView* view : viewsForSession(sessionId))
                    view->handleError(sessionId, displayErr);
            }
        }
        for (IdentityView* view : viewsForSession(sessionId))
            view->refreshHistoryForSession(sessionId);
        return;
    }

    if (type == QLatin1String("turn_started")) {
        for (IdentityView* view : viewsForSession(sessionId))
            view->refreshSendingState();
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
    if (m_chatService)
        m_chatService->saveSessionsToDisk();

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
    const bool loadedFromDisk = m_chatService->loadSessionsFromDisk();

    const QString userId = IdentityManager::instance()->userIdentity()->id();
    QString targetActiveId = userId;

    if (loadedFromDisk) {
        ChatService::TabState tabState = m_chatService->loadTabState();
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

void MainWindow::onMemoryStewardChanged(int index)
{
    Q_UNUSED(index);
    if (m_memoryUiLoading || !m_memoryStewardCombo || !m_memoryStewardModelCombo)
        return;

    const QString stewardId = m_memoryStewardCombo->currentData().toString().trimmed();
    if (stewardId.isEmpty() || !m_chatService)
        return;

    Identity* steward = IdentityManager::instance()->findById(stewardId);
    if (!steward || !steward->profile())
        return;
    const LLMConfig cfg = steward->profile()->llmConfig();
    const QString cfgId = ModelFactory::resolveConfigKey(cfg);
    const int idx = m_memoryStewardModelCombo->findData(cfgId);
    if (idx >= 0)
        m_memoryStewardModelCombo->setCurrentIndex(idx);
}

void MainWindow::onInfoSettingsClicked()
{
    openMemorySettingsDialog();
}

void MainWindow::onCommandPolicyClicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("命令权限设置"));
    dlg.setMinimumSize(920, 860);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("execute_command 安全策略"), &dlg);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 1);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    const QString toolLoopPolicyPath = QDir::home().filePath(QStringLiteral(".tmagent/config/tool_loop_policy.json"));
    const auto defaultToolLoopPolicyObject = []() {
        QJsonObject obj;
        obj.insert(QStringLiteral("schema_version"), 4);
        obj.insert(QStringLiteral("max_tool_rounds_per_turn"), 12);
        obj.insert(QStringLiteral("max_consecutive_same_tool_rounds"), 4);
        obj.insert(QStringLiteral("max_consecutive_no_progress_rounds"), 4);
        obj.insert(QStringLiteral("max_consecutive_failed_tool_rounds"), 3);
        obj.insert(QStringLiteral("max_total_tool_calls_per_turn"), 24);
        obj.insert(QStringLiteral("max_web_fetch_calls_per_turn"), 8);
        obj.insert(QStringLiteral("max_tool_loop_time_ms"), 180000);
        return obj;
    };
    const auto normalizeToolLoopPolicyObject = [&](const QJsonObject& raw) {
        QJsonObject out = defaultToolLoopPolicyObject();
        out.insert(QStringLiteral("schema_version"), 4);
        out.insert(QStringLiteral("max_tool_rounds_per_turn"), qBound(2, raw.value(QStringLiteral("max_tool_rounds_per_turn")).toInt(out.value(QStringLiteral("max_tool_rounds_per_turn")).toInt()), 64));
        out.insert(QStringLiteral("max_consecutive_same_tool_rounds"), qBound(1, raw.value(QStringLiteral("max_consecutive_same_tool_rounds")).toInt(out.value(QStringLiteral("max_consecutive_same_tool_rounds")).toInt()), 32));
        out.insert(QStringLiteral("max_consecutive_no_progress_rounds"), qBound(1, raw.value(QStringLiteral("max_consecutive_no_progress_rounds")).toInt(out.value(QStringLiteral("max_consecutive_no_progress_rounds")).toInt()), 32));
        out.insert(QStringLiteral("max_consecutive_failed_tool_rounds"), qBound(1, raw.value(QStringLiteral("max_consecutive_failed_tool_rounds")).toInt(out.value(QStringLiteral("max_consecutive_failed_tool_rounds")).toInt()), 32));
        out.insert(QStringLiteral("max_total_tool_calls_per_turn"), qBound(4, raw.value(QStringLiteral("max_total_tool_calls_per_turn")).toInt(out.value(QStringLiteral("max_total_tool_calls_per_turn")).toInt()), 256));
        out.insert(QStringLiteral("max_web_fetch_calls_per_turn"), qBound(1, raw.value(QStringLiteral("max_web_fetch_calls_per_turn")).toInt(out.value(QStringLiteral("max_web_fetch_calls_per_turn")).toInt()), 128));
        out.insert(QStringLiteral("max_tool_loop_time_ms"), qBound<qint64>(5000, raw.value(QStringLiteral("max_tool_loop_time_ms")).toVariant().toLongLong(), 300000));
        return out;
    };
    const auto loadToolLoopPolicyObject = [&]() {
        QFile file(toolLoopPolicyPath);
        if (!file.exists())
            return defaultToolLoopPolicyObject();
        if (!file.open(QFile::ReadOnly | QFile::Text))
            return defaultToolLoopPolicyObject();
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            return defaultToolLoopPolicyObject();
        return normalizeToolLoopPolicyObject(doc.object());
    };
    const auto saveToolLoopPolicyObject = [&](const QJsonObject& raw, QString* errOut) {
        if (errOut)
            errOut->clear();
        if (!QDir().mkpath(QFileInfo(toolLoopPolicyPath).absolutePath())) {
            if (errOut)
                *errOut = tr("创建工具循环策略目录失败");
            return false;
        }
        QFile file(toolLoopPolicyPath);
        if (!file.open(QFile::WriteOnly | QFile::Text)) {
            if (errOut)
                *errOut = tr("写入工具循环策略文件失败");
            return false;
        }
        const QJsonObject normalized = normalizeToolLoopPolicyObject(raw);
        const QByteArray bytes = QJsonDocument(normalized).toJson(QJsonDocument::Indented);
        const bool ok = (file.write(bytes) == bytes.size());
        file.close();
        if (!ok && errOut)
            *errOut = tr("工具循环策略写入不完整");
        return ok;
    };

    auto* desc = new QLabel(
        tr("策略文件位置：\n- 命令权限：%1\n- 工具循环：%2\n"
           "规则默认按“黑名单优先”执行；可选开启白名单前缀校验。"
           "写命令默认仅允许在助手工作空间内。")
            .arg(QDir::toNativeSeparators(ShellTool::policyFilePath()), QDir::toNativeSeparators(toolLoopPolicyPath)),
        &dlg);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color: #4b5563;"));
    layout->addWidget(desc);

    const auto loadToEditors = [&](const QJsonObject& src,
                                   QCheckBox* allowOutsideCheck,
                                   QCheckBox* confirmExecCheck,
                                   QCheckBox* enforceSafeCheck,
                                   QSpinBox* timeoutSpin,
                                   QPlainTextEdit* safeEdit,
                                   QPlainTextEdit* dangerEdit,
                                   QPlainTextEdit* writeEdit) {
        const auto arrToText = [](const QJsonArray& arr) {
            QStringList lines;
            for (const QJsonValue& v : arr) {
                const QString s = v.toString().trimmed();
                if (!s.isEmpty())
                    lines.append(s);
            }
            lines.removeDuplicates();
            return lines.join(QLatin1Char('\n'));
        };

        const QJsonObject policy = ShellTool::normalizePolicyObject(src);
        allowOutsideCheck->setChecked(policy.value(QStringLiteral("allow_outside_workspace")).toBool(false));
        confirmExecCheck->setChecked(policy.value(QStringLiteral("confirm_executable")).toBool(true));
        enforceSafeCheck->setChecked(policy.value(QStringLiteral("enforce_safe_prefixes")).toBool(false));
        timeoutSpin->setValue(qBound(1000, policy.value(QStringLiteral("command_timeout_ms")).toInt(30000), 300000));
        safeEdit->setPlainText(arrToText(policy.value(QStringLiteral("safe_command_prefixes")).toArray()));
        dangerEdit->setPlainText(arrToText(policy.value(QStringLiteral("dangerous_patterns")).toArray()));
        writeEdit->setPlainText(arrToText(policy.value(QStringLiteral("write_command_prefixes")).toArray()));
    };

    auto* optionsForm = new QFormLayout();
    optionsForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    optionsForm->setFormAlignment(Qt::AlignTop);
    optionsForm->setHorizontalSpacing(12);
    optionsForm->setVerticalSpacing(8);

    auto* allowOutsideCheck = new QCheckBox(tr("允许写命令跨工作空间（高风险）"), &dlg);
    auto* confirmExecCheck = new QCheckBox(tr("执行本地可执行文件前弹窗确认"), &dlg);
    auto* enforceSafeCheck = new QCheckBox(tr("启用白名单前缀校验（更严格）"), &dlg);
    auto* timeoutSpin = new QSpinBox(&dlg);
    timeoutSpin->setRange(1000, 300000);
    timeoutSpin->setSingleStep(1000);
    timeoutSpin->setSuffix(QStringLiteral(" ms"));

    optionsForm->addRow(tr("写入范围:"), allowOutsideCheck);
    optionsForm->addRow(tr("执行确认:"), confirmExecCheck);
    optionsForm->addRow(tr("命令准入:"), enforceSafeCheck);
    optionsForm->addRow(tr("命令超时:"), timeoutSpin);
    layout->addLayout(optionsForm);

    auto* toolLoopTitle = new QLabel(tr("工具循环预算"), &dlg);
    QFont subTitleFont = toolLoopTitle->font();
    subTitleFont.setBold(true);
    toolLoopTitle->setFont(subTitleFont);
    layout->addWidget(toolLoopTitle);

    auto* toolLoopForm = new QFormLayout();
    toolLoopForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    toolLoopForm->setFormAlignment(Qt::AlignTop);
    toolLoopForm->setHorizontalSpacing(12);
    toolLoopForm->setVerticalSpacing(8);

    auto* maxToolRoundsSpin = new QSpinBox(&dlg);
    maxToolRoundsSpin->setRange(2, 64);
    auto* maxSameToolRoundsSpin = new QSpinBox(&dlg);
    maxSameToolRoundsSpin->setRange(1, 32);
    auto* maxNoProgressRoundsSpin = new QSpinBox(&dlg);
    maxNoProgressRoundsSpin->setRange(1, 32);
    auto* maxFailedRoundsSpin = new QSpinBox(&dlg);
    maxFailedRoundsSpin->setRange(1, 32);
    auto* maxTotalToolCallsSpin = new QSpinBox(&dlg);
    maxTotalToolCallsSpin->setRange(4, 256);
    auto* maxWebFetchCallsSpin = new QSpinBox(&dlg);
    maxWebFetchCallsSpin->setRange(1, 128);
    auto* maxToolLoopTimeSpin = new QSpinBox(&dlg);
    maxToolLoopTimeSpin->setRange(5000, 300000);
    maxToolLoopTimeSpin->setSingleStep(5000);
    maxToolLoopTimeSpin->setSuffix(QStringLiteral(" ms"));

    toolLoopForm->addRow(tr("单回合最大轮次:"), maxToolRoundsSpin);
    toolLoopForm->addRow(tr("同参数重复上限:"), maxSameToolRoundsSpin);
    toolLoopForm->addRow(tr("无进展轮次上限:"), maxNoProgressRoundsSpin);
    toolLoopForm->addRow(tr("连续失败轮次上限:"), maxFailedRoundsSpin);
    toolLoopForm->addRow(tr("单回合工具调用总数上限:"), maxTotalToolCallsSpin);
    toolLoopForm->addRow(tr("单回合 web_fetch 上限:"), maxWebFetchCallsSpin);
    toolLoopForm->addRow(tr("单回合总时长上限:"), maxToolLoopTimeSpin);
    layout->addLayout(toolLoopForm);

    const auto loadToolLoopToEditors = [&](const QJsonObject& src) {
        const QJsonObject policy = normalizeToolLoopPolicyObject(src);
        maxToolRoundsSpin->setValue(policy.value(QStringLiteral("max_tool_rounds_per_turn")).toInt(12));
        maxSameToolRoundsSpin->setValue(policy.value(QStringLiteral("max_consecutive_same_tool_rounds")).toInt(4));
        maxNoProgressRoundsSpin->setValue(policy.value(QStringLiteral("max_consecutive_no_progress_rounds")).toInt(4));
        maxFailedRoundsSpin->setValue(policy.value(QStringLiteral("max_consecutive_failed_tool_rounds")).toInt(3));
        maxTotalToolCallsSpin->setValue(policy.value(QStringLiteral("max_total_tool_calls_per_turn")).toInt(24));
        maxWebFetchCallsSpin->setValue(policy.value(QStringLiteral("max_web_fetch_calls_per_turn")).toInt(8));
        maxToolLoopTimeSpin->setValue(static_cast<int>(policy.value(QStringLiteral("max_tool_loop_time_ms")).toVariant().toLongLong()));
    };

    auto* safeLabel = new QLabel(tr("白名单前缀（每行一个，允许执行）"), &dlg);
    auto* safeEdit = new QPlainTextEdit(&dlg);
    safeEdit->setPlaceholderText(tr("例如：git clone"));
    safeEdit->setMinimumHeight(170);
    layout->addWidget(safeLabel);
    layout->addWidget(safeEdit);

    auto* dangerLabel = new QLabel(tr("黑名单模式（每行一个，命中即拒绝）"), &dlg);
    auto* dangerEdit = new QPlainTextEdit(&dlg);
    dangerEdit->setPlaceholderText(tr("例如：rm -rf"));
    dangerEdit->setMinimumHeight(120);
    layout->addWidget(dangerLabel);
    layout->addWidget(dangerEdit);

    auto* writeLabel = new QLabel(tr("写命令前缀（每行一个，用于写入范围限制）"), &dlg);
    auto* writeEdit = new QPlainTextEdit(&dlg);
    writeEdit->setPlaceholderText(tr("例如：git clone"));
    writeEdit->setMinimumHeight(120);
    layout->addWidget(writeLabel);
    layout->addWidget(writeEdit);

    const QJsonObject currentPolicy = ShellTool::loadPolicyObject();
    const QJsonObject currentToolLoopPolicy = loadToolLoopPolicyObject();
    loadToEditors(currentPolicy, allowOutsideCheck, confirmExecCheck, enforceSafeCheck, timeoutSpin, safeEdit, dangerEdit, writeEdit);
    loadToolLoopToEditors(currentToolLoopPolicy);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    QPushButton* saveBtn = buttons->button(QDialogButtonBox::Save);
    QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    if (saveBtn)
        saveBtn->setText(tr("保存"));
    if (cancelBtn)
        cancelBtn->setText(tr("取消"));
    QPushButton* resetBtn = buttons->addButton(tr("恢复默认"), QDialogButtonBox::ResetRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(resetBtn, &QPushButton::clicked, &dlg, [=]() {
        loadToEditors(ShellTool::defaultPolicyObject(), allowOutsideCheck, confirmExecCheck, enforceSafeCheck, timeoutSpin, safeEdit, dangerEdit, writeEdit);
        loadToolLoopToEditors(defaultToolLoopPolicyObject());
    });

    connect(buttons, &QDialogButtonBox::accepted, &dlg, [this, allowOutsideCheck, confirmExecCheck, enforceSafeCheck, timeoutSpin, safeEdit, dangerEdit, writeEdit, maxToolRoundsSpin, maxSameToolRoundsSpin, maxNoProgressRoundsSpin, maxFailedRoundsSpin, maxTotalToolCallsSpin, maxWebFetchCallsSpin, maxToolLoopTimeSpin, saveToolLoopPolicyObject, &dlg]() {
        const auto textToArray = [](const QString& text) {
            QStringList lines;
            const QStringList rawLines = text.split(QLatin1Char('\n'));
            for (const QString& line : rawLines) {
                const QString trimmed = line.trimmed();
                if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
                    continue;
                lines.append(trimmed);
            }
            lines.removeDuplicates();
            QJsonArray arr;
            for (const QString& line : lines)
                arr.append(line);
            return arr;
        };

        QJsonObject raw;
        raw.insert(QStringLiteral("allow_outside_workspace"), allowOutsideCheck->isChecked());
        raw.insert(QStringLiteral("confirm_executable"), confirmExecCheck->isChecked());
        raw.insert(QStringLiteral("enforce_safe_prefixes"), enforceSafeCheck->isChecked());
        raw.insert(QStringLiteral("command_timeout_ms"), timeoutSpin->value());
        raw.insert(QStringLiteral("safe_command_prefixes"), textToArray(safeEdit->toPlainText()));
        raw.insert(QStringLiteral("dangerous_patterns"), textToArray(dangerEdit->toPlainText()));
        raw.insert(QStringLiteral("write_command_prefixes"), textToArray(writeEdit->toPlainText()));

        QString err;
        if (!ShellTool::savePolicyObject(raw, &err)) {
            QMessageBox::warning(this, tr("保存失败"), err.isEmpty() ? tr("无法写入命令权限配置。") : err);
            return;
        }

        QJsonObject toolLoopRaw;
        toolLoopRaw.insert(QStringLiteral("schema_version"), 4);
        toolLoopRaw.insert(QStringLiteral("max_tool_rounds_per_turn"), maxToolRoundsSpin->value());
        toolLoopRaw.insert(QStringLiteral("max_consecutive_same_tool_rounds"), maxSameToolRoundsSpin->value());
        toolLoopRaw.insert(QStringLiteral("max_consecutive_no_progress_rounds"), maxNoProgressRoundsSpin->value());
        toolLoopRaw.insert(QStringLiteral("max_consecutive_failed_tool_rounds"), maxFailedRoundsSpin->value());
        toolLoopRaw.insert(QStringLiteral("max_total_tool_calls_per_turn"), maxTotalToolCallsSpin->value());
        toolLoopRaw.insert(QStringLiteral("max_web_fetch_calls_per_turn"), maxWebFetchCallsSpin->value());
        toolLoopRaw.insert(QStringLiteral("max_tool_loop_time_ms"), maxToolLoopTimeSpin->value());

        QString toolLoopErr;
        if (!saveToolLoopPolicyObject(toolLoopRaw, &toolLoopErr)) {
            QMessageBox::warning(this, tr("保存失败"), toolLoopErr.isEmpty() ? tr("无法写入工具循环配置。") : toolLoopErr);
            return;
        }

        QMessageBox::information(this, tr("保存成功"), tr("命令权限与工具循环配置已更新，将在下一次工具调用时生效。"));
        dlg.accept();
    });

    dlg.exec();
}

void MainWindow::onToolLogClicked()
{
    if (!m_toolLogWindow)
        m_toolLogWindow = new ToolLogWidget();
    m_toolLogWindow->show();
    m_toolLogWindow->raise();
    m_toolLogWindow->activateWindow();
}

// ==================== MCP 配置 ====================

void MainWindow::onMcpConfigClicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("配置 MCP 工具服务"));

    auto* layout = new QVBoxLayout(&dlg);
    auto* hint = new QLabel(tr("每行一个 server：name|url|token|header|prefix|async\n"
                               "示例: exa|https://example.com/mcp|TOKEN|Authorization|1|1\n"
                               "说明: prefix=1 将工具名前缀为 name:tool，async=1 使用异步回传。"),
                            &dlg);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* editor = new QPlainTextEdit(&dlg);
    const QStringList specs = m_chatService->loadMcpConfigSpecs();
    editor->setPlainText(specs.join('\n'));
    layout->addWidget(editor, 1);

    auto* envHint = new QLabel(tr("注意：环境变量 TMAGENT_MCP_SERVERS 会在运行时追加，但不会写入此配置。"), &dlg);
    envHint->setWordWrap(true);
    layout->addWidget(envHint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    QStringList newSpecs;
    const QStringList lines = editor->toPlainText().split('\n');
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#'))
            continue;
        newSpecs.append(trimmed);
    }

    if (!m_chatService->saveMcpConfigSpecs(newSpecs)) {
        QMessageBox::warning(this, tr("保存失败"), tr("无法写入 MCP 配置文件。"));
        return;
    }

    m_chatService->applyMcpConfig(newSpecs);
    m_chatService->applyToolDispatcherToAllRuntimes();
    QMessageBox::information(this, tr("配置已保存"), tr("MCP 配置已更新。"));
}

// ==================== 模型配置导入 ====================

namespace {
bool extractEnvVarName(const QString& value, QString* varName)
{
    if (!varName)
        return false;
    const QString trimmed = value.trimmed();
    if (trimmed.startsWith(QStringLiteral("$ENV{")) && trimmed.endsWith('}')) {
        *varName = trimmed.mid(5, trimmed.size() - 6).trimmed();
        return !varName->isEmpty();
    }
    if (trimmed.startsWith(QStringLiteral("${")) && trimmed.endsWith('}')) {
        *varName = trimmed.mid(2, trimmed.size() - 3).trimmed();
        return !varName->isEmpty();
    }
    if (trimmed.startsWith('$') && trimmed.size() > 1 && !trimmed.contains(' ')) {
        *varName = trimmed.mid(1).trimmed();
        return !varName->isEmpty();
    }
    return false;
}

bool isEnvVarReference(const QString& value)
{
    QString dummy;
    return extractEnvVarName(value, &dummy);
}

QString canonicalProviderId(const QString& providerId)
{
    const QString id = providerId.trimmed().toLower();
    if (id == QStringLiteral("claude") || id == QStringLiteral("claudeai")
        || id == QStringLiteral("anthropic")) {
        return QStringLiteral("anthropic");
    }
    if (id == QStringLiteral("google"))
        return QStringLiteral("gemini");
    return id;
}

bool isAnthropicProviderId(const QString& providerId)
{
    return canonicalProviderId(providerId) == QStringLiteral("anthropic");
}

QString normalizeBaseUrl(const QString& baseUrl)
{
    QString url = baseUrl.trimmed();
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

QString resolveApiKeyInputForTest(const QString& apiKeyInput)
{
    QString varName;
    if (extractEnvVarName(apiKeyInput, &varName))
        return QProcessEnvironment::systemEnvironment().value(varName);
    return apiKeyInput.trimmed();
}

QString buildModelsEndpoint(const QString& providerId, const QString& baseUrl)
{
    const QString root = normalizeBaseUrl(baseUrl);
    if (root.isEmpty())
        return QString();

    if (isAnthropicProviderId(providerId)) {
        if (root.endsWith(QStringLiteral("/v1/models"), Qt::CaseInsensitive))
            return root;
        if (root.endsWith(QStringLiteral("/v1"), Qt::CaseInsensitive))
            return root + QStringLiteral("/models");
        return root + QStringLiteral("/v1/models");
    }

    if (root.endsWith(QStringLiteral("/models"), Qt::CaseInsensitive))
        return root;
    return root + QStringLiteral("/models");
}

bool isHttpReachable(QNetworkReply* reply)
{
    if (!reply)
        return false;
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    return status.isValid() || reply->error() == QNetworkReply::NoError;
}

QString extractErrorMessage(const QByteArray& body, const QString& fallback)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fallback.trimmed();

    const QJsonObject root = doc.object();
    const QJsonObject errObj = root.value(QStringLiteral("error")).toObject();
    const QString errMsg = errObj.value(QStringLiteral("message")).toString().trimmed();
    if (!errMsg.isEmpty())
        return errMsg;

    const QString msg = root.value(QStringLiteral("message")).toString().trimmed();
    if (!msg.isEmpty())
        return msg;

    return fallback.trimmed();
}

QStringList parseModelIdsFromResponse(const QByteArray& body)
{
    QStringList modelIds;
    QSet<QString> seen;
    auto append = [&modelIds, &seen](const QString& candidate) {
        const QString id = candidate.trimmed();
        if (id.isEmpty() || seen.contains(id))
            return;
        seen.insert(id);
        modelIds.append(id);
    };

    auto parseArray = [&append](const QJsonArray& arr) {
        for (const QJsonValue& item : arr) {
            if (item.isString()) {
                append(item.toString());
                continue;
            }
            if (!item.isObject())
                continue;
            const QJsonObject obj = item.toObject();
            append(obj.value(QStringLiteral("id")).toString());
            append(obj.value(QStringLiteral("model")).toString());
            append(obj.value(QStringLiteral("name")).toString());
        }
    };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return modelIds;

    if (doc.isArray()) {
        parseArray(doc.array());
        return modelIds;
    }

    if (!doc.isObject())
        return modelIds;

    const QJsonObject root = doc.object();
    parseArray(root.value(QStringLiteral("data")).toArray());
    parseArray(root.value(QStringLiteral("models")).toArray());
    parseArray(root.value(QStringLiteral("result")).toArray());
    return modelIds;
}

QList<ModelConfigProvider> defaultModelConfigProviders()
{
    QList<ModelConfigProvider> list;
    ModelConfigProvider deepseek { "deepseek", "DeepSeek", "中国高性能 AI 模型" };
    deepseek.fields << ModelConfigField { "apiKey", "API 密钥", "sk-...", "", true, true };
    deepseek.fields << ModelConfigField { "modelId", "模型名称", "deepseek-chat", "deepseek-chat" };
    deepseek.fields << ModelConfigField { "baseUrl", "接口地址", "https://api.deepseek.com", "https://api.deepseek.com" };
    list << deepseek;

    ModelConfigProvider openai { "openai", "OpenAI", "全球领先的 AI 语言模型" };
    openai.fields << ModelConfigField { "apiKey", "API 密钥", "sk-...", "", true, true };
    openai.fields << ModelConfigField { "modelId", "模型名称", "gpt-4o", "gpt-4o" };
    openai.fields << ModelConfigField { "baseUrl", "接口地址", "https://api.openai.com/v1", "https://api.openai.com/v1" };
    list << openai;

    ModelConfigProvider anthropic { "anthropic", "Anthropic / Claude", "Anthropic 强大的 AI 模型" };
    anthropic.fields << ModelConfigField { "apiKey", "API 密钥", "sk-ant-...", "", true, true };
    anthropic.fields << ModelConfigField { "modelId", "模型名称", "claude-sonnet-4-5-20250929", "claude-sonnet-4-5-20250929" };
    anthropic.fields << ModelConfigField { "baseUrl", "接口地址", "https://api.anthropic.com", "https://api.anthropic.com" };
    list << anthropic;

    ModelConfigProvider ollama { "ollama", "Ollama", "本地运行的各类型开源模型" };
    ollama.fields << ModelConfigField { "modelId", "模型名称", "llama3", "llama3" };
    ollama.fields << ModelConfigField { "baseUrl", "接口地址", "http://localhost:11434", "http://localhost:11434" };
    list << ollama;

    ModelConfigProvider gemini { "gemini", "Gemini", "Google 强大的 AI 服务" };
    gemini.fields << ModelConfigField { "apiKey", "API 密钥", "在此输入密钥", "", true, true };
    gemini.fields << ModelConfigField { "modelId", "模型名称", "gemini-1.5-pro", "gemini-1.5-pro" };
    gemini.fields << ModelConfigField { "baseUrl", "接口地址", "https://generativelanguage.googleapis.com", "" };
    list << gemini;

    return list;
}
} // namespace

void MainWindow::onModelConfigImportClicked()
{
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("模型配置管理"));
    dlg->resize(800, 520);

    auto* page = new ModelConfigManagerPage(dlg);
    page->setProviders(defaultModelConfigProviders());
    page->setYamlPath(m_chatService->modelConfigPath());

    // 注入数据加载回调（子模块已解耦，不再直接依赖 ModelConfigLoader）
    page->setConfigListLoader([](const QString& path) -> QList<ModelConfigEntry> {
        QList<ModelConfigEntry> result;
        const auto instances = ModelConfigLoader::loadProviderInstances(path, false);
        for (const auto& inst : instances) {
            ModelConfigEntry entry;
            entry.configId = inst.instanceId;
            entry.providerId = inst.providerType;
            entry.displayName = inst.displayName.isEmpty() ? inst.instanceId : inst.displayName;
            entry.baseUrl = inst.baseUrl;
            entry.apiKey = inst.apiKey;
            entry.modelId = QString();
            entry.enabled = inst.enabled;
            result.append(entry);
        }
        return result;
    });
    page->setSingleConfigLoader([](const QString& path, const QString& configId) -> ModelConfigEntry {
        ModelConfigEntry entry;
        const ProviderInstanceConfig inst = ModelConfigLoader::getProviderInstance(path, configId, false);
        if (inst.isValid()) {
            entry.configId = inst.instanceId;
            entry.providerId = inst.providerType;
            entry.displayName = inst.displayName.isEmpty() ? inst.instanceId : inst.displayName;
            entry.baseUrl = inst.baseUrl;
            entry.apiKey = inst.apiKey;
            entry.modelId = QString();
            entry.enabled = inst.enabled;
        }
        return entry;
    });
    page->setDefaultConfigIdLoader([](const QString& path) -> QString {
        return ModelConfigLoader::getDefaultProvider(path);
    });

    page->refreshConfigList();
    page->applyStyleSheet();

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(page);

    const QString yamlPath = m_chatService->modelConfigPath();

    // ---- configSaved: 新建或编辑保存 ----
    connect(page, &ModelConfigManagerPage::configSaved, this, [this, page, yamlPath](const QVariantMap& config) {
        ModelConfig modelConfig;
        modelConfig.modelId = config.value("modelId").toString().trimmed();
        modelConfig.configId = config.value("configId").toString().trimmed();
        modelConfig.enabled = config.value("enabled", true).toBool();
        modelConfig.displayName = config.value("providerName").toString();
        modelConfig.provider = canonicalProviderId(config.value("providerId").toString());
        if (modelConfig.provider.isEmpty())
            modelConfig.provider = config.value("providerId").toString().trimmed();
        modelConfig.baseUrl = config.value("baseUrl").toString().trimmed();

        if (modelConfig.configId.isEmpty())
            modelConfig.configId = modelConfig.modelId;

        // 编辑模式下不做 provider 冲突检查
        const bool isEdit = config.value("editMode").toBool();
        if (!isEdit) {
            const ProviderInstanceConfig existing =
                ModelConfigLoader::getProviderInstance(yamlPath, modelConfig.configId, false);
            if (existing.isValid() && !existing.providerType.isEmpty()
                && canonicalProviderId(existing.providerType) != modelConfig.provider) {
                QMessageBox::warning(
                    this, tr("配置ID冲突"),
                    tr("配置ID「%1」已归属于 Provider「%2」。\n为避免混用，请修改配置 ID 或先删除旧配置后再导入。")
                        .arg(modelConfig.configId, existing.providerType));
                return;
            }
        }

        // API Key 处理
        QString apiKeyStored;
        QString apiKeyRuntime;
        const QString apiKeyInput = config.value("apiKey").toString().trimmed();
        if (!apiKeyInput.isEmpty()) {
            QString keychainId;
            if (KeychainHelper::parseKeyRef(apiKeyInput, &keychainId)) {
                apiKeyStored = KeychainHelper::makeKeyRef(keychainId);
                bool ok = false;
                QString error;
                apiKeyRuntime = KeychainHelper::readPasswordSync(keychainId, &ok, &error);
                if (!ok || apiKeyRuntime.isEmpty()) {
                    QMessageBox::warning(this, tr("读取失败"),
                        tr("无法从系统密钥库读取：%1").arg(error.isEmpty() ? tr("未知错误") : error));
                    return;
                }
            } else if (isEnvVarReference(apiKeyInput)) {
                apiKeyStored = apiKeyInput;
                QString varName;
                if (extractEnvVarName(apiKeyInput, &varName))
                    apiKeyRuntime = QProcessEnvironment::systemEnvironment().value(varName);
                if (apiKeyRuntime.isEmpty()) {
                    QMessageBox::warning(this, tr("环境变量未设置"),
                        tr("未读取到 %1，请先设置环境变量后再导入。").arg(apiKeyInput));
                    return;
                }
            } else {
                keychainId = KeychainHelper::entryIdForModel(modelConfig.provider, modelConfig.modelId);
                QString error;
                if (!KeychainHelper::writePasswordSync(keychainId, apiKeyInput, &error)) {
                    QMessageBox::warning(this, tr("保存失败"),
                        tr("无法写入系统密钥库：%1").arg(error.isEmpty() ? tr("未知错误") : error));
                    return;
                }
                apiKeyStored = KeychainHelper::makeKeyRef(keychainId);
                apiKeyRuntime = apiKeyInput;
            }
        }
        modelConfig.apiKey = apiKeyRuntime;
        modelConfig.authType = isAnthropicProviderId(modelConfig.provider)
            ? QStringLiteral("X-API-Key")
            : QStringLiteral("Bearer");
        modelConfig.temperature = 0.7;
        modelConfig.maxTokens = 4096;
        modelConfig.timeoutMs = 180000;
        modelConfig.capabilities << Capability::TextGeneration << Capability::ToolCalling;
        modelConfig.toolCalling = true;
        modelConfig.systemPrompt = DefaultPrompts::codingAssistantSystemPrompt();

        ModelConfig saveConfig = modelConfig;
        saveConfig.apiKey = apiKeyStored;

        // 保存为 ProviderInstanceConfig (v2)
        ProviderInstanceConfig inst;
        inst.instanceId = modelConfig.configId;
        inst.enabled = modelConfig.enabled;
        inst.displayName = modelConfig.displayName;
        inst.providerType = modelConfig.provider;
        inst.baseUrl = modelConfig.baseUrl;
        inst.apiKey = apiKeyStored;
        inst.authType = modelConfig.authType;
        inst.defaultTemperature = modelConfig.temperature;
        inst.defaultMaxTokens = modelConfig.maxTokens;
        inst.defaultTimeoutMs = modelConfig.timeoutMs;
        inst.capabilities = modelConfig.capabilities;
        inst.toolCalling = modelConfig.toolCalling;
        inst.contextLength = modelConfig.contextLength;
        ModelConfigLoader::addOrUpdateProviderInstance(yamlPath, inst);
        const QString currentDefault = ModelConfigLoader::getDefaultProvider(yamlPath);
        if (currentDefault.trimmed().isEmpty())
            ModelConfigLoader::setDefaultProvider(yamlPath, modelConfig.configId);

        m_chatService->modelFactory()->registerModelConfig(modelConfig);

        LLMConfig agentConfig;
        agentConfig.configId = modelConfig.configId;
        agentConfig.systemPrompt = modelConfig.systemPrompt;
        agentConfig.userName = tr("TM Agent");
        m_chatService->setDefaultAgentConfig(agentConfig);
        m_chatService->applyConfigToAllRuntimes();

        page->refreshConfigList();
        QMessageBox::information(this, tr("已保存"),
            tr("配置「%1」已保存到 %2").arg(modelConfig.configId, QDir::toNativeSeparators(yamlPath)));
    });

    // ---- configDeleted ----
    connect(page, &ModelConfigManagerPage::configDeleted, this, [this, page, yamlPath](const QString& configId) {
        ModelConfigLoader::removeProviderInstance(yamlPath, configId);
        page->refreshConfigList();
    });

    // ---- defaultChanged ----
    connect(page, &ModelConfigManagerPage::defaultChanged, this, [this, page, yamlPath](const QString& configId) {
        ModelConfigLoader::setDefaultProvider(yamlPath, configId);
        page->refreshConfigList();
    });

    // ---- enabledToggled ----
    connect(page, &ModelConfigManagerPage::enabledToggled, this, [yamlPath](const QString& configId, bool enabled) {
        ModelConfigLoader::setProviderEnabled(yamlPath, configId, enabled);
    });

    // ---- testConnectionRequested ----
    connect(page, &ModelConfigManagerPage::testConnectionRequested, this, [page](const QVariantMap& config) {
        const QString providerId =
            canonicalProviderId(config.value(QStringLiteral("providerId")).toString());
        const QString baseUrl = config.value(QStringLiteral("baseUrl")).toString().trimmed();
        const QString apiKey = resolveApiKeyInputForTest(config.value(QStringLiteral("apiKey")).toString());

        page->clearFieldErrors();
        page->setTestStatus(ModelConfigManagerPage::TestStatus::Testing, QObject::tr("正在验证地址连通性…"));

        if (baseUrl.isEmpty()) {
            page->setFieldError(providerId, QStringLiteral("baseUrl"), QObject::tr("接口地址不能为空"));
            page->setTestStatus(ModelConfigManagerPage::TestStatus::Failed, QObject::tr("接口地址不能为空"));
            return;
        }

        const QUrl parsedBase(baseUrl);
        if (!parsedBase.isValid()
            || (parsedBase.scheme() != QStringLiteral("http")
                && parsedBase.scheme() != QStringLiteral("https"))) {
            page->setFieldError(providerId, QStringLiteral("baseUrl"), QObject::tr("请输入合法的 http/https 地址"));
            page->setTestStatus(ModelConfigManagerPage::TestStatus::Failed, QObject::tr("地址格式不合法"));
            return;
        }

        const QString modelsEndpoint = buildModelsEndpoint(providerId, baseUrl);
        if (modelsEndpoint.isEmpty()) {
            page->setFieldError(providerId, QStringLiteral("baseUrl"), QObject::tr("无法生成模型列表地址"));
            page->setTestStatus(ModelConfigManagerPage::TestStatus::Failed, QObject::tr("模型列表地址无效"));
            return;
        }

        QPointer<ModelConfigManagerPage> safePage(page);
        auto* nam = new QNetworkAccessManager(page);
        QNetworkReply* pingReply = nam->get(QNetworkRequest(parsedBase));
        connect(pingReply, &QNetworkReply::finished, page, [safePage, nam, pingReply, providerId, modelsEndpoint, apiKey]() {
            const bool reachable = isHttpReachable(pingReply);
            const QString pingError = pingReply->errorString();
            pingReply->deleteLater();

            if (!safePage) {
                nam->deleteLater();
                return;
            }

            if (!reachable) {
                safePage->setFieldError(providerId, QStringLiteral("baseUrl"), QObject::tr("无法连通：%1").arg(pingError));
                safePage->setTestStatus(ModelConfigManagerPage::TestStatus::Failed, QObject::tr("接口地址不可达"));
                nam->deleteLater();
                return;
            }

            safePage->setTestStatus(ModelConfigManagerPage::TestStatus::Testing, QObject::tr("地址可达，正在拉取模型列表…"));

            QNetworkRequest modelsReq { QUrl(modelsEndpoint) };
            modelsReq.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

            if (isAnthropicProviderId(providerId)) {
                if (!apiKey.isEmpty())
                    modelsReq.setRawHeader("x-api-key", apiKey.toUtf8());
                modelsReq.setRawHeader("anthropic-version", "2023-06-01");
            } else if (!apiKey.isEmpty()) {
                modelsReq.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
            }

            QNetworkReply* modelsReply = nam->get(modelsReq);
            connect(modelsReply, &QNetworkReply::finished, safePage.data(), [safePage, nam, modelsReply, providerId]() {
                const int httpStatus =
                    modelsReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray body = modelsReply->readAll();
                const QString fallbackMsg = modelsReply->errorString();
                modelsReply->deleteLater();

                if (!safePage) {
                    nam->deleteLater();
                    return;
                }

                const bool ok = (httpStatus >= 200 && httpStatus < 300);
                if (!ok) {
                    const QString errorMsg = extractErrorMessage(body, fallbackMsg);
                    if (httpStatus == 401 || httpStatus == 403) {
                        safePage->setFieldError(providerId, QStringLiteral("apiKey"),
                            QObject::tr("鉴权失败，请检查 API Key"));
                    }
                    safePage->setTestStatus(ModelConfigManagerPage::TestStatus::Failed,
                        QObject::tr("地址可达，但拉取模型失败（HTTP %1）：%2")
                            .arg(httpStatus)
                            .arg(errorMsg));
                    nam->deleteLater();
                    return;
                }

                const QStringList modelIds = parseModelIdsFromResponse(body);
                if (!modelIds.isEmpty()) {
                    safePage->setFieldOptions(providerId, QStringLiteral("modelId"), modelIds, true);
                    safePage->setTestStatus(ModelConfigManagerPage::TestStatus::Success,
                        QObject::tr("连接成功，发现 %1 个可用模型").arg(modelIds.size()));
                } else {
                    safePage->setTestStatus(ModelConfigManagerPage::TestStatus::Success,
                        QObject::tr("连接成功，但未返回模型列表，可手动输入模型名称"));
                }

                nam->deleteLater();
            });
        });
    });

    dlg->exec();
    dlg->deleteLater();
}
