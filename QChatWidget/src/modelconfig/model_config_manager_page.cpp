#include "model_config_manager_page.h"
#include "qss_utils.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

// ===========================================================================
// 构造 / 样式
// ===========================================================================

ModelConfigManagerPage::ModelConfigManagerPage(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    applyStyleSheet();
}

void ModelConfigManagerPage::applyStyleSheet(const QString& styleSheet)
{
    const QString combined = QssUtils::buildCombinedStyleSheet(
        "model_config_manager_page.qss", styleSheet);
    setStyleSheet(combined);
}

void ModelConfigManagerPage::setConfigListLoader(const ConfigListLoader& loader)
{
    m_configListLoader = loader;
}

void ModelConfigManagerPage::setSingleConfigLoader(const SingleConfigLoader& loader)
{
    m_singleConfigLoader = loader;
}

void ModelConfigManagerPage::setDefaultConfigIdLoader(const DefaultConfigIdLoader& loader)
{
    m_defaultConfigIdLoader = loader;
}

void ModelConfigManagerPage::setProviderTagInferrer(const ProviderTagInferrer& inferrer)
{
    m_providerTagInferrer = inferrer;
}

void ModelConfigManagerPage::setProviderAliasResolver(const ProviderAliasResolver& resolver)
{
    m_providerAliasResolver = resolver;
}

void ModelConfigManagerPage::setConfigIdGenerator(const ConfigIdGenerator& generator)
{
    m_configIdGenerator = generator;
}

// ===========================================================================
// 公共接口
// ===========================================================================

void ModelConfigManagerPage::setProviders(const QList<ModelConfigProvider>& providers)
{
    m_providers = providers;

    // 清理旧的厂商按钮
    const QList<QAbstractButton*> oldBtns = m_providerGroup->buttons();
    for (auto* btn : oldBtns) {
        m_providerGroup->removeButton(btn);
        btn->deleteLater();
    }

    // 清理旧的表单页
    while (m_formStack->count() > 0) {
        QWidget* w = m_formStack->widget(0);
        m_formStack->removeWidget(w);
        w->deleteLater();
    }
    m_fieldWidgetsMap.clear();

    // 找到厂商按钮的父 layout
    QWidget* providerBar = m_providerGroup->parent()
        ? qobject_cast<QWidget*>(m_providerGroup->parent())
        : nullptr;
    QHBoxLayout* barLayout = providerBar
        ? qobject_cast<QHBoxLayout*>(providerBar->layout())
        : nullptr;

    for (int i = 0; i < providers.size(); ++i) {
        const auto& p = providers[i];

        // 厂商 pill 按钮
        auto* btn = new QPushButton(p.name);
        btn->setObjectName("providerPill");
        btn->setCheckable(true);
        btn->setProperty("providerId", p.id);
        m_providerGroup->addButton(btn, i);
        if (barLayout) {
            // 插入到 stretch 之前
            barLayout->insertWidget(barLayout->count() - 1, btn);
        }

        // 表单页
        m_formStack->addWidget(createFormWidget(p));
    }

    if (!providers.isEmpty()) {
        m_providerGroup->button(0)->setChecked(true);
        m_formStack->setCurrentIndex(0);
    }
}

void ModelConfigManagerPage::setYamlPath(const QString& yamlPath)
{
    m_yamlPath = yamlPath;
}

void ModelConfigManagerPage::refreshConfigList()
{
    if (m_yamlPath.isEmpty())
        return;

    // 记住当前选中
    QString selectedConfigId;
    if (m_configList->currentItem())
        selectedConfigId = m_configList->currentItem()->data(Qt::UserRole).toString();

    m_defaultConfigId = m_defaultConfigIdLoader ? m_defaultConfigIdLoader(m_yamlPath) : QString();
    const QList<ModelConfigEntry> configs = m_configListLoader
        ? m_configListLoader(m_yamlPath)
        : QList<ModelConfigEntry>();

    m_configList->clear();

    int restoreRow = -1;
    for (int i = 0; i < configs.size(); ++i) {
        const auto& cfg = configs[i];
        const bool isDefault = (cfg.configId == m_defaultConfigId);
        const QString tag = inferProviderTag(cfg.providerId, cfg.baseUrl);
        const QString display = cfg.displayName.isEmpty() ? cfg.configId : cfg.displayName;

        auto* item = new QListWidgetItem();
        item->setData(Qt::UserRole, cfg.configId);
        item->setSizeHint(QSize(0, 52));
        m_configList->addItem(item);
        m_configList->setItemWidget(item, createConfigItemWidget(display, tag, isDefault, cfg.enabled));

        if (cfg.configId == selectedConfigId)
            restoreRow = i;
    }

    if (restoreRow >= 0)
        m_configList->setCurrentRow(restoreRow);

    // 更新按钮状态
    const bool hasSelection = m_configList->currentItem() != nullptr;
    m_setDefaultBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);
}

