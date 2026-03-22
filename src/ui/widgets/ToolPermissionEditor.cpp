#include "ToolPermissionEditor.h"

#include <QHeaderView>
#include <QLabel>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

ToolPermissionEditor::ToolPermissionEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_hintLabel = new QLabel(
        tr("按插件分组配置工具权限。已保存但当前不可用的工具会保留在“不可用工具”分组中。"),
        this);
    m_hintLabel->setWordWrap(true);
    layout->addWidget(m_hintLabel);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels(QStringList() << tr("工具来源") << tr("状态"));
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree, 1);
}

void ToolPermissionEditor::setToolPlugins(const QList<ToolPluginInfo>& plugins)
{
    m_plugins = plugins;
    rebuildTree();
}

void ToolPermissionEditor::setSelectedTools(const QStringList& selectedTools)
{
    m_selectedTools = selectedTools;
    m_selectedTools.removeDuplicates();
    rebuildTree();
}

QStringList ToolPermissionEditor::selectedTools() const
{
    QStringList selected;
    if (!m_tree)
        return selected;

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* topItem = m_tree->topLevelItem(i);
        if (!topItem)
            continue;
        for (int j = 0; j < topItem->childCount(); ++j) {
            QTreeWidgetItem* child = topItem->child(j);
            if (!child)
                continue;
            if (child->checkState(0) == Qt::Checked) {
                const QString toolName = child->data(0, Qt::UserRole).toString().trimmed();
                if (!toolName.isEmpty())
                    selected.append(toolName);
            }
        }
    }

    selected.removeDuplicates();
    return selected;
}

void ToolPermissionEditor::rebuildTree()
{
    if (!m_tree)
        return;

    m_tree->clear();

    QSet<QString> knownTools;
    QList<ToolPluginInfo> plugins = m_plugins;
    std::sort(plugins.begin(), plugins.end(), [](const ToolPluginInfo& a, const ToolPluginInfo& b) {
        return a.descriptor.displayName.compare(b.descriptor.displayName, Qt::CaseInsensitive) < 0;
    });

    for (const ToolPluginInfo& info : plugins) {
        const bool available = info.externalProvider || (info.loaded && info.enabled);
        const QString title = info.externalProvider
            ? tr("%1（外部）").arg(info.descriptor.displayName)
            : info.descriptor.displayName;

        auto* topItem = new QTreeWidgetItem(m_tree);
        topItem->setText(0, title);
        topItem->setText(1, available ? tr("可用") : tr("不可用"));
        topItem->setToolTip(0, info.descriptor.description);
        topItem->setExpanded(true);

        QStringList toolNames = info.descriptor.toolNames;
        toolNames.removeDuplicates();
        std::sort(toolNames.begin(), toolNames.end(), [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });

        for (const QString& toolName : toolNames) {
            knownTools.insert(toolName);
            auto* child = new QTreeWidgetItem(topItem);
            child->setText(0, toolName);
            child->setText(1, available ? tr("已发现") : tr("当前未加载"));
            child->setData(0, Qt::UserRole, toolName);
            child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
            child->setCheckState(0, m_selectedTools.contains(toolName) ? Qt::Checked : Qt::Unchecked);
            if (!available)
                child->setDisabled(true);
        }
    }

    QStringList unavailable;
    for (const QString& toolName : std::as_const(m_selectedTools)) {
        if (!knownTools.contains(toolName))
            unavailable.append(toolName);
    }
    unavailable.removeDuplicates();
    std::sort(unavailable.begin(), unavailable.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    if (!unavailable.isEmpty()) {
        auto* topItem = new QTreeWidgetItem(m_tree);
        topItem->setText(0, tr("不可用工具"));
        topItem->setText(1, tr("配置中存在，但当前未发现"));
        topItem->setExpanded(true);
        for (const QString& toolName : unavailable) {
            auto* child = new QTreeWidgetItem(topItem);
            child->setText(0, toolName);
            child->setText(1, tr("缺失"));
            child->setData(0, Qt::UserRole, toolName);
            child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
            child->setCheckState(0, Qt::Checked);
            child->setDisabled(true);
        }
    }
}
