#ifndef SCHEDULERTOOLPROVIDER_H
#define SCHEDULERTOOLPROVIDER_H

#include "core/agent/IToolProvider.h"

#include <QObject>

class SchedulerToolProvider final : public QObject, public IToolProvider {
    Q_OBJECT
public:
    explicit SchedulerToolProvider(QObject* parent = nullptr);

    static QList<Tool> toolSchemas();
    QList<Tool> listTools() const override;
    ToolResult execute(const ToolCall& call) override;

private:
    QList<Tool> m_tools;
};

#endif // SCHEDULERTOOLPROVIDER_H
