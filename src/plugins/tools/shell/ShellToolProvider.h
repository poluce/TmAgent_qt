#ifndef SHELLTOOLPROVIDER_H
#define SHELLTOOLPROVIDER_H

#include <tmagent/plugin/IToolProvider.h>
#include <QObject>

class ShellToolProvider final : public QObject, public TmAgent::IToolProvider {
    Q_OBJECT
public:
    explicit ShellToolProvider(QObject* parent = nullptr);

    QList<TmAgent::Tool> listTools() const override;
    TmAgent::ToolResult execute(const TmAgent::ToolCall& call) override;

private:
    QList<TmAgent::Tool> m_tools;
};

#endif // SHELLTOOLPROVIDER_H
