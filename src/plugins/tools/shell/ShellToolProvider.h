#ifndef SHELLTOOLPROVIDER_H
#define SHELLTOOLPROVIDER_H

#include "core/agent/IToolProvider.h"
#include <QObject>

class ShellToolProvider final : public QObject, public IToolProvider {
    Q_OBJECT
public:
    explicit ShellToolProvider(QObject* parent = nullptr);

    QList<Tool> listTools() const override;
    ToolResult execute(const ToolCall& call) override;

private:
    QList<Tool> m_tools;
};

#endif // SHELLTOOLPROVIDER_H
