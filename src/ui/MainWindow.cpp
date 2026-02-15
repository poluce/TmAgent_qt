#include "MainWindow.h"
#include "AgentCreateDialog.h"
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
#include "newCore/LLMTypes.h"
#include "newCore/ModelFactory.h"
#include <algorithm>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QCheckBox>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QSpinBox>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QColor identityAvatarColor(const QString& identityId)
{
    const uint h = qHash(identityId);
    return QColor::fromHsv(static_cast<int>(h % 360), 120, 212);
}

QIcon makeIdentityAvatarIcon(const QString& identityId,
                             const QString& displayName,
                             int side = 54,
                             int cornerRadius = 12)
{
    const int avatarSide = qMax(16, side);
    const int radius = qMax(0, cornerRadius);

    Identity* identity = IdentityManager::instance()->findById(identityId);
    const QString avatarPath = identity ? identity->avatar().trimmed() : QString();
    if (!avatarPath.isEmpty()) {
        QPixmap source(avatarPath);
        if (!source.isNull()) {
            QPixmap avatar(avatarSide, avatarSide);
            avatar.fill(Qt::transparent);
            QPainter painter(&avatar);
            painter.setRenderHint(QPainter::Antialiasing, true);
            QPainterPath path;
            path.addRoundedRect(QRectF(0, 0, avatarSide, avatarSide), radius, radius);
            painter.setClipPath(path);
            painter.drawPixmap(0, 0,
                               source.scaled(avatarSide, avatarSide,
                                             Qt::KeepAspectRatioByExpanding,
                                             Qt::SmoothTransformation));
            return QIcon(avatar);
        }
    }

    QPixmap pixmap(avatarSide, avatarSide);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(identityAvatarColor(identityId));
    painter.drawRoundedRect(pixmap.rect(), radius, radius);

    QString avatarText = displayName.trimmed();
    if (avatarText.isEmpty())
        avatarText = QStringLiteral("A");
    avatarText = avatarText.left(1).toUpper();

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(qMax(16, avatarSide / 2));
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, avatarText);
    return QIcon(pixmap);
}