void ModelConfigManagerPage::setTestStatus(TestStatus status, const QString& message)
{
    if (!m_testStatusLabel)
        return;
    QString text;
    QString statusKey;
    switch (status) {
    case TestStatus::Idle:
        text = tr("未验证");
        statusKey = "idle";
        break;
    case TestStatus::Testing:
        text = tr("验证中...");
        statusKey = "testing";
        break;
    case TestStatus::Success:
        text = tr("验证成功");
        statusKey = "success";
        break;
    case TestStatus::Failed:
        text = tr("验证失败");
        statusKey = "failed";
        break;
    }
    if (!message.trimmed().isEmpty())
        text += tr("：") + message;
    m_testStatusLabel->setText(text);
    m_testStatusLabel->setProperty("status", statusKey);
    m_testStatusLabel->style()->unpolish(m_testStatusLabel);
    m_testStatusLabel->style()->polish(m_testStatusLabel);
}

void ModelConfigManagerPage::setFieldError(const QString& fieldKey, const QString& message)
{
    setFieldError(QString(), fieldKey, message);
}

void ModelConfigManagerPage::setFieldError(const QString& providerId, const QString& fieldKey, const QString& message)
{
    int index = providerId.isEmpty() ? m_formStack->currentIndex() : providerIndexForId(providerId);
    if (index < 0 || !m_fieldWidgetsMap.contains(index))
        return;
    auto& widgets = m_fieldWidgetsMap[index];
    QLabel* label = widgets.errors.value(fieldKey, nullptr);
    if (!label)
        return;
    label->setText(message);
    label->setVisible(!message.trimmed().isEmpty());
}

void ModelConfigManagerPage::setFieldOptions(const QString& providerId, const QString& fieldKey, const QStringList& options, bool editable)
{
    const int index = providerId.isEmpty() ? m_formStack->currentIndex() : providerIndexForId(providerId);
    if (index < 0 || !m_fieldWidgetsMap.contains(index))
        return;

    auto& widgets = m_fieldWidgetsMap[index];
    QComboBox* combo = widgets.combos.value(fieldKey, nullptr);
    if (!combo)
        return;

    QStringList normalized;
    normalized.reserve(options.size());
    for (const QString& item : options) {
        const QString trimmed = item.trimmed();
        if (!trimmed.isEmpty() && !normalized.contains(trimmed))
            normalized.append(trimmed);
    }

    const QString current = combo->currentText().trimmed();
    const QSignalBlocker blocker(combo);
    combo->setEditable(editable);
    combo->clear();
    for (const QString& item : normalized)
        combo->addItem(item);

    if (!current.isEmpty()) {
        int ci = combo->findText(current);
        if (ci < 0) {
            combo->addItem(current);
            ci = combo->count() - 1;
        }
        combo->setCurrentIndex(ci);
    } else if (combo->count() > 0) {
        combo->setCurrentIndex(0);
    }
}

void ModelConfigManagerPage::clearFieldErrors()
{
    int index = m_formStack->currentIndex();
    if (index < 0 || !m_fieldWidgetsMap.contains(index))
        return;
    auto& widgets = m_fieldWidgetsMap[index];
    for (auto it = widgets.errors.begin(); it != widgets.errors.end(); ++it) {
        it.value()->clear();
        it.value()->setVisible(false);
    }
}

// ===========================================================================
// setupUi
// ===========================================================================

void ModelConfigManagerPage::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(createLeftPanel());
    splitter->addWidget(createRightPanel());
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setHandleWidth(1);

    mainLayout->addWidget(splitter);
}

// ===========================================================================
// 左侧面板
// ===========================================================================

