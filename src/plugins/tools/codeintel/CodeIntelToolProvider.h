#ifndef CODEINTELTOOLPROVIDER_H
#define CODEINTELTOOLPROVIDER_H

#include <tmagent/plugin/IToolProvider.h>
#include <tmagent/plugin/IToolPluginHost.h>
#include <tmagent/types/ToolTypes.h>
#include <QList>
#include <QObject>

class CodeIntelToolProvider final : public QObject, public TmAgent::IToolProvider {
    Q_OBJECT
public:
    explicit CodeIntelToolProvider(TmAgent::IToolPluginHost* host, QObject* parent = nullptr);

    static QList<TmAgent::Tool> toolSchemas();
    QList<TmAgent::Tool> listTools() const override;
    TmAgent::ToolResult execute(const TmAgent::ToolCall& call) override;

private:
    TmAgent::IToolPluginHost* m_host;
    QList<TmAgent::Tool> m_tools;
};

#endif // CODEINTELTOOLPROVIDER_H
