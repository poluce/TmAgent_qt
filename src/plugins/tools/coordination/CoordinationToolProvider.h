#ifndef COORDINATIONTOOLPROVIDER_H
#define COORDINATIONTOOLPROVIDER_H

#include "core/agent/IToolProvider.h"

#include <QHash>
#include <QObject>

class AgentTool;

class CoordinationToolProvider final : public QObject, public IToolProvider {
    Q_OBJECT
public:
    explicit CoordinationToolProvider(QObject* parent = nullptr);

    static QList<Tool> toolSchemas();
    QList<Tool> listTools() const override;
    ToolResult execute(const ToolCall& call) override;

private:
    QList<Tool> m_tools;
    QHash<QString, AgentTool*> m_toolInstances;
};

#endif // COORDINATIONTOOLPROVIDER_H
