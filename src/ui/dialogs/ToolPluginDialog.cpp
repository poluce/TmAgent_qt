#include "ToolPluginDialog.h"

#include "CommandPolicyDialog.h"
#include "McpConfigDialog.h"
#include <QDir>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QSet>
#include <algorithm>

namespace {

void applyAppStyleSheet(QDialog& dlg)
{
    QFile qssFile(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/styles/app.qss")));
    if (qssFile.open(QFile::ReadOnly)) {
        dlg.setStyleSheet(QString::fromUtf8(qssFile.readAll()));
        qssFile.close();
    }
}

QJsonObject pluginEntriesObject(const QJsonObject& root)
{
    return root.value(QStringLiteral("plugins")).toObject();
}

QJsonObject normalizedPluginEntry(const QJsonValue& value)
{
    const QJsonObject obj = value.toObject();
    QJsonObject out;
    out.insert(QStringLiteral("enabled"), obj.value(QStringLiteral("enabled")).toBool(true));
    out.insert(QStringLiteral("config"),
               obj.value(QStringLiteral("config")).isObject()
                   ? obj.value(QStringLiteral("config")).toObject()
                   : QJsonObject());
    return out;
}

QString displayHealthState(const ToolPluginInfo& info)
{
    if (!info.loaded)
        return QObject::tr("加载失败");
    if (info.health.state == QLatin1String("disabled"))
        return QObject::tr("已禁用");
    if (info.health.state == QLatin1String("error"))
        return QObject::tr("错误");
    return QObject::tr("正常");
}

} // namespace

namespace ToolPluginDialog {

void show(QWidget* parent, IAppFacade& app)
{
    auto* governance = &app.governance();
    if (!governance)
        return;

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("工具插件治理"));
    dlg.resize(1180, 760);
    applyAppStyleSheet(dlg);

    auto* mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* header = new QFrame(&dlg);
    header->setFixedHeight(60);
    header->setProperty("class", "SettingsHeader");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(20, 0, 20, 0);

    auto* titleContainer = new QWidget(header);
    auto* titleVBox = new QVBoxLayout(titleContainer);
    titleVBox->setContentsMargins(0, 0, 0, 0);
    titleVBox->setSpacing(2);
    auto* title = new QLabel(QObject::tr("工具插件治理"), titleContainer);
    title->setProperty("class", "SettingsTitle");
    auto* subTitle = new QLabel(QObject::tr("查看工具插件加载状态、启停配置与来源信息"), titleContainer);
    subTitle->setProperty("class", "SettingsSubTitle");
    titleVBox->addWidget(title);
    titleVBox->addWidget(subTitle);
    headerLayout->addWidget(titleContainer);
    headerLayout->addStretch();
    mainLayout->addWidget(header);

    auto* content = new QWidget(&dlg);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(20, 20, 20, 20);
    contentLayout->setSpacing(12);

    auto* searchDirsGroup = new QGroupBox(QObject::tr("附加插件搜索目录"), content);
    auto* searchDirsLayout = new QVBoxLayout(searchDirsGroup);
    auto* searchDirsHint = new QLabel(
        QObject::tr("每行一个目录。应用会自动扫描 resources/plugins/tools 以及开发态 build-plugins 输出目录。"),
        searchDirsGroup);
    searchDirsHint->setWordWrap(true);
    auto* searchDirsEdit = new QPlainTextEdit(searchDirsGroup);
    searchDirsEdit->setMinimumHeight(88);
    searchDirsLayout->addWidget(searchDirsHint);
    searchDirsLayout->addWidget(searchDirsEdit);
    contentLayout->addWidget(searchDirsGroup);

    auto* splitter = new QSplitter(Qt::Horizontal, content);
    contentLayout->addWidget(splitter, 1);

