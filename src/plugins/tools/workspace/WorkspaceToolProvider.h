#ifndef WORKSPACETOOLPROVIDER_H
#define WORKSPACETOOLPROVIDER_H

#include "core/agent/IToolProvider.h"
#include <QObject>

class WorkspaceToolProvider final : public QObject, public IToolProvider {
    Q_OBJECT
public:
    explicit WorkspaceToolProvider(QObject* parent = nullptr);

    QList<Tool> listTools() const override;
    ToolResult execute(const ToolCall& call) override;

private:
    QList<Tool> m_tools;
};

#endif // WORKSPACETOOLPROVIDER_H
