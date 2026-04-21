#ifndef WEBTOOLPROVIDER_H
#define WEBTOOLPROVIDER_H

#include <tmagent/plugin/IToolProvider.h>
#include <tmagent/types/ToolTypes.h>
#include <QList>
#include <QObject>

class WebToolProvider final : public QObject, public TmAgent::IToolProvider {
    Q_OBJECT
public:
    explicit WebToolProvider(QObject* parent = nullptr);

    static QList<TmAgent::Tool> toolSchemas();

    QList<TmAgent::Tool> listTools() const override;
    TmAgent::ToolResult execute(const TmAgent::ToolCall& call) override;

private:
    QList<TmAgent::Tool> m_tools;
};

#endif // WEBTOOLPROVIDER_H