QWidget* ModelConfigManagerPage::createLeftPanel()
{
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(10, 10, 0, 10);

    auto* titleLabel = new QLabel(tr("已导入模型实例"), container);
    titleLabel->setObjectName("panelTitle");
    layout->addWidget(titleLabel);

    m_configList = new QListWidget(container);
    m_configList->setObjectName("configList");
    m_configList->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_configList->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    layout->addWidget(m_configList);

    // 底部按钮行
    auto* btnLayout = new QHBoxLayout();
    m_newConfigBtn = new QPushButton(tr("新建"), container);
    m_newConfigBtn->setObjectName("newConfigBtn");
    m_setDefaultBtn = new QPushButton(tr("设为默认"), container);
    m_setDefaultBtn->setObjectName("setDefaultBtn");
    m_setDefaultBtn->setEnabled(false);
    m_deleteBtn = new QPushButton(tr("删除"), container);
    m_deleteBtn->setObjectName("deleteBtn");
    m_deleteBtn->setEnabled(false);

    btnLayout->addWidget(m_newConfigBtn);
    btnLayout->addWidget(m_setDefaultBtn);
    btnLayout->addWidget(m_deleteBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    // 连接信号
    connect(m_configList, &QListWidget::itemClicked, this, &ModelConfigManagerPage::onConfigListItemClicked);
    connect(m_newConfigBtn, &QPushButton::clicked, this, &ModelConfigManagerPage::onNewConfigClicked);
    connect(m_setDefaultBtn, &QPushButton::clicked, this, &ModelConfigManagerPage::onSetDefaultClicked);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ModelConfigManagerPage::onDeleteClicked);

    return container;
}

// ===========================================================================
// 右侧面板
// ===========================================================================

QWidget* ModelConfigManagerPage::createRightPanel()
{
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(10, 10, 10, 10);

    // 厂商 / 消息格式 标题
    auto* providerTitle = new QLabel(tr("厂商 / 消息格式"), container);
    providerTitle->setObjectName("panelTitle");
    layout->addWidget(providerTitle);

    // 厂商 pill 按钮行
    auto* providerBar = new QWidget(container);
    providerBar->setObjectName("providerBar");
    auto* barLayout = new QHBoxLayout(providerBar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(6);
    barLayout->addStretch();

    m_providerGroup = new QButtonGroup(providerBar);
    m_providerGroup->setExclusive(true);
    layout->addWidget(providerBar);

    // 分割线
    auto* separator = new QWidget(container);
    separator->setObjectName("separator");
    separator->setFixedHeight(1);
    layout->addWidget(separator);

    auto* displayNameRow = new QHBoxLayout();
    auto* displayNameLabel = new QLabel(tr("名称:"), container);
    m_displayNameEdit = new QLineEdit(container);
    m_displayNameEdit->setObjectName("displayNameEdit");
    m_displayNameEdit->setPlaceholderText(tr("例如：CC MiniMax 21（用于列表展示）"));
    displayNameRow->addWidget(displayNameLabel);
    displayNameRow->addWidget(m_displayNameEdit);
    layout->addLayout(displayNameRow);

    m_configIdEdit = new QLineEdit(container);
    m_configIdEdit->setObjectName("configIdEdit");
    m_configIdEdit->hide();

    // 表单 stack
    m_formStack = new QStackedWidget(container);
    m_formStack->setObjectName("detailStack");
    layout->addWidget(m_formStack, 1);

    // 底部按钮行
    m_testStatusLabel = new QLabel(tr("未验证"), container);
    m_testStatusLabel->setObjectName("testStatusLabel");
    setTestStatus(TestStatus::Idle);

    m_testBtn = new QPushButton(tr("验证连接"), container);
    m_testBtn->setObjectName("testBtn");
    m_saveBtn = new QPushButton(tr("导入并保存"), container);
    m_saveBtn->setObjectName("saveBtn");

    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->addWidget(m_testStatusLabel);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_testBtn);
    bottomLayout->addWidget(m_saveBtn);
    layout->addLayout(bottomLayout);

    // 连接信号
    connect(m_providerGroup, QOverload<int>::of(&QButtonGroup::buttonClicked), this, &ModelConfigManagerPage::onProviderButtonClicked);
    connect(m_saveBtn, &QPushButton::clicked, this, &ModelConfigManagerPage::onSaveClicked);
    connect(m_testBtn, &QPushButton::clicked, this, &ModelConfigManagerPage::onTestConnectionClicked);
    connect(m_displayNameEdit, &QLineEdit::textChanged, this, &ModelConfigManagerPage::autoGenerateConfigId);

    return container;
}

