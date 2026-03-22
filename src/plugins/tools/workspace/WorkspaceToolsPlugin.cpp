#include "WorkspaceToolsPlugin.h"

#include "core/agent/HostedToolProvider.h"

namespace {

QList<HostedToolSpec> workspaceToolSpecs()
{
    return {
        { QStringLiteral("create_file"), QStringLiteral("在指定目录创建新文件") },
        { QStringLiteral("view_file"), QStringLiteral("查看文件完整内容") },
        { QStringLiteral("read_file_lines"), QStringLiteral("读取文件指定行") },
        { QStringLiteral("replace_in_file"), QStringLiteral("替换文件中的指定内容") },
        { QStringLiteral("delete_file"), QStringLiteral("删除指定文件") },
        { QStringLiteral("list_directory"), QStringLiteral("列出目录内容") },
        { QStringLiteral("grep_search"), QStringLiteral("在目录中搜索内容") },
        { QStringLiteral("find_by_name"), QStringLiteral("按文件名模式搜索") },
        { QStringLiteral("insert_content"), QStringLiteral("在文件指定行插入内容") },
        { QStringLiteral("multi_replace_in_file"), QStringLiteral("一次替换文件中多处内容") },
        { QStringLiteral("send_file"), QStringLiteral("将内容保存为文件发送给用户") },
        { QStringLiteral("apply_patch"), QStringLiteral("应用结构化补丁") }
    };
}

} // namespace

ToolPluginDescriptor WorkspaceToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("workspace_tools");
    descriptor.displayName = QStringLiteral("工作区工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("workspace");
    descriptor.description = QStringLiteral("文件读取、目录搜索、补丁与文件写入类工具集合。");
    const QList<HostedToolSpec> specs = workspaceToolSpecs();
    for (const HostedToolSpec& spec : specs)
        descriptor.toolNames.append(spec.name);
    return descriptor;
}

IToolProvider* WorkspaceToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new HostedToolProvider(host, workspaceToolSpecs(), parent);
}
