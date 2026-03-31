#ifndef MEMORYTOOLPROVIDER_H
#define MEMORYTOOLPROVIDER_H

#include "core/agent/IToolProvider.h"

#include <QObject>

class MemoryToolProvider final : public QObject, public IToolProvider {
    Q_OBJECT
public:
    explicit MemoryToolProvider(QObject* parent = nullptr);

    static QList<Tool> toolSchemas();
    QList<Tool> listTools() const override;
    ToolResult execute(const ToolCall& call) override;

private:
    QList<Tool> m_tools;
};

#endif // MEMORYTOOLPROVIDER_H