// ===========================================================================
// 表单生成（沿用共享字段布局模式）
// ===========================================================================

QWidget* ModelConfigManagerPage::createFormWidget(const ModelConfigProvider& provider)
{
    auto* container = new QWidget();
    auto* layout = new QFormLayout(container);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->setLabelAlignment(Qt::AlignRight);

    FieldWidgets fieldWidgets;
    fieldWidgets.providerId = provider.id;

    for (const auto& f : provider.fields) {
        if (f.key == QStringLiteral("modelId")) {
            auto* combo = new QComboBox();
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
            if (!f.defaultValue.trimmed().isEmpty())
                combo->addItem(f.defaultValue.trimmed());
            combo->setCurrentText(f.defaultValue);
            if (combo->lineEdit())
                combo->lineEdit()->setPlaceholderText(f.placeholder);
            layout->addRow(f.label + ":", combo);
            fieldWidgets.combos.insert(f.key, combo);

            connect(combo, &QComboBox::currentTextChanged, this, &ModelConfigManagerPage::autoGenerateConfigId);
        } else {
            auto* edit = new QLineEdit();
            if (f.isPassword)
                edit->setEchoMode(QLineEdit::Password);
            edit->setPlaceholderText(f.placeholder);
            edit->setText(f.defaultValue);
            layout->addRow(f.label + ":", edit);
            fieldWidgets.inputs.insert(f.key, edit);
        }

        auto* errorLabel = new QLabel();
        errorLabel->setObjectName("fieldError");
        errorLabel->setVisible(false);
        errorLabel->setWordWrap(true);
        layout->addRow(QString(), errorLabel);
        fieldWidgets.errors.insert(f.key, errorLabel);
    }

    int index = m_formStack->count();
    m_fieldWidgetsMap.insert(index, fieldWidgets);

    return container;
}

// ===========================================================================
// 左侧列表项自定义 Widget
// ===========================================================================

QWidget* ModelConfigManagerPage::createConfigItemWidget(
    const QString& displayName, const QString& providerTag,
    bool isDefault, bool enabled)
{
    auto* widget = new QWidget();
    widget->setObjectName("configItemWidget");
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(0);

    // 第一行：checkbox + 名称
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(4);
    auto* check = new QCheckBox();
    check->setChecked(enabled);
    check->setObjectName("enabledCheck");
    auto* nameLabel = new QLabel(displayName);
    nameLabel->setObjectName("configName");
    topRow->addWidget(check);
    topRow->addWidget(nameLabel);
    topRow->addStretch();
    layout->addLayout(topRow);

    // 第二行：provider 标签 + 默认标记
    auto* bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(6);
    bottomRow->setContentsMargins(22, 0, 0, 0); // 与 checkbox 对齐
    auto* tagLabel = new QLabel(providerTag);
    tagLabel->setObjectName("providerTag");
    bottomRow->addWidget(tagLabel);
    if (isDefault) {
        auto* defaultLabel = new QLabel(QStringLiteral("\u2605") + tr("默认")); // ★默认
        defaultLabel->setObjectName("defaultMark");
        bottomRow->addWidget(defaultLabel);
    }
    bottomRow->addStretch();
    layout->addLayout(bottomRow);

    // checkbox 信号 → enabledToggled
    connect(check, &QCheckBox::toggled, this, [this, widget](bool checked) {
        for (int i = 0; i < m_configList->count(); ++i) {
            auto* item = m_configList->item(i);
            if (m_configList->itemWidget(item) == widget) {
                emit enabledToggled(item->data(Qt::UserRole).toString(), checked);
                break;
            }
        }
    });

    return widget;
}

// ===========================================================================
// Slots
// ===========================================================================

void ModelConfigManagerPage::onConfigListItemClicked(QListWidgetItem* item)
{
    if (!item)
        return;
    const QString configId = item->data(Qt::UserRole).toString();
    switchToEditMode(configId);

    m_setDefaultBtn->setEnabled(true);
    m_deleteBtn->setEnabled(true);
}

