#ifndef WORKSPACETOOLPROVIDER_H
#define WORKSPACETOOLPROVIDER_H

#include <tmagent/plugin/IToolProvider.h>
#include <QObject>

class WorkspaceToolProvider final : public QObject, public TmAgent::IToolProvider {
    Q_OBJECT
public:
    explicit WorkspaceToolProvider(QObject* parent = nullptr);

    QList<TmAgent::Tool> listTools() const override;
    TmAgent::ToolResult execute(const TmAgent::ToolCall& call) override;

private:
    QList<TmAgent::Tool> m_tools;
};

#endif // WORKSPACETOOLPROVIDER_H
