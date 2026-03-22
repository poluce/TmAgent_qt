#ifndef TOOLPERMISSIONEDITOR_H
#define TOOLPERMISSIONEDITOR_H

#include "core/agent/ToolPluginTypes.h"
#include <QWidget>

class QLabel;
class QTreeWidget;

class ToolPermissionEditor : public QWidget {
    Q_OBJECT
public:
    explicit ToolPermissionEditor(QWidget* parent = nullptr);

    void setToolPlugins(const QList<ToolPluginInfo>& plugins);
    void setSelectedTools(const QStringList& selectedTools);
    QStringList selectedTools() const;

private:
    void rebuildTree();

    QLabel* m_hintLabel = nullptr;
    QTreeWidget* m_tree = nullptr;
    QList<ToolPluginInfo> m_plugins;
    QStringList m_selectedTools;
};

#endif // TOOLPERMISSIONEDITOR_H
