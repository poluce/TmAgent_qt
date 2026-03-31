#ifndef CODEINTELTOOLPROVIDER_H
#define CODEINTELTOOLPROVIDER_H

#include "core/agent/IToolProvider.h"
#include "core/agent/ToolTypes.h"
#include <QList>
#include <QObject>

class CodeIntelToolProvider final : public QObject, public IToolProvider {
    Q_OBJECT
public:
    explicit CodeIntelToolProvider(QObject* parent = nullptr);

    static QList<Tool> toolSchemas();
    QList<Tool> listTools() const override;
    ToolResult execute(const ToolCall& call) override;

private:
    QList<Tool> m_tools;
};

#endif // CODEINTELTOOLPROVIDER_H
