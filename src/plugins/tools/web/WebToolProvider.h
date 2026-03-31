#ifndef WEBTOOLPROVIDER_H
#define WEBTOOLPROVIDER_H

#include "core/agent/IToolProvider.h"
#include "core/agent/ToolTypes.h"
#include <QList>
#include <QObject>

class WebToolProvider final : public QObject, public IToolProvider {
    Q_OBJECT
public:
    explicit WebToolProvider(QObject* parent = nullptr);

    static QList<Tool> toolSchemas();

    QList<Tool> listTools() const override;
    ToolResult execute(const ToolCall& call) override;

private:
    QList<Tool> m_tools;
};

#endif // WEBTOOLPROVIDER_H
