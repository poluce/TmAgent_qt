#ifndef HOSTEDTOOLPROVIDER_H
#define HOSTEDTOOLPROVIDER_H

#include "IToolPluginHost.h"
#include "IToolProvider.h"
#include <QList>
#include <QObject>

class HostedToolProvider : public QObject, public IToolProvider {
public:
    HostedToolProvider(IToolPluginHost* host,
                       const QList<Tool>& tools,
                       QObject* parent = nullptr)
        : QObject(parent)
        , m_host(host)
        , m_tools(tools)
    {
    }

    void setTools(const QList<Tool>& tools)
    {
        m_tools = tools;
    }

    QList<Tool> listTools() const override
    {
        QList<Tool> schemas;
        if (!m_host)
            return schemas;

        schemas.reserve(m_tools.size());
        for (const Tool& tool : m_tools)
            schemas.append(tool);
        return schemas;
    }

    ToolResult execute(const ToolCall& call) override
    {
        if (!m_host) {
            return ToolResult(
                QStringLiteral("错误: tool host 未就绪"),
                QStringLiteral("执行失败"),
                false);
        }

        for (const Tool& tool : m_tools) {
            if (tool.name == call.name) {
                return m_host->executeHostedTool(call.name, call.input);
            }
        }

        return ToolResult(
            QStringLiteral("错误: 未知的工具 %1").arg(call.name),
            QStringLiteral("执行失败"),
            false);
    }

private:
    IToolPluginHost* m_host = nullptr;
    QList<Tool> m_tools;
};

#endif // HOSTEDTOOLPROVIDER_H