    auto* listPanel = new QGroupBox(QObject::tr("插件列表"), splitter);
    auto* listLayout = new QVBoxLayout(listPanel);
    auto* pluginTree = new QTreeWidget(listPanel);
    pluginTree->setHeaderLabels(QStringList()
                                << QObject::tr("插件")
                                << QObject::tr("启用")
                                << QObject::tr("健康")
                                << QObject::tr("工具数"));
    pluginTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    pluginTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    pluginTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    pluginTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    listLayout->addWidget(pluginTree, 1);

    auto* detailPanel = new QGroupBox(QObject::tr("插件详情"), splitter);
    auto* detailLayout = new QVBoxLayout(detailPanel);
    auto* detailForm = new QFormLayout();
    QLabel* idValue = new QLabel(detailPanel);
    QLabel* versionValue = new QLabel(detailPanel);
    QLabel* categoryValue = new QLabel(detailPanel);
    QLabel* sourceValue = new QLabel(detailPanel);
    sourceValue->setWordWrap(true);
    QLabel* stateValue = new QLabel(detailPanel);
    stateValue->setWordWrap(true);
    QLabel* descriptionValue = new QLabel(detailPanel);
    descriptionValue->setWordWrap(true);
    detailForm->addRow(QObject::tr("ID:"), idValue);
    detailForm->addRow(QObject::tr("版本:"), versionValue);
    detailForm->addRow(QObject::tr("类别:"), categoryValue);
    detailForm->addRow(QObject::tr("来源:"), sourceValue);
    detailForm->addRow(QObject::tr("健康:"), stateValue);
    detailForm->addRow(QObject::tr("说明:"), descriptionValue);
    detailLayout->addLayout(detailForm);
    detailLayout->addWidget(new QLabel(QObject::tr("提供工具:"), detailPanel));
    auto* toolList = new QListWidget(detailPanel);
    detailLayout->addWidget(toolList, 1);

    auto* configPanel = new QGroupBox(QObject::tr("插件配置"), splitter);
    auto* configLayout = new QVBoxLayout(configPanel);
    auto* enabledCheck = new QCheckBox(QObject::tr("启用该插件"), configPanel);
    configLayout->addWidget(enabledCheck);

    auto* configHint = new QLabel(configPanel);
    configHint->setWordWrap(true);
    configLayout->addWidget(configHint);

    auto* specialButton = new QPushButton(configPanel);
    specialButton->hide();
    configLayout->addWidget(specialButton, 0, Qt::AlignLeft);

    auto* formContainer = new QWidget(configPanel);
    auto* formLayout = new QFormLayout(formContainer);
    formLayout->setContentsMargins(0, 0, 0, 0);
    configLayout->addWidget(formContainer, 1);
    configLayout->addStretch(1);

    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 4);
    splitter->setStretchFactor(2, 4);

