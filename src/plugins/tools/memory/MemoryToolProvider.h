#ifndef MEMORYTOOLPROVIDER_H
#define MEMORYTOOLPROVIDER_H

#include <tmagent/plugin/IToolProvider.h>
#include <tmagent/plugin/IToolPluginHost.h>

#include <QObject>

class MemoryToolProvider final : public QObject, public TmAgent::IToolProvider {
    Q_OBJECT
public:
    explicit MemoryToolProvider(TmAgent::IToolPluginHost* host, QObject* parent = nullptr);

    static QList<TmAgent::Tool> toolSchemas();
    QList<TmAgent::Tool> listTools() const override;
    TmAgent::ToolResult execute(const TmAgent::ToolCall& call) override;

private:
    TmAgent::IToolPluginHost* m_host;
    QList<TmAgent::Tool> m_tools;
};

#endif // MEMORYTOOLPROVIDER_H
