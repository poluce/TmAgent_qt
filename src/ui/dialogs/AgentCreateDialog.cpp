#include "AgentCreateDialog.h"
#include "AvatarUtils.h"
#include "ToolPermissionEditor.h"
#include "core/utils/DefaultPrompts.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

AgentCreateDialog::AgentCreateDialog(const QStringList& configIds, const QString& defaultConfigId, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("创建 Agent"));
    resize(680, 520);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 14, 14, 14);
    mainLayout->setSpacing(10);

    auto* topLayout = new QHBoxLayout();
    topLayout->setSpacing(12);

    auto* avatarColumn = new QVBoxLayout();
    avatarColumn->setContentsMargins(0, 0, 0, 0);
    avatarColumn->setSpacing(6);

    m_avatarButton = new QToolButton(this);
    m_avatarButton->setCursor(Qt::PointingHandCursor);
    m_avatarButton->setFixedSize(104, 104);
    m_avatarButton->setIconSize(QSize(96, 96));
    m_avatarButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_avatarButton->setStyleSheet(
        "QToolButton { border: 1px solid #d1d5db; border-radius: 12px; background: #ffffff; padding: 4px; }"
        "QToolButton:hover { border-color: #93c5fd; }");
    connect(m_avatarButton, &QToolButton::clicked, this, &AgentCreateDialog::chooseAvatar);
    avatarColumn->addWidget(m_avatarButton, 0, Qt::AlignHCenter);

    auto* avatarHint = new QLabel(tr("点击头像导入"), this);
    avatarHint->setAlignment(Qt::AlignHCenter);
    avatarHint->setStyleSheet("color: #6b7280; font-size: 11px;");
    avatarColumn->addWidget(avatarHint);
    avatarColumn->addStretch(1);
    topLayout->addLayout(avatarColumn, 0);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("例如：代码助手"));
    form->addRow(tr("名字:"), m_nameEdit);

    m_roleCombo = new QComboBox(this);
    m_roleCombo->setEditable(true);
    if (QLineEdit* roleEdit = m_roleCombo->lineEdit())
        roleEdit->setPlaceholderText(tr("例如：后端工程师 / 测试负责人"));
    form->addRow(tr("岗位:"), m_roleCombo);

    m_modelCombo = new QComboBox(this);
    m_modelCombo->setEditable(true);
    m_modelCombo->addItem(tr("跟随系统默认模型"), QString());
    QStringList uniqueConfigIds = configIds;
    uniqueConfigIds.removeDuplicates();
    for (const QString& cid : uniqueConfigIds) {
        const QString trimmed = cid.trimmed();
        if (!trimmed.isEmpty())
            m_modelCombo->addItem(trimmed, trimmed);
    }
    int defaultIndex = m_modelCombo->findData(defaultConfigId.trimmed());
    if (defaultIndex >= 0) {
        m_modelCombo->setCurrentIndex(defaultIndex);
    } else if (!defaultConfigId.trimmed().isEmpty()) {
        m_modelCombo->setCurrentText(defaultConfigId.trimmed());
    } else {
        m_modelCombo->setCurrentIndex(0);
    }
    m_modelCombo->setVisible(false); // 旧控件隐藏，保留兼容

    // 新路径：接入点 + 模型 两级选择
    m_providerCombo = new QComboBox(this);
    m_providerCombo->addItem(tr("跟随系统默认"), QString());
    form->addRow(tr("接入点:"), m_providerCombo);

    m_modelSelectCombo = new QComboBox(this);
    m_modelSelectCombo->setEditable(true);
    form->addRow(tr("模型:"), m_modelSelectCombo);

    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const QString instId = providerInstanceId();
        // 从缓存刷新模型列表
        m_modelSelectCombo->clear();
        const QList<ModelEntry>& models = m_modelEntriesCache.value(instId);
        for (const ModelEntry& m : models) {
            const QString display = m.displayName.isEmpty() ? m.modelId : m.displayName;
            m_modelSelectCombo->addItem(display, m.modelId);
        }
        if (m_modelSelectCombo->count() > 0)
            m_modelSelectCombo->setCurrentIndex(0);
        emit providerChanged(instId);
    });

    m_personalityCombo = new QComboBox(this);
    m_personalityCombo->setEditable(true);
    if (QLineEdit* personalityEdit = m_personalityCombo->lineEdit())
        personalityEdit->setPlaceholderText(tr("例如：稳健严谨 / 果断执行"));
    form->addRow(tr("性格:"), m_personalityCombo);

    m_delegateCheck = new QCheckBox(tr("启用团队协作（teammate）"), this);
    m_delegateCheck->setChecked(true);
    m_delegateCheck->setToolTip(tr("默认开启。关闭后该助手不会调用队友协作相关工具。"));
    form->addRow(tr("团队协作:"), m_delegateCheck);

    m_executionModeCombo = new QComboBox(this);
    m_executionModeCombo->addItem(tr("连续执行"), DefaultPrompts::executionModeContinuous());
    m_executionModeCombo->addItem(tr("规划模式"), DefaultPrompts::executionModePlanFirst());
    m_executionModeCombo->setToolTip(tr("连续执行会默认在同一回合内持续推进；规划模式会先给方案。"));
    form->addRow(tr("执行模式:"), m_executionModeCombo);

    auto* promptTemplateRow = new QWidget(this);
    auto* promptTemplateLayout = new QHBoxLayout(promptTemplateRow);
    promptTemplateLayout->setContentsMargins(0, 0, 0, 0);
    promptTemplateLayout->setSpacing(6);
    m_promptTemplateCombo = new QComboBox(promptTemplateRow);
    promptTemplateLayout->addWidget(m_promptTemplateCombo, 1);
    m_applyPromptBtn = new QPushButton(tr("套用"), promptTemplateRow);
    m_applyPromptBtn->setToolTip(tr("用当前岗位 + 性格 + 模板覆盖系统提示词"));
    promptTemplateLayout->addWidget(m_applyPromptBtn, 0);
    form->addRow(tr("角色模板:"), promptTemplateRow);

    topLayout->addLayout(form, 1);
    mainLayout->addLayout(topLayout);

    mainLayout->addWidget(new QLabel(tr("工具权限:"), this));
    m_toolPermissionEditor = new ToolPermissionEditor(this);
    m_toolPermissionEditor->setMinimumHeight(220);
    mainLayout->addWidget(m_toolPermissionEditor, 0);

    mainLayout->addWidget(new QLabel(tr("系统提示词（可选）:"), this));
    m_promptEdit = new QPlainTextEdit(this);
    m_promptEdit->setPlaceholderText(tr("可手工编辑，也可通过上面的模板自动生成"));
    mainLayout->addWidget(m_promptEdit, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttons);

    loadPresetConfig();
    applyRolePreset(m_roleCombo->currentData().toString().trimmed());
    refreshAvatarPreview();
    applyPromptComposition(true);

    connect(m_roleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        applyRolePreset(m_roleCombo->currentData().toString().trimmed());
    });
    connect(m_roleCombo, &QComboBox::currentTextChanged, this, [this]() {
        refreshAvatarPreview();
        applyPromptComposition(false);
    });
    connect(m_personalityCombo, &QComboBox::currentTextChanged, this, [this]() {
        applyPromptComposition(false);
    });
    connect(m_executionModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        Q_UNUSED(index);
        applyPromptComposition(false);
    });
    connect(m_promptTemplateCombo, &QComboBox::currentTextChanged, this, [this]() {
        applyPromptComposition(false);
    });
    connect(m_applyPromptBtn, &QPushButton::clicked, this, [this]() {
        applyPromptComposition(true);
    });
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() {
        refreshAvatarPreview();
        applyPromptComposition(false);
    });
    connect(m_promptEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_updatingPrompt)
            m_promptEdited = true;
    });

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (agentName().isEmpty()) {
            QMessageBox::warning(this, tr("输入不完整"), tr("请先填写 Agent 名字。"));
            m_nameEdit->setFocus();
            return;
        }
        if (roleName().isEmpty()) {
            QMessageBox::warning(this, tr("输入不完整"), tr("请先填写岗位名。"));
            m_roleCombo->setFocus();
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString AgentCreateDialog::agentName() const
{
    return m_nameEdit->text().trimmed();
}

QString AgentCreateDialog::roleName() const
{
    return m_roleCombo->currentText().trimmed();
}

QString AgentCreateDialog::avatarPath() const
{
    return m_avatarPath.trimmed();
}

QString AgentCreateDialog::configId() const
{
    // 新路径优先
    const QString instId = providerInstanceId();
    if (!instId.isEmpty())
        return instId;

    const int index = m_modelCombo->currentIndex();
    const QString data = m_modelCombo->itemData(index).toString().trimmed();
    if (!data.isEmpty())
        return data;

    const QString text = m_modelCombo->currentText().trimmed();
    if (index == 0 && text == m_modelCombo->itemText(0))
        return QString();
    return text;
}

QString AgentCreateDialog::providerInstanceId() const
{
    return m_providerCombo->currentData().toString().trimmed();
}

QString AgentCreateDialog::selectedModelId() const
{
    return m_modelSelectCombo->currentData().toString().trimmed();
}

void AgentCreateDialog::setProviderEntries(const QList<ProviderEntry>& entries, const QString& defaultInstanceId)
{
    m_providerCombo->blockSignals(true);
    m_providerCombo->clear();
    m_providerCombo->addItem(tr("跟随系统默认"), QString());
    for (const ProviderEntry& e : entries) {
        const QString display = e.displayName.isEmpty() ? e.instanceId : e.displayName;
        m_providerCombo->addItem(display, e.instanceId);
    }
    int idx = m_providerCombo->findData(defaultInstanceId.trimmed());
    if (idx >= 0)
        m_providerCombo->setCurrentIndex(idx);
    m_providerCombo->blockSignals(false);

    // 触发一次模型列表刷新
    emit providerChanged(providerInstanceId());
}

void AgentCreateDialog::setModelEntries(const QString& instanceId, const QList<ModelEntry>& models, const QString& defaultModelId)
{
    m_modelEntriesCache.insert(instanceId, models);

    // 如果当前选中的就是这个 instanceId，刷新模型下拉
    if (providerInstanceId() == instanceId) {
        m_modelSelectCombo->blockSignals(true);
        m_modelSelectCombo->clear();
        for (const ModelEntry& m : models) {
            const QString display = m.displayName.isEmpty() ? m.modelId : m.displayName;
            m_modelSelectCombo->addItem(display, m.modelId);
        }
        int idx = m_modelSelectCombo->findData(defaultModelId.trimmed());
        if (idx >= 0)
            m_modelSelectCombo->setCurrentIndex(idx);
        else if (m_modelSelectCombo->count() > 0)
            m_modelSelectCombo->setCurrentIndex(0);
        m_modelSelectCombo->blockSignals(false);
    }
}

QString AgentCreateDialog::systemPrompt() const
{
    return m_promptEdit->toPlainText().trimmed();
}

QString AgentCreateDialog::executionMode() const
{
    return DefaultPrompts::normalizeExecutionMode(m_executionModeCombo->currentData().toString());
}

void AgentCreateDialog::setExecutionMode(const QString& mode)
{
    const QString normalized = DefaultPrompts::normalizeExecutionMode(mode);
    const int index = m_executionModeCombo->findData(normalized);
    if (index >= 0)
        m_executionModeCombo->setCurrentIndex(index);
}

bool AgentCreateDialog::delegationEnabled() const
{
    return m_delegateCheck->isChecked();
}

QStringList AgentCreateDialog::allowedTools() const
{
    return m_toolPermissionEditor->selectedTools();
}

void AgentCreateDialog::setToolPluginInfos(const QList<ToolPluginInfo>& infos,
                                           const QStringList& selectedTools)
{
    m_toolPermissionEditor->setToolPlugins(infos);
    if (!selectedTools.isEmpty())
        m_toolPermissionEditor->setSelectedTools(selectedTools);
    else {
        QStringList defaults;
        for (const ToolPluginInfo& info : infos) {
            const bool available = info.externalProvider || (info.loaded && info.enabled);
            if (!available)
                continue;
            defaults.append(info.descriptor.toolNames);
        }
        defaults.removeDuplicates();
        m_toolPermissionEditor->setSelectedTools(defaults);
    }
}

void AgentCreateDialog::loadPresetConfig()
{
    m_promptTemplates.clear();
    m_personalities.clear();
    m_rolePresets.clear();

    m_roleCombo->clear();
    m_promptTemplateCombo->clear();
    m_personalityCombo->clear();

    QStringList candidates;
    candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/resources/agent_presets.json");
    candidates << QDir::currentPath() + QStringLiteral("/resources/agent_presets.json");

    QJsonObject root;
    QString configPath;
    for (const QString& candidate : candidates) {
        QFile file(candidate);
        if (!file.exists())
            continue;
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
        file.close();
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        root = doc.object();
        configPath = candidate;
        break;
    }

    if (!configPath.isEmpty())
        m_configDir = QFileInfo(configPath).absolutePath();
    else
        m_configDir = QCoreApplication::applicationDirPath() + QStringLiteral("/resources");

    auto addPromptTemplate = [this](const QString& id, const QString& name, const QString& content) {
        const QString key = id.trimmed();
        if (key.isEmpty())
            return;
        PromptTemplateItem item;
        item.id = key;
        item.name = name.trimmed().isEmpty() ? key : name.trimmed();
        item.content = content.trimmed();
        m_promptTemplates.insert(key, item);
        m_promptTemplateCombo->addItem(item.name, key);
    };

    auto addPersonality = [this](const QString& id, const QString& name, const QString& instruction) {
        const QString key = id.trimmed();
        if (key.isEmpty())
            return;
        PersonalityItem item;
        item.id = key;
        item.name = name.trimmed().isEmpty() ? key : name.trimmed();
        item.instruction = instruction.trimmed();
        m_personalities.insert(key, item);
        m_personalityCombo->addItem(item.name, key);
    };

    auto addRole = [this](const RolePresetItem& role) {
        if (role.id.trimmed().isEmpty())
            return;
        m_rolePresets.insert(role.id, role);
        const QString display = !role.title.trimmed().isEmpty()
            ? role.title.trimmed()
            : (role.name.trimmed().isEmpty() ? role.id : role.name.trimmed());
        m_roleCombo->addItem(display, role.id);
    };

    m_promptTemplateCombo->addItem(tr("自动生成（不选模板）"), QString());

    const QJsonArray promptTemplates = root.value(QStringLiteral("prompt_templates")).toArray();
    for (const QJsonValue& value : promptTemplates) {
        const QJsonObject obj = value.toObject();
        addPromptTemplate(obj.value(QStringLiteral("id")).toString(), obj.value(QStringLiteral("name")).toString(), obj.value(QStringLiteral("content")).toString());
    }
    if (m_promptTemplates.isEmpty()) {
        addPromptTemplate(QStringLiteral("coding_general"), tr("通用研发助手"), DefaultPrompts::codingAssistantSystemPrompt());
        addPromptTemplate(
            QStringLiteral("test_engineer"),
            tr("测试工程师"),
            QStringLiteral("你是一名资深测试工程师，擅长从需求中提炼测试点，输出测试计划、边界条件、异常路径和可执行测试用例。"));
        addPromptTemplate(
            QStringLiteral("code_reviewer"),
            tr("代码评审专家"),
            QStringLiteral("你是一名代码评审专家，重点识别功能缺陷、稳定性风险、安全风险与回归风险，给出可执行修改建议。"));
    }

    const QJsonArray personalities = root.value(QStringLiteral("personalities")).toArray();
    for (const QJsonValue& value : personalities) {
        const QJsonObject obj = value.toObject();
        addPersonality(obj.value(QStringLiteral("id")).toString(), obj.value(QStringLiteral("name")).toString(), obj.value(QStringLiteral("instruction")).toString());
    }
    if (m_personalities.isEmpty()) {
        addPersonality(QStringLiteral("steady"), tr("稳健严谨"), tr("表达客观严谨，先校验约束，再给结论，避免拍脑袋。"));
        addPersonality(QStringLiteral("concise"), tr("简洁高效"), tr("聚焦关键结论，减少冗余解释，优先给可执行步骤。"));
        addPersonality(QStringLiteral("proactive"), tr("积极推进"), tr("主动识别风险和下一步动作，必要时给出备选方案。"));
    }

    const QJsonArray roles = root.value(QStringLiteral("roles")).toArray();
    for (const QJsonValue& value : roles) {
        const QJsonObject obj = value.toObject();
        RolePresetItem role;
        role.id = obj.value(QStringLiteral("id")).toString().trimmed();
        role.name = obj.value(QStringLiteral("name")).toString().trimmed();
        role.title = obj.value(QStringLiteral("title")).toString().trimmed();
        role.suggestedName = obj.value(QStringLiteral("suggested_name")).toString().trimmed();
        role.defaultPromptId = obj.value(QStringLiteral("default_prompt_id")).toString().trimmed();
        role.defaultPersonalityId = obj.value(QStringLiteral("default_personality_id")).toString().trimmed();
        role.avatarPath = obj.value(QStringLiteral("avatar")).toString().trimmed();
        addRole(role);
    }
    if (m_rolePresets.isEmpty()) {
        RolePresetItem backend;
        backend.id = QStringLiteral("backend_engineer");
        backend.name = tr("后端工程师");
        backend.title = tr("后端工程师");
        backend.suggestedName = tr("后端助手");
        backend.defaultPromptId = QStringLiteral("coding_general");
        backend.defaultPersonalityId = QStringLiteral("steady");
        addRole(backend);

        RolePresetItem qa;
        qa.id = QStringLiteral("qa_engineer");
        qa.name = tr("测试工程师");
        qa.title = tr("测试工程师");
        qa.suggestedName = tr("测试助手");
        qa.defaultPromptId = QStringLiteral("test_engineer");
        qa.defaultPersonalityId = QStringLiteral("steady");
        addRole(qa);

        RolePresetItem reviewer;
        reviewer.id = QStringLiteral("code_reviewer");
        reviewer.name = tr("代码评审");
        reviewer.title = tr("代码评审专家");
        reviewer.suggestedName = tr("评审助手");
        reviewer.defaultPromptId = QStringLiteral("code_reviewer");
        reviewer.defaultPersonalityId = QStringLiteral("concise");
        addRole(reviewer);
    }

    QString defaultPromptId = root.value(QStringLiteral("default_prompt_id")).toString().trimmed();
    QString defaultPersonalityId = root.value(QStringLiteral("default_personality_id")).toString().trimmed();
    QString defaultRoleId = root.value(QStringLiteral("default_role_id")).toString().trimmed();

    int idx = m_promptTemplateCombo->findData(defaultPromptId);
    if (idx < 0 && m_promptTemplateCombo->count() > 1)
        idx = 1;
    if (idx >= 0)
        m_promptTemplateCombo->setCurrentIndex(idx);

    idx = m_personalityCombo->findData(defaultPersonalityId);
    if (idx < 0 && m_personalityCombo->count() > 0)
        idx = 0;
    if (idx >= 0)
        m_personalityCombo->setCurrentIndex(idx);

    idx = m_roleCombo->findData(defaultRoleId);
    if (idx >= 0)
        m_roleCombo->setCurrentIndex(idx);
}

void AgentCreateDialog::applyRolePreset(const QString& roleId)
{
    if (roleId.trimmed().isEmpty()) {
        if (!m_avatarCustom)
            m_avatarPath.clear();
        refreshAvatarPreview();
        applyPromptComposition(false);
        return;
    }
    const RolePresetItem role = m_rolePresets.value(roleId);
    if (role.id.isEmpty())
        return;

    if (m_nameEdit->text().trimmed().isEmpty() && !role.suggestedName.isEmpty())
        m_nameEdit->setText(role.suggestedName);
    if (m_roleCombo->isEditable()) {
        const QString roleText = !role.title.trimmed().isEmpty() ? role.title.trimmed() : role.name.trimmed();
        if (!roleText.isEmpty())
            m_roleCombo->setEditText(roleText);
    }

    if (!role.defaultPromptId.isEmpty()) {
        const int promptIdx = m_promptTemplateCombo->findData(role.defaultPromptId);
        if (promptIdx >= 0)
            m_promptTemplateCombo->setCurrentIndex(promptIdx);
    }
    if (!role.defaultPersonalityId.isEmpty()) {
        const int personalityIdx = m_personalityCombo->findData(role.defaultPersonalityId);
        if (personalityIdx >= 0)
            m_personalityCombo->setCurrentIndex(personalityIdx);
    }

    if (!m_avatarCustom) {
        m_avatarPath = resolveResourcePath(role.avatarPath);
        refreshAvatarPreview();
    }
    applyPromptComposition(false);
}

void AgentCreateDialog::refreshAvatarPreview()
{
    QPixmap avatar;
    if (!m_avatarPath.trimmed().isEmpty()) {
        QPixmap src(m_avatarPath);
        if (!src.isNull())
            avatar = src.scaled(96, 96, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }

    if (avatar.isNull()) {
        QString seed = agentName();
        if (seed.isEmpty())
            seed = roleName();
        avatar = AvatarUtils::makeFallbackAvatar(seed, 96);
    }

    if (!avatar.isNull()) {
        QPixmap clipped(96, 96);
        clipped.fill(Qt::transparent);
        QPainter painter(&clipped);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addRoundedRect(QRectF(0, 0, 96, 96), 12, 12);
        painter.setClipPath(path);
        painter.drawPixmap(0, 0, avatar);
        m_avatarButton->setIcon(QIcon(clipped));
    } else {
        m_avatarButton->setIcon(QIcon());
    }

    if (m_avatarPath.trimmed().isEmpty())
        m_avatarButton->setToolTip(tr("默认头像（点击可导入自定义头像）"));
    else
        m_avatarButton->setToolTip(QDir::toNativeSeparators(m_avatarPath));
}

QString AgentCreateDialog::composePrompt() const
{
    const QString templateId = m_promptTemplateCombo->currentData().toString().trimmed();
    QString prompt = m_promptTemplates.value(templateId).content;
    if (prompt.isEmpty())
        prompt = DefaultPrompts::codingAssistantSystemPrompt(executionMode());

    const QString name = agentName();
    const QString role = roleName();
    QString personalityInstruction;
    const QString personalityId = m_personalityCombo->currentData().toString().trimmed();
    personalityInstruction = m_personalities.value(personalityId).instruction;
    if (personalityInstruction.isEmpty())
        personalityInstruction = m_personalityCombo->currentText().trimmed();

    const bool hasRolePlaceholder = prompt.contains(QStringLiteral("{{role}}"));
    const bool hasPersonalityPlaceholder = prompt.contains(QStringLiteral("{{personality}}"));
    const bool hasNamePlaceholder = prompt.contains(QStringLiteral("{{name}}"));

    prompt.replace(QStringLiteral("{{name}}"), name);
    prompt.replace(QStringLiteral("{{role}}"), role);
    prompt.replace(QStringLiteral("{{personality}}"), personalityInstruction);

    if (!hasRolePlaceholder && !role.isEmpty())
        prompt += QStringLiteral("\n\n岗位定位：%1").arg(role);
    if (!hasPersonalityPlaceholder && !personalityInstruction.isEmpty())
        prompt += QStringLiteral("\n\n行为风格：%1").arg(personalityInstruction);
    if (!hasNamePlaceholder && !name.isEmpty())
        prompt += QStringLiteral("\n\n对外称呼：%1").arg(name);

    return DefaultPrompts::ensureExecutionDiscipline(prompt.trimmed(), executionMode());
}

void AgentCreateDialog::applyPromptComposition(bool forceOverwrite)
{
    if (!forceOverwrite && m_promptEdited)
        return;

    m_updatingPrompt = true;
    m_promptEdit->setPlainText(composePrompt());
    m_updatingPrompt = false;
    m_promptEdited = false;
}

QString AgentCreateDialog::resolveResourcePath(const QString& maybeRelativePath) const
{
    const QString path = maybeRelativePath.trimmed();
    if (path.isEmpty())
        return QString();

    QFileInfo info(path);
    if (info.isAbsolute())
        return info.exists() ? info.absoluteFilePath() : QString();

    const QStringList bases = {
        m_configDir,
        QCoreApplication::applicationDirPath(),
        QCoreApplication::applicationDirPath() + QStringLiteral("/resources"),
        QDir::currentPath(),
        QDir::currentPath() + QStringLiteral("/resources"),
    };

    for (const QString& base : bases) {
        if (base.trimmed().isEmpty())
            continue;
        const QString candidate = QDir(base).filePath(path);
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }
    return QString();
}

void AgentCreateDialog::chooseAvatar()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("选择头像"),
        QString(),
        tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp *.svg)"));
    if (path.isEmpty())
        return;
    m_avatarCustom = true;
    m_avatarPath = path;
    refreshAvatarPreview();
}