void ModelConfigManagerPage::onProviderButtonClicked(int index)
{
    m_formStack->setCurrentIndex(index);
    if (m_formMode == FormMode::CreateNew)
        switchToCreateMode();
}

void ModelConfigManagerPage::onSaveClicked()
{
    QVariantMap config = collectCurrentConfig();
    if (config.isEmpty())
        return;

    // 基础必填验证
    int index = m_formStack->currentIndex();
    if (index >= 0 && index < m_providers.size()) {
        const auto& provider = m_providers[index];
        for (const auto& f : provider.fields) {
            if (f.isRequired && config[f.key].toString().isEmpty()) {
                QMessageBox::warning(this, tr("验证失败"), QString(tr("%1 不能为空")).arg(f.label));
                return;
            }
        }
    }

    if (config.value("configId").toString().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("验证失败"), tr("请填写名称，或先选择模型名称"));
        return;
    }

    emit configSaved(config);
}

void ModelConfigManagerPage::onTestConnectionClicked()
{
    QVariantMap config = collectCurrentConfig();
    if (config.isEmpty())
        return;
    setTestStatus(TestStatus::Testing);
    emit testConnectionRequested(config);
}

void ModelConfigManagerPage::onSetDefaultClicked()
{
    auto* item = m_configList->currentItem();
    if (!item)
        return;
    emit defaultChanged(item->data(Qt::UserRole).toString());
}