    auto* footer = new QFrame(&dlg);
    footer->setFixedHeight(60);
    footer->setProperty("class", "SettingsFooter");
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(20, 0, 20, 0);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, footer);
    if (QPushButton* okBtn = buttons->button(QDialogButtonBox::Ok))
        okBtn->setText(QObject::tr("保存"));
    if (QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel))
        cancelBtn->setText(QObject::tr("取消"));
    footerLayout->addStretch();
    footerLayout->addWidget(buttons);

    mainLayout->addWidget(content, 1);
    mainLayout->addWidget(footer);

    QJsonObject pendingConfig = governance->loadToolPluginConfigObject();
    QList<ToolPluginInfo> pluginInfos = governance->toolPluginInfos();
    QString currentPluginId;

    struct FieldBinding {
        QString key;
        QString type;
        QWidget* widget = nullptr;
    };
    QList<FieldBinding> bindings;

    auto clearBindings = [&]() {
        bindings.clear();
        while (QLayoutItem* item = formLayout->takeAt(0)) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
    };

    auto saveCurrentPluginState = [&]() {
        if (currentPluginId.trimmed().isEmpty())
            return;

        QJsonObject plugins = pluginEntriesObject(pendingConfig);
        QJsonObject entry = normalizedPluginEntry(plugins.value(currentPluginId));
        entry.insert(QStringLiteral("enabled"), enabledCheck->isChecked());

        QJsonObject configObj;
        for (const FieldBinding& binding : bindings) {
            if (!binding.widget)
                continue;
            if (binding.type == QLatin1String("boolean")) {
                if (QCheckBox* check = qobject_cast<QCheckBox*>(binding.widget))
                    configObj.insert(binding.key, check->isChecked());
            } else if (binding.type == QLatin1String("integer")) {
                if (QSpinBox* spin = qobject_cast<QSpinBox*>(binding.widget))
                    configObj.insert(binding.key, spin->value());
            } else {
                if (QLineEdit* edit = qobject_cast<QLineEdit*>(binding.widget))
                    configObj.insert(binding.key, edit->text().trimmed());
            }
        }
        entry.insert(QStringLiteral("config"), configObj);
        plugins.insert(currentPluginId, entry);
        pendingConfig.insert(QStringLiteral("plugins"), plugins);
    };

    auto rebuildPluginTree = [&]() {
        pluginTree->clear();
        for (const ToolPluginInfo& info : pluginInfos) {
            auto* item = new QTreeWidgetItem(pluginTree);
            item->setText(0, info.descriptor.displayName);
            item->setText(1, info.externalProvider ? QObject::tr("外部") : (info.enabled ? QObject::tr("是") : QObject::tr("否")));
            item->setText(2, displayHealthState(info));
            item->setText(3, QString::number(info.descriptor.toolNames.size()));
            item->setData(0, Qt::UserRole, info.descriptor.pluginId);
            item->setToolTip(0, info.descriptor.description);
        }
        if (pluginTree->topLevelItemCount() > 0)
            pluginTree->setCurrentItem(pluginTree->topLevelItem(0));
    };

    auto loadPluginIntoPanels = [&](const QString& pluginId) {
        saveCurrentPluginState();
        currentPluginId = pluginId;
        clearBindings();
        toolList->clear();

        const auto it = std::find_if(pluginInfos.begin(), pluginInfos.end(), [&](const ToolPluginInfo& info) {
            return info.descriptor.pluginId == pluginId;
        });
        if (it == pluginInfos.end())
            return;
        const ToolPluginInfo info = *it;

        idValue->setText(info.descriptor.pluginId);
        versionValue->setText(info.descriptor.version);
        categoryValue->setText(info.descriptor.category);
        sourceValue->setText(info.sourcePath.isEmpty() ? QObject::tr("运行时提供者") : QDir::toNativeSeparators(info.sourcePath));
        stateValue->setText(info.health.message.trimmed().isEmpty()
                                ? displayHealthState(info)
                                : QStringLiteral("%1：%2").arg(displayHealthState(info), info.health.message));
        descriptionValue->setText(info.descriptor.description);
        for (const QString& toolName : info.descriptor.toolNames)
            toolList->addItem(toolName);

        const QJsonObject entry = normalizedPluginEntry(pluginEntriesObject(pendingConfig).value(pluginId));
        enabledCheck->setChecked(info.externalProvider ? true : entry.value(QStringLiteral("enabled")).toBool(info.enabled));
        enabledCheck->setEnabled(!info.externalProvider);

        specialButton->hide();
        configHint->clear();
        if (pluginId == QLatin1String("shell_tools")) {
            specialButton->setText(QObject::tr("打开命令权限设置"));
            specialButton->show();
            configHint->setText(QObject::tr("命令安全策略由“命令权限”对话框统一管理。"));
        } else if (pluginId == QLatin1String("mcp_provider")) {
            specialButton->setText(QObject::tr("打开 MCP 配置"));
            specialButton->show();
            configHint->setText(QObject::tr("MCP 外部工具由服务器配置动态发现，本页仅展示运行状态。"));
        } else {
            configHint->setText(QObject::tr("当前插件没有额外配置项。"));
        }

        const QJsonObject schema = info.descriptor.configSchema;
        const QJsonObject currentPluginConfig = entry.value(QStringLiteral("config")).toObject();
        const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
        QStringList keys = properties.keys();
        std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        for (const QString& key : keys) {
            const QJsonObject prop = properties.value(key).toObject();
            const QString type = prop.value(QStringLiteral("type")).toString().trimmed().toLower();
            const QString titleText = prop.value(QStringLiteral("title")).toString().trimmed().isEmpty()
                ? key
                : prop.value(QStringLiteral("title")).toString().trimmed();
            const QString desc = prop.value(QStringLiteral("description")).toString().trimmed();

            if (type == QLatin1String("boolean")) {
                auto* check = new QCheckBox(formContainer);
                check->setChecked(currentPluginConfig.contains(key)
                                      ? currentPluginConfig.value(key).toBool()
                                      : prop.value(QStringLiteral("default")).toBool());
                check->setToolTip(desc);
                formLayout->addRow(titleText + QStringLiteral(":"), check);
                bindings.append({ key, type, check });
            } else if (type == QLatin1String("integer")) {
                auto* spin = new QSpinBox(formContainer);
                spin->setRange(prop.value(QStringLiteral("minimum")).toInt(0),
                               prop.value(QStringLiteral("maximum")).toInt(999999));
                spin->setValue(currentPluginConfig.contains(key)
                                   ? currentPluginConfig.value(key).toInt()
                                   : prop.value(QStringLiteral("default")).toInt());
                spin->setToolTip(desc);
                formLayout->addRow(titleText + QStringLiteral(":"), spin);
                bindings.append({ key, type, spin });
            } else {
                auto* edit = new QLineEdit(formContainer);
                edit->setText(currentPluginConfig.contains(key)
                                  ? currentPluginConfig.value(key).toString()
                                  : prop.value(QStringLiteral("default")).toString());
                edit->setToolTip(desc);
                formLayout->addRow(titleText + QStringLiteral(":"), edit);
                bindings.append({ key, QStringLiteral("string"), edit });
            }
        }
    };

    QStringList extraDirs;
    const QJsonArray extraDirsArray = pendingConfig.value(QStringLiteral("search_dirs")).toArray();
    for (const QJsonValue& value : extraDirsArray) {
        const QString dir = value.toString().trimmed();
        if (!dir.isEmpty())
            extraDirs.append(dir);
    }
    searchDirsEdit->setPlainText(extraDirs.join(QStringLiteral("\n")));

    QObject::connect(pluginTree,
                     &QTreeWidget::currentItemChanged,
                     &dlg,
                     [&](QTreeWidgetItem* current, QTreeWidgetItem*) {
                         if (!current)
                             return;
                         loadPluginIntoPanels(current->data(0, Qt::UserRole).toString());
                     });
    QObject::connect(specialButton, &QPushButton::clicked, &dlg, [&]() {
        if (currentPluginId == QLatin1String("shell_tools"))
            CommandPolicyDialog::show(&dlg, app);
        else if (currentPluginId == QLatin1String("mcp_provider"))
            McpConfigDialog::show(&dlg, app);
    });

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
        saveCurrentPluginState();

        QJsonArray searchDirs;
        const QStringList dirLines = searchDirsEdit->toPlainText().split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
        QSet<QString> seen;
        for (const QString& line : dirLines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || seen.contains(trimmed))
                continue;
            seen.insert(trimmed);
            searchDirs.append(trimmed);
        }
        pendingConfig.insert(QStringLiteral("search_dirs"), searchDirs);

        QString err;
        if (!governance->saveToolPluginConfigObject(pendingConfig, &err)) {
            QMessageBox::warning(&dlg,
                                 QObject::tr("保存失败"),
                                 err.isEmpty() ? QObject::tr("工具插件配置写入失败。") : err);
            return;
        }

        governance->reloadToolPlugins();
        governance->applyToolDispatcherToAllRuntimes();
        dlg.accept();
    });

    rebuildPluginTree();
    dlg.exec();
}

} // namespace ToolPluginDialog
