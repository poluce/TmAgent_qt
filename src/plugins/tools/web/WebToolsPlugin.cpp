#include "WebToolsPlugin.h"

#include "core/agent/HostedToolProvider.h"
#include "core/tools/ToolSchemaSupport.h"

namespace {

QList<Tool> webTools()
{
    return {
        makeToolSchema(
            QStringLiteral("web_fetch"),
            QStringLiteral("获取指定 URL 的网页内容并转换为 Markdown 格式。"),
            QJsonObject {
                { QStringLiteral("url"), makePropertySchema(QStringLiteral("string"), QStringLiteral("网页 URL")) },
                { QStringLiteral("format"), makePropertySchema(QStringLiteral("string"), QStringLiteral("返回格式（text/markdown，默认 markdown）")) }
            },
            QStringList { QStringLiteral("url") }),
        makeToolSchema(
            QStringLiteral("websearch"),
            QStringLiteral("搜索互联网获取实时信息。"),
            QJsonObject {
                { QStringLiteral("query"), makePropertySchema(QStringLiteral("string"), QStringLiteral("搜索关键词")) }
            },
            QStringList { QStringLiteral("query") })
    };
}

} // namespace

ToolPluginDescriptor WebToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("web_tools");
    descriptor.displayName = QStringLiteral("网络工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("web");
    descriptor.description = QStringLiteral("网页抓取和网页搜索相关工具。");
    for (const Tool& tool : webTools())
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

IToolProvider* WebToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new HostedToolProvider(host, webTools(), parent);
}