void ModelConfigManagerPage::onDeleteClicked()
{
    auto* item = m_configList->currentItem();
    if (!item)
        return;
    const QString configId = item->data(Qt::UserRole).toString();
    if (QMessageBox::question(this, tr("确认删除"), tr("确定要删除配置「%1」吗？").arg(configId), QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    emit configDeleted(configId);
}

void ModelConfigManagerPage::onNewConfigClicked()
{
    m_configList->clearSelection();
    switchToCreateMode();
    m_setDefaultBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);
}

// ===========================================================================
// 模式切换
// ===========================================================================

void ModelConfigManagerPage::switchToCreateMode()
{
    m_formMode = FormMode::CreateNew;
    m_editingConfigId.clear();
    m_configIdEdit->setReadOnly(false);
    m_configIdEdit->clear();
    if (m_displayNameEdit) {
        m_displayNameEdit->clear();
        m_displayNameEdit->setReadOnly(false);
    }
    m_saveBtn->setText(tr("导入并保存"));
    setTestStatus(TestStatus::Idle);
    clearFieldErrors();

    // 清空当前表单，恢复默认值
    int index = m_formStack->currentIndex();
    if (index >= 0 && index < m_providers.size() && m_fieldWidgetsMap.contains(index)) {
        const auto& widgets = m_fieldWidgetsMap[index];
        for (const auto& f : m_providers[index].fields) {
            if (widgets.inputs.contains(f.key))
                widgets.inputs[f.key]->setText(f.defaultValue);
            if (widgets.combos.contains(f.key))
                widgets.combos[f.key]->setCurrentText(f.defaultValue);
        }
    }
    autoGenerateConfigId();
}

void ModelConfigManagerPage::switchToEditMode(const QString& configId)
{
    if (m_yamlPath.isEmpty())
        return;

    ModelConfigEntry cfg;
    if (m_singleConfigLoader)
        cfg = m_singleConfigLoader(m_yamlPath, configId);
    if (cfg.configId.isEmpty())
        return;

    m_formMode = FormMode::EditExisting;
    m_editingConfigId = configId;
    m_configIdEdit->setReadOnly(true);
    m_configIdEdit->setText(configId);
    if (m_displayNameEdit) {
        const QString displayName = cfg.displayName.trimmed().isEmpty() ? cfg.configId : cfg.displayName.trimmed();
        m_displayNameEdit->setText(displayName);
        m_displayNameEdit->setReadOnly(false);
    }
    m_saveBtn->setText(tr("保存修改"));
    setTestStatus(TestStatus::Idle);
    clearFieldErrors();

    // 找到对应的 provider 并选中
    const QString pid = cfg.providerId.toLower();
    int providerIdx = providerIndexForId(pid);
    if (providerIdx < 0)
        providerIdx = 0;

    {
        const QSignalBlocker blocker(m_providerGroup);
        if (auto* btn = m_providerGroup->button(providerIdx))
            btn->setChecked(true);
    }
    m_formStack->setCurrentIndex(providerIdx);

    // 填充表单
    if (m_fieldWidgetsMap.contains(providerIdx)) {
        QHash<QString, QString> values {
            { QStringLiteral("apiKey"), cfg.apiKey },
            { QStringLiteral("baseUrl"), cfg.baseUrl },
            { QStringLiteral("modelId"), cfg.modelId },
        };
        // 合并 extraFields
        for (auto it = cfg.extraFields.constBegin(); it != cfg.extraFields.constEnd(); ++it) {
            values.insert(it.key(), it.value().toString());
        }

        auto& widgets = m_fieldWidgetsMap[providerIdx];

        for (auto it = widgets.inputs.begin(); it != widgets.inputs.end(); ++it) {
            if (values.contains(it.key()))
                it.value()->setText(values.value(it.key()));
        }
        for (auto it = widgets.combos.begin(); it != widgets.combos.end(); ++it) {
            const QString val = values.value(it.key());
            if (val.isEmpty())
                continue;
            if (it.value()->findText(val) < 0)
                it.value()->addItem(val.trimmed());
            it.value()->setCurrentText(val);
        }
    }
}

// ===========================================================================
// 辅助方法
// ===========================================================================

QVariantMap ModelConfigManagerPage::collectCurrentConfig() const
{
    int index = m_formStack->currentIndex();
    if (index < 0 || index >= m_providers.size())
        return QVariantMap();

    const auto& provider = m_providers[index];
    const auto& widgets = m_fieldWidgetsMap[index];

    QVariantMap config;
    config["providerId"] = provider.id;
    config["providerName"] = provider.name;
    config["configId"] = m_configIdEdit->text().trimmed();
    config["displayName"] = m_displayNameEdit ? m_displayNameEdit->text().trimmed() : QString();
    config["enabled"] = true;

    if (m_formMode == FormMode::EditExisting)
        config["editMode"] = true;

    for (const auto& field : provider.fields) {
        if (widgets.inputs.contains(field.key)) {
            config[field.key] = widgets.inputs.value(field.key)->text();
        } else if (widgets.combos.contains(field.key)) {
            config[field.key] = widgets.combos.value(field.key)->currentText();
        } else {
            config[field.key] = QString();
        }
    }

    return config;
}

void ModelConfigManagerPage::autoGenerateConfigId()
{
    if (m_formMode == FormMode::EditExisting)
        return;
    const QSignalBlocker blocker(m_configIdEdit);
    m_configIdEdit->setText(generatedConfigId());
}

int ModelConfigManagerPage::providerIndexForId(const QString& providerId) const
{
    if (providerId.isEmpty())
        return -1;
    for (int i = 0; i < m_providers.size(); ++i) {
        if (m_providers[i].id == providerId)
            return i;
    }
    return -1;
}

QString ModelConfigManagerPage::inferProviderTag(const QString& provider, const QString& baseUrl) const
{
    if (m_providerTagInferrer)
        return m_providerTagInferrer(provider, baseUrl);
    return QString();
}

QString ModelConfigManagerPage::generatedConfigId() const
{
    const QString displayName = m_displayNameEdit ? m_displayNameEdit->text().simplified() : QString();
    if (!displayName.isEmpty())
        return displayName;

    int index = m_formStack->currentIndex();
    if (index < 0 || index >= m_providers.size())
        return QString();

    const auto& provider = m_providers[index];
    const auto& widgets = m_fieldWidgetsMap[index];

    static const QString kModelId = QStringLiteral("modelId");
    QString modelId;
    if (widgets.combos.contains(kModelId))
        modelId = widgets.combos.value(kModelId)->currentText().trimmed();
    else if (widgets.inputs.contains(kModelId))
        modelId = widgets.inputs.value(kModelId)->text().trimmed();

    if (modelId.isEmpty())
        return QString();

    if (m_configIdGenerator)
        return m_configIdGenerator(provider.id, modelId);
    return QStringLiteral("%1@%2").arg(provider.id, modelId);
}
