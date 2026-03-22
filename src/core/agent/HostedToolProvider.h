#ifndef HOSTEDTOOLPROVIDER_H
#define HOSTEDTOOLPROVIDER_H

#include "IToolPluginHost.h"
#include "IToolProvider.h"
#include <QList>
#include <QObject>

struct HostedToolSpec {
    QString name;
    QString fallbackDescription;
};

class HostedToolProvider : public QObject, public IToolProvider {
public:
    HostedToolProvider(IToolPluginHost* host,
                       const QList<HostedToolSpec>& tools,
                       QObject* parent = nullptr)
        : QObject(parent)
        , m_host(host)
        , m_tools(tools)
    {
    }

    void setTools(const QList<HostedToolSpec>& tools)
    {
        m_tools = tools;
    }

    QList<Tool> listTools() const override
    {
        QList<Tool> schemas;
        if (!m_host)
            return schemas;

        schemas.reserve(m_tools.size());
        for (const HostedToolSpec& spec : m_tools) {
            const Tool tool = m_host->resolveToolSchema(spec.name, spec.fallbackDescription);
            if (!tool.name.trimmed().isEmpty())
                schemas.append(tool);
        }
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

        for (const HostedToolSpec& spec : m_tools) {
            if (spec.name == call.name) {
                return m_host->executeHostedTool(call.name, spec.fallbackDescription, call.input);
            }
        }

        return ToolResult(
            QStringLiteral("错误: 未知的工具 %1").arg(call.name),
            QStringLiteral("执行失败"),
            false);
    }

private:
    IToolPluginHost* m_host = nullptr;
    QList<HostedToolSpec> m_tools;
};

#endif // HOSTEDTOOLPROVIDER_H
