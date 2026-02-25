#ifndef LOCALTOOLPROVIDER_H
#define LOCALTOOLPROVIDER_H

#include "IToolProvider.h"
#include <QMap>

class LocalToolProvider : public IToolProvider {
public:
    ~LocalToolProvider() override;
    void registerTool(ITool* tool, const QString& description);
    QList<Tool> listTools() const override;
    ToolResult execute(const ToolCall& call) override;
    QString descriptionFor(const QString& toolName) const;

private:
    struct ToolEntry {
        ITool* toolImpl = nullptr;
        QString description;
    };

    QMap<QString, ToolEntry> m_registry;
};

#endif // LOCALTOOLPROVIDER_H