QIcon makeMenuCardGlyphIcon(const QString& glyph,
                            const QColor& bgColor,
                            int side = 54,
                            int cornerRadius = 12)
{
    const int iconSide = qMax(16, side);
    const int radius = qMax(0, cornerRadius);

    QPixmap pixmap(iconSide, iconSide);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(pixmap.rect(), radius, radius);

    QString iconText = glyph.trimmed();
    if (iconText.isEmpty())
        iconText = QStringLiteral("T");
    iconText = iconText.left(1);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(qMax(16, iconSide / 2));
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, iconText);
    return QIcon(pixmap);
}

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
        makeMenuCardGlyphIcon(QStringLiteral("导"), QColor(QStringLiteral("#60a5fa")), kMenuCardAvatarSide, kMenuCardRadius));
    m_mcpConfigBtn = makeToolButton(
        tr("配置 MCP"),
        tr("配置 MCP 工具服务（可选）"),
        makeMenuCardGlyphIcon(QStringLiteral("M"), QColor(QStringLiteral("#34d399")), kMenuCardAvatarSide, kMenuCardRadius));
    m_toolLogBtn = makeToolButton(
        tr("工具日志"),
        tr("打开工具执行日志窗口"),
        makeMenuCardGlyphIcon(QStringLiteral("志"), QColor(QStringLiteral("#f59e0b")), kMenuCardAvatarSide, kMenuCardRadius));
    m_infoSettingsBtn = makeToolButton(
        tr("信息设置"),
        tr("配置记忆管家、模型与用户信息"),
        makeMenuCardGlyphIcon(QStringLiteral("设"), QColor(QStringLiteral("#a78bfa")), kMenuCardAvatarSide, kMenuCardRadius));
    m_commandPolicyBtn = makeToolButton(
        tr("命令权限"),
        tr("查看并编辑 execute_command 的白名单、黑名单与执行策略"),
        makeMenuCardGlyphIcon(QStringLiteral("权"), QColor(QStringLiteral("#0ea5e9")), kMenuCardAvatarSide, kMenuCardRadius));

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

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    QPushButton* saveBtn = buttons->button(QDialogButtonBox::Save);
    QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    if (saveBtn)
        saveBtn->setText(tr("保存"));
    if (cancelBtn)
        cancelBtn->setText(tr("取消"));
    layout->addWidget(buttons);

    connect(m_memoryStewardCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            &MainWindow::onMemoryStewardChanged);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, [this, &dlg]() {
        QString err;
        if (!saveMemorySettingsUi(&err)) {
            QMessageBox::warning(this,
                                 tr("保存失败"),
                                 err.isEmpty() ? tr("信息设置保存失败。") : err);
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
    const QString policyStewardId =
        policyObj.value(QStringLiteral("memory_steward_agent_id")).toString().trimmed();
    QString stewardId = policyStewardId.isEmpty() ? previousStewardId : policyStewardId;
    int stewardIndex = m_memoryStewardCombo->findData(stewardId);
    if (stewardIndex < 0)
        stewardIndex = 0;
    m_memoryStewardCombo->setCurrentIndex(stewardIndex);

    const QStringList modelIds = m_chatService && m_chatService->modelFactory()
        ? m_chatService->modelFactory()->registeredModelIds()
        : QStringList();
    m_memoryStewardModelCombo->clear();
    for (const QString& modelId : modelIds)
        m_memoryStewardModelCombo->addItem(modelId, modelId);

    const QString selectedStewardId = m_memoryStewardCombo->currentData().toString().trimmed();
    QString stewardModelId;
    if (!selectedStewardId.isEmpty()) {
        Identity* steward = IdentityManager::instance()->findById(selectedStewardId);
        if (steward && steward->profile()) {
            const LLMConfig cfg = steward->profile()->llmConfig();
            stewardModelId = ModelFactory::resolveModelKey(cfg.model, cfg.customModelId);
        }
    }
    if (stewardModelId.isEmpty() && m_chatService)
        stewardModelId = ModelFactory::resolveModelKey(
            m_chatService->defaultAgentConfig().model,
            m_chatService->defaultAgentConfig().customModelId);
    int modelIndex = m_memoryStewardModelCombo->findData(stewardModelId);
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

    QJsonObject policyObj;
    policyObj.insert(QStringLiteral("memory_steward_agent_id"), stewardId);
    if (!writeJsonFileObject(memoryPolicyFilePath(), policyObj)) {
        if (error)
            *error = tr("写入 memory_policy.json 失败");
        return false;
    }

    if (!stewardId.isEmpty() && !stewardModelId.isEmpty()) {
        Identity* steward = IdentityManager::instance()->findById(stewardId);
        if (steward && steward->isAgent() && steward->profile()) {
            LLMConfig cfg = steward->profile()->llmConfig();
            const ModelFactory::ParsedModelId parsed = ModelFactory::parseModelKey(stewardModelId);
            if (parsed.model != ModelId::Unknown) {
                cfg.model = parsed.model;
                cfg.customModelId = parsed.customModelId;
                steward->profile()->setLlmConfig(cfg);
            }
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

    constexpr int kAlignedAvatarSide = 54;    // 与左侧会话列表一致
    constexpr int kAlignedAvatarRadius = 12;  // 与左侧会话列表一致

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
    agents.erase(std::remove_if(agents.begin(), agents.end(),
                                [](Identity* identity) { return identity == nullptr; }),
                 agents.end());
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
        button->setIcon(makeIdentityAvatarIcon(identityId, displayName, loginAvatarSide, kAlignedAvatarRadius));
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
                const int toolsPageHeight =
                    m_toolsScrollArea->minimumHeight() + toolsMargins.top() + toolsMargins.bottom();
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
    QStringList modelIds;
    if (ModelFactory* factory = m_chatService->modelFactory())
        modelIds = factory->registeredModelIds();
    const LLMConfig defaultAgentCfg = m_chatService->defaultAgentConfig();
    const QString defaultModelId = ModelFactory::resolveModelKey(defaultAgentCfg.model, defaultAgentCfg.customModelId);

    AgentCreateDialog dlg(modelIds, defaultModelId, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString name = dlg.agentName();
    const QString prompt = dlg.systemPrompt();
    const QString roleName = dlg.roleName();
    const QString avatarPath = dlg.avatarPath();
    const QString selectedModelId = dlg.modelId();

    // 创建 Agent Identity
    auto* profile = new IdentityProfile();
    LLMConfig agentCfg = defaultAgentCfg;
    if (!selectedModelId.isEmpty()) {
        const ModelFactory::ParsedModelId parsed = ModelFactory::parseModelKey(selectedModelId);
        if (parsed.model != ModelId::Unknown) {
            agentCfg.model = parsed.model;
            agentCfg.customModelId = parsed.customModelId;
        }
    }
    if (!prompt.isEmpty())
        agentCfg.systemPrompt = prompt;
    profile->setLlmConfig(agentCfg);
    if (!prompt.isEmpty())
        profile->setSystemPrompt(prompt);
    if (!roleName.isEmpty())
        profile->setDescription(roleName);
    if (ToolDispatcher* dispatcher = m_chatService->toolDispatcher()) {
        QStringList toolNames;
        const QList<Tool> tools = dispatcher->getAllToolSchemas();
        for (const Tool& tool : tools) {
            const QString name = tool.name.trimmed();
            if (!name.isEmpty())
                toolNames.append(name);
        }
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
            onError(sessionId, QStringLiteral("队列已满（%1/%2），请稍后重试。")
                                   .arg(queueDepth)
                                   .arg(queueHardLimit));
        } else {
            onError(sessionId, QStringLiteral("请求被拒绝。"));
        }
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
    // 工具日志窗口
    if (m_toolLogWindow)
        m_toolLogWindow->logEvent(event);

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
    const QString modelId = ModelFactory::resolveModelKey(cfg.model, cfg.customModelId);
    const int idx = m_memoryStewardModelCombo->findData(modelId);
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
    dlg.setMinimumSize(920, 760);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(10);

    auto* title = new QLabel(tr("execute_command 安全策略"), &dlg);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 1);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* desc = new QLabel(
        tr("策略文件位置：%1\n"
           "规则按“黑名单优先、再白名单”执行。写命令默认仅允许在助手工作空间内。")
            .arg(QDir::toNativeSeparators(ShellTool::policyFilePath())),
        &dlg);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color: #4b5563;"));
    layout->addWidget(desc);

    const auto loadToEditors = [&](const QJsonObject& src,
                                   QCheckBox* allowOutsideCheck,
                                   QCheckBox* confirmExecCheck,
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
        timeoutSpin->setValue(qBound(1000,
                                     policy.value(QStringLiteral("command_timeout_ms")).toInt(30000),
                                     300000));
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
    auto* timeoutSpin = new QSpinBox(&dlg);
    timeoutSpin->setRange(1000, 300000);
    timeoutSpin->setSingleStep(1000);
    timeoutSpin->setSuffix(QStringLiteral(" ms"));

    optionsForm->addRow(tr("写入范围:"), allowOutsideCheck);
    optionsForm->addRow(tr("执行确认:"), confirmExecCheck);
    optionsForm->addRow(tr("命令超时:"), timeoutSpin);
    layout->addLayout(optionsForm);

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
    loadToEditors(currentPolicy,
                  allowOutsideCheck,
                  confirmExecCheck,
                  timeoutSpin,
                  safeEdit,
                  dangerEdit,
                  writeEdit);

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
        loadToEditors(ShellTool::defaultPolicyObject(),
                      allowOutsideCheck,
                      confirmExecCheck,
                      timeoutSpin,
                      safeEdit,
                      dangerEdit,
                      writeEdit);
    });

    connect(buttons, &QDialogButtonBox::accepted, &dlg, [this,
                                                         allowOutsideCheck,
                                                         confirmExecCheck,
                                                         timeoutSpin,
                                                         safeEdit,
                                                         dangerEdit,
                                                         writeEdit,
                                                         &dlg]() {
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
        raw.insert(QStringLiteral("command_timeout_ms"), timeoutSpin->value());
        raw.insert(QStringLiteral("safe_command_prefixes"), textToArray(safeEdit->toPlainText()));
        raw.insert(QStringLiteral("dangerous_patterns"), textToArray(dangerEdit->toPlainText()));
        raw.insert(QStringLiteral("write_command_prefixes"), textToArray(writeEdit->toPlainText()));

        QString err;
        if (!ShellTool::savePolicyObject(raw, &err)) {
            QMessageBox::warning(this,
                                 tr("保存失败"),
                                 err.isEmpty() ? tr("无法写入命令权限配置。") : err);
            return;
        }

        QMessageBox::information(this,
                                 tr("保存成功"),
                                 tr("命令权限配置已更新，将在下一次工具调用时生效。"));
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

QString inferProviderIdFromBaseUrl(const QString& baseUrl)
{
    const QString u = baseUrl.trimmed().toLower();
    if (u.contains("deepseek"))
        return QStringLiteral("deepseek");
    if (u.contains("openai.com"))
        return QStringLiteral("openai");
    if (u.contains("anthropic"))
        return QStringLiteral("claude");
    if (u.contains("localhost:11434") || u.contains("ollama"))
        return QStringLiteral("ollama");
    if (u.contains("generativelanguage") || u.contains("googleapis"))
        return QStringLiteral("gemini");
    return QString();
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

    ModelConfigProvider claude { "claude", "Claude", "Anthropic 强大的 AI 模型" };
    claude.fields << ModelConfigField { "apiKey", "API 密钥", "sk-ant-...", "", true, true };
    claude.fields << ModelConfigField { "modelId", "模型名称", "claude-sonnet-4-5-20250929", "claude-sonnet-4-5-20250929" };
    claude.fields << ModelConfigField { "baseUrl", "接口地址", "https://api.anthropic.com", "https://api.anthropic.com" };
    list << claude;

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
    dlg->setWindowTitle(tr("从厂商导入模型配置"));
    dlg->resize(720, 480);

    auto* page = new ModelConfigImportPage(dlg);
    page->setProviders(defaultModelConfigProviders());
    page->applyStyleSheet();

    QString yamlPath = m_chatService->modelConfigPath();
    QString defaultModelId = ModelConfigLoader::getDefaultModelId(yamlPath);

    QVariantMap initial;
    if (!defaultModelId.isEmpty()) {
        ModelConfig existingConfig = ModelConfigLoader::getModelConfig(yamlPath, defaultModelId, false);
        QString pid = inferProviderIdFromBaseUrl(existingConfig.baseUrl);
        if (pid.isEmpty())
            pid = QStringLiteral("deepseek");
        initial["providerId"] = pid;
        initial["apiKey"] = existingConfig.apiKey;
        initial["baseUrl"] = existingConfig.baseUrl;
        initial["modelId"] = existingConfig.modelId;
    } else {
        initial["providerId"] = QStringLiteral("deepseek");
    }
    page->setConfigData(initial);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(page);

    connect(page, &ModelConfigImportPage::importRequested, this, [this, dlg, yamlPath](const QVariantMap& config) {
        ModelConfig modelConfig;
        modelConfig.modelId = config.value("modelId").toString().trimmed();
        modelConfig.displayName = config.value("providerName").toString();
        modelConfig.provider = config.value("providerId").toString();
        modelConfig.baseUrl = config.value("baseUrl").toString().trimmed();
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
                    QMessageBox::warning(this, tr("读取失败"), tr("无法从系统密钥库读取：%1").arg(error.isEmpty() ? tr("未知错误") : error));
                    return;
                }
            } else if (isEnvVarReference(apiKeyInput)) {
                apiKeyStored = apiKeyInput;
                QString varName;
                if (extractEnvVarName(apiKeyInput, &varName))
                    apiKeyRuntime = QProcessEnvironment::systemEnvironment().value(varName);
                if (apiKeyRuntime.isEmpty()) {
                    QMessageBox::warning(this, tr("环境变量未设置"), tr("未读取到 %1，请先设置环境变量后再导入。").arg(apiKeyInput));
                    return;
                }
            } else {
                keychainId = KeychainHelper::entryIdForModel(modelConfig.provider, modelConfig.modelId);
                QString error;
                if (!KeychainHelper::writePasswordSync(keychainId, apiKeyInput, &error)) {
                    QMessageBox::warning(this, tr("保存失败"), tr("无法写入系统密钥库：%1").arg(error.isEmpty() ? tr("未知错误") : error));
                    return;
                }
                apiKeyStored = KeychainHelper::makeKeyRef(keychainId);
                apiKeyRuntime = apiKeyInput;
            }
        }
        modelConfig.apiKey = apiKeyRuntime;
        modelConfig.authType = "Bearer";
        modelConfig.temperature = 0.7;
        modelConfig.maxTokens = 4096;
        modelConfig.timeoutMs = 180000;
        modelConfig.capabilities << Capability::TextGeneration << Capability::ToolCalling;
        modelConfig.toolCalling = true;
        modelConfig.systemPrompt = DefaultPrompts::codingAssistantSystemPrompt();

        ModelConfig saveConfig = modelConfig;
        saveConfig.apiKey = apiKeyStored;
        ModelConfigLoader::addOrUpdateModel(yamlPath, saveConfig);
        ModelConfigLoader::setDefaultModelId(yamlPath, modelConfig.modelId);

        m_chatService->modelFactory()->registerModelConfig(modelConfig);

        LLMConfig agentConfig;
        {
            ModelFactory::ParsedModelId parsed = ModelFactory::parseModelKey(modelConfig.modelId);
            agentConfig.model = parsed.model;
            agentConfig.customModelId = parsed.customModelId;
        }
        agentConfig.systemPrompt = modelConfig.systemPrompt;
        agentConfig.userName = tr("TM Agent");
        m_chatService->setDefaultAgentConfig(agentConfig);
        m_chatService->applyConfigToAllRuntimes();

        dlg->accept();
        QMessageBox::information(this, tr("已导入"), tr("已从「%1」导入配置并保存到 %2").arg(config.value("providerName").toString(), QDir::toNativeSeparators(yamlPath)));
    });
    connect(page, &ModelConfigImportPage::cancelled, dlg, &QDialog::reject);

    connect(page, &ModelConfigImportPage::testConnectionRequested, this, [page](const QVariantMap&) {
        page->setTestStatus(ModelConfigImportPage::TestStatus::Testing, QObject::tr("验证中…"));
        QTimer::singleShot(800, page, [page]() {
            page->setTestStatus(ModelConfigImportPage::TestStatus::Success, QObject::tr("可在主界面保存后发送消息验证"));
        });
    });

    connect(page, &ModelConfigImportPage::importFromFileRequested, this, [this, page]() {
        QString path = QFileDialog::getOpenFileName(this, tr("从文件导入配置"), QString(), tr("JSON (*.json)"));
        if (path.isEmpty())
            return;
        QFile f(path);
        if (!f.open(QFile::ReadOnly | QFile::Text)) {
            QMessageBox::warning(this, tr("打开失败"), tr("无法读取文件：%1").arg(path));
            return;
        }
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        f.close();
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, tr("解析失败"), tr("不是有效的 JSON：%1").arg(err.errorString()));
            return;
        }
        page->setConfigData(doc.object().toVariantMap());
    });

    connect(page, &ModelConfigImportPage::exportRequested, this, [this](const QVariantMap& config) {
        QString path = QFileDialog::getSaveFileName(this, tr("导出配置"), QString(), tr("JSON (*.json)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(".json", Qt::CaseInsensitive))
            path.append(".json");
        QFile f(path);
        if (!f.open(QFile::WriteOnly | QFile::Text)) {
            QMessageBox::warning(this, tr("保存失败"), tr("无法写入文件：%1").arg(path));
            return;
        }
        f.write(QJsonDocument(QJsonObject::fromVariantMap(config)).toJson(QJsonDocument::Indented));
        f.close();
        QMessageBox::information(this, tr("已导出"), tr("已保存到 %1").arg(path));
    });

    dlg->exec();
    dlg->deleteLater();
}
