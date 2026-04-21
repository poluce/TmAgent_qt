#ifndef COORDINATIONTOOLPROVIDER_H
#define COORDINATIONTOOLPROVIDER_H

#include <tmagent/plugin/IToolProvider.h>

#include <QHash>
#include <QObject>

namespace TmAgent {
class IToolPluginHost;
}

class AgentTool;

class CoordinationToolProvider final : public QObject, public TmAgent::IToolProvider {
    Q_OBJECT
public:
    explicit CoordinationToolProvider(TmAgent::IToolPluginHost* host, QObject* parent = nullptr);

    static QList<TmAgent::Tool> toolSchemas();
    QList<TmAgent::Tool> listTools() const override;
    TmAgent::ToolResult execute(const TmAgent::ToolCall& call) override;

private:
    TmAgent::IToolPluginHost* m_host;
    QList<TmAgent::Tool> m_tools;
    QHash<QString, AgentTool*> m_toolInstances;
};

#endif // COORDINATIONTOOLPROVIDER_H
