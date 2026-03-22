#include "ToolDispatcher.h"
#include "core/tools/AgentTool.h"
#include "core/tools/AgentToolNames.h"
#include "core/tools/CodeParserTool.h"
#include "core/tools/EventLogTool.h"
#include "core/tools/ExternalSearchTool.h"
#include "core/tools/FileTool.h"
#include "core/tools/LspInstallTool.h"
#include "core/tools/LspTool.h"
#include "core/tools/MemoryTool.h"
#include "core/tools/PatchTool.h"
#include "core/tools/SessionSearchTool.h"
#include "core/tools/ShellTool.h"
#include "core/tools/WebTool.h"
#include "core/utils/ToolSchemaLoader.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QDebug>

namespace {

Tool resolveToolSchemaWithFallback(const QString& name, const QString& fallbackDescription)
{
    Tool tool = ToolSchemaLoader::getToolSchema(name);
    if (tool.name.trimmed().isEmpty()) {
        tool.name = name.trimmed();
        tool.description = fallbackDescription;
        QJsonObject schema;
        schema.insert(QStringLiteral("type"), QStringLiteral("object"));
        schema.insert(QStringLiteral("properties"), QJsonObject());
        schema.insert(QStringLiteral("required"), QJsonArray());
        tool.inputSchema = schema;
        return tool;
    }

    if (tool.description.trimmed().isEmpty())
        tool.description = fallbackDescription;
    return tool;
}

bool isOkResult(const QString& raw)
{
    const QString text = raw.trimmed();
    if (text.startsWith(QStringLiteral("错误"))
        || text.startsWith(QStringLiteral("抓取失败"))
        || text.startsWith(QStringLiteral("搜索失败"))) {
        return false;
    }

    static const QRegularExpression kExitCodeRe(
        QStringLiteral("(?:^|\\n)\\s*退出码\\s*:\\s*(-?\\d+)"));
    const QRegularExpressionMatch match = kExitCodeRe.match(text);
    if (match.hasMatch())
        return match.captured(1).toInt() == 0;

    return true;
}

ToolResult wrapResult(const QString& raw, const QString& okSummary, const QString& failSummary)
{
    const bool ok = isOkResult(raw);
    return ToolResult(raw, ok ? okSummary : failSummary, ok);
}

} // namespace

ToolDispatcher* ToolDispatcher::instance()
{
    static ToolDispatcher dispatcher;
    return &dispatcher;
}

ToolDispatcher::ToolDispatcher(QObject* parent)
    : QObject(parent)
{
}

ToolDispatcher::~ToolDispatcher() = default;

void ToolDispatcher::registerProvider(IToolProvider* provider, const QString& name)
{
    if (!provider)
        return;

    const QString providerName = name.isEmpty() ? QString("provider") : name;
    if (m_providers.contains(providerName)) {
        qWarning() << "[ToolDispatcher] provider 名称冲突:" << providerName;
        return;
    }

    m_providers.insert(providerName, provider);
    indexProviderTools(provider, providerName);
}

void ToolDispatcher::refreshProvider(const QString& name)
{
    if (!m_providers.contains(name))
        return;

    const QList<QString> toolNames = m_toolOwners.keys();
    for (const QString& toolName : toolNames) {
        if (m_toolOwners.value(toolName) == name) {
            m_toolOwners.remove(toolName);
            m_toolIndex.remove(toolName);
            m_toolSchemas.remove(toolName);
        }
    }

    indexProviderTools(m_providers.value(name), name);
}

void ToolDispatcher::registerDefaultTools()
{
    if (m_defaultToolsRegistered)
        return;

    const QString toolsPath =
        QCoreApplication::applicationDirPath() + QStringLiteral("/resources/tools.yaml");
    ToolSchemaLoader::loadFromFile(toolsPath);
    m_defaultToolsRegistered = true;
}

QList<Tool> ToolDispatcher::getAllToolSchemas() const
{
    return m_toolSchemas.values();
}

QStringList ToolDispatcher::toolNamesForProvider(const QString& providerName) const
{
    QStringList names;
    for (auto it = m_toolOwners.constBegin(); it != m_toolOwners.constEnd(); ++it) {
        if (it.value() == providerName)
            names.append(it.key());
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool ToolDispatcher::hasToolSchema(const QString& name) const
{
    return m_toolSchemas.contains(name);
}

ToolResult ToolDispatcher::dispatch(const ToolCall& call)
{
    const QString& toolName = call.name;
    QJsonObject input = call.input;
    input["_tool_call_id"] = call.id;
    QString inputStr = QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));

    qDebug() << "[ToolDispatcher] 分发接口工具调用:" << toolName;

    if (!m_toolIndex.contains(toolName)) {
        return ToolResult(QString("错误: 未知的工具 %1").arg(toolName), "执行失败", false);
    }

    const Tool& schema = m_toolSchemas[toolName];
    const QString desc = schema.description.isEmpty() ? toolName : schema.description;
    emit toolStarted(desc, inputStr);

    ToolCall enriched = call;
    enriched.input = input;
    return m_toolIndex.value(toolName)->execute(enriched);
}

void ToolDispatcher::registerAgentTools(const LLMConfig& config)
{
    Q_UNUSED(config);
}

void ToolDispatcher::clearProviders()
{
    m_providers.clear();
    m_toolIndex.clear();
    m_toolSchemas.clear();
    m_toolOwners.clear();
}

void ToolDispatcher::setDefaultAgentConfig(const LLMConfig& config)
{
    m_defaultAgentConfig = config;
}

Tool ToolDispatcher::resolveToolSchema(const QString& toolName,
                                       const QString& fallbackDescription) const
{
    return resolveToolSchemaWithFallback(toolName, fallbackDescription);
}

ToolResult ToolDispatcher::executeHostedTool(const QString& toolName,
                                             const QString& fallbackDescription,
                                             const QJsonObject& args)
{
    auto wrapSimpleResult = [](const QString& raw,
                               const QString& okSummary,
                               const QString& failSummary) {
        return wrapResult(raw, okSummary, failSummary);
    };

    if (toolName == QLatin1String("create_file"))
        return wrapSimpleResult(FileTool::executeCreateFile(args), QStringLiteral("[OK] 文件已创建"), QStringLiteral("[FAIL] 创建文件失败"));
    if (toolName == QLatin1String("view_file")) {
        const QString raw = FileTool::executeViewFile(args);
        return wrapResult(
            raw,
            QStringLiteral("[OK] 已读取 %1").arg(args.value(QStringLiteral("file_path")).toString()),
            QStringLiteral("[FAIL] 读取文件失败"));
    }
    if (toolName == QLatin1String("read_file_lines"))
        return wrapSimpleResult(FileTool::executeReadFileLines(args), QStringLiteral("[OK] 已读取指定行"), QStringLiteral("[FAIL] 读取文件行失败"));
    if (toolName == QLatin1String("replace_in_file"))
        return wrapSimpleResult(FileTool::executeReplaceInFile(args), QStringLiteral("[OK] 替换完成"), QStringLiteral("[FAIL] 替换失败"));
    if (toolName == QLatin1String("delete_file"))
        return wrapSimpleResult(FileTool::executeDeleteFile(args), QStringLiteral("[OK] 删除完成"), QStringLiteral("[FAIL] 删除失败"));
    if (toolName == QLatin1String("list_directory"))
        return wrapSimpleResult(FileTool::executeListDirectory(args), QStringLiteral("[OK] 已列出目录"), QStringLiteral("[FAIL] 列出目录失败"));
    if (toolName == QLatin1String("grep_search"))
        return wrapSimpleResult(FileTool::executeGrepSearch(args), QStringLiteral("[OK] 搜索完成"), QStringLiteral("[FAIL] 搜索失败"));
    if (toolName == QLatin1String("find_by_name"))
        return wrapSimpleResult(FileTool::executeFindByName(args), QStringLiteral("[OK] 搜索完成"), QStringLiteral("[FAIL] 搜索失败"));
    if (toolName == QLatin1String("insert_content"))
        return wrapSimpleResult(FileTool::executeInsertContent(args), QStringLiteral("[OK] 插入完成"), QStringLiteral("[FAIL] 插入失败"));
    if (toolName == QLatin1String("multi_replace_in_file"))
        return wrapSimpleResult(FileTool::executeMultiReplaceInFile(args), QStringLiteral("[OK] 多处替换完成"), QStringLiteral("[FAIL] 多处替换失败"));
    if (toolName == QLatin1String("send_file")) {
        const QString raw = FileTool::executeSendFile(args);
        const bool ok = isOkResult(raw);
        QString summary = ok
            ? QStringLiteral("[OK] 文件已发送: %1").arg(args.value(QStringLiteral("file_name")).toString())
            : QStringLiteral("[FAIL] 发送文件失败");
        QJsonObject data;
        if (ok) {
            const int pathStart = raw.indexOf(QStringLiteral("文件已发送 ")) + 6;
            const int pathEnd = raw.indexOf(QStringLiteral(" ("), pathStart);
            const QString filePath = raw.mid(pathStart, pathEnd - pathStart);
            QFileInfo fileInfo(filePath);
            data.insert(QStringLiteral("file_path"), filePath);
            data.insert(QStringLiteral("file_name"), args.value(QStringLiteral("file_name")).toString());
            data.insert(QStringLiteral("file_size"), fileInfo.size());
            data.insert(QStringLiteral("description"), args.value(QStringLiteral("description")).toString());
        }
        return ToolResult(raw, summary, ok, data);
    }

    if (toolName == QLatin1String("execute_command"))
        return wrapSimpleResult(ShellTool::execute(args), QStringLiteral("[OK] 命令执行完成"), QStringLiteral("[FAIL] 命令执行失败"));

    if (toolName == QLatin1String("view_file_outline"))
        return wrapSimpleResult(CodeParserTool::executeViewFileOutline(args), QStringLiteral("[OK] 已生成大纲"), QStringLiteral("[FAIL] 生成大纲失败"));
    if (toolName == QLatin1String("view_code_item"))
        return wrapSimpleResult(CodeParserTool::executeViewCodeItem(args), QStringLiteral("[OK] 已获取代码项"), QStringLiteral("[FAIL] 获取代码项失败"));
    if (toolName == QLatin1String("lsp"))
        return wrapSimpleResult(LspTool::execute(args), QStringLiteral("[OK] LSP 请求完成"), QStringLiteral("[FAIL] LSP 请求失败"));
    if (toolName == QLatin1String("lsp_install"))
        return wrapSimpleResult(LspInstallTool::execute(args), QStringLiteral("[OK] LSP 安装已触发"), QStringLiteral("[FAIL] LSP 安装失败"));

    if (toolName == QLatin1String("web_fetch"))
        return wrapSimpleResult(WebTool::executeWebFetch(args), QStringLiteral("[OK] 网页抓取完成"), QStringLiteral("[FAIL] 网页抓取失败"));
    if (toolName == QLatin1String("websearch"))
        return wrapSimpleResult(ExternalSearchTool::executeWebSearch(args), QStringLiteral("[OK] 网页搜索完成"), QStringLiteral("[FAIL] 网页搜索失败"));

    if (toolName == QLatin1String("memory_search"))
        return wrapSimpleResult(MemoryTool::executeSearch(args), QStringLiteral("[OK] 记忆检索完成"), QStringLiteral("[FAIL] 记忆检索失败"));
    if (toolName == QLatin1String("memory_reindex"))
        return wrapSimpleResult(MemoryTool::executeRebuild(args), QStringLiteral("[OK] 记忆索引重建完成"), QStringLiteral("[FAIL] 记忆索引重建失败"));
    if (toolName == QLatin1String("memory_write"))
        return MemoryTool::executeWrite(args);
    if (toolName == QLatin1String("session_search"))
        return wrapSimpleResult(SessionSearchTool::executeSearch(args), QStringLiteral("[OK] 会话历史检索完成"), QStringLiteral("[FAIL] 会话历史检索失败"));
    if (toolName == QLatin1String("event_log")) {
        const QString raw = EventLogTool::execute(args);
        const QJsonObject parsed = QJsonDocument::fromJson(raw.toUtf8()).object();
        const bool ok = parsed.value(QStringLiteral("status")).toString() == QLatin1String("successful");
        return ToolResult(
            raw,
            ok ? QStringLiteral("[OK] 日志查询完成") : QStringLiteral("[FAIL] 日志查询失败"),
            ok);
    }

    if (toolName == QLatin1String("apply_patch"))
        return wrapSimpleResult(PatchTool::execute(args), QStringLiteral("[OK] 补丁已处理"), QStringLiteral("[FAIL] 补丁处理失败"));

    if (AgentToolNames::isDelegateTool(toolName)) {
        AgentTool tool(m_defaultAgentConfig, this, toolName, fallbackDescription);
        return tool.execute(args);
    }

    return ToolResult(
        QStringLiteral("错误: 未知的宿主工具 %1").arg(toolName),
        QStringLiteral("执行失败"),
        false);
}

void ToolDispatcher::indexProviderTools(IToolProvider* provider, const QString& providerName)
{
    const QList<Tool> tools = provider->listTools();
    for (const Tool& tool : tools) {
        indexToolSchema(tool, provider, providerName);
    }
}

void ToolDispatcher::indexToolSchema(const Tool& tool, IToolProvider* provider, const QString& providerName)
{
    if (!provider || tool.name.isEmpty())
        return;

    if (m_toolIndex.contains(tool.name)) {
        const QString existingOwner = m_toolOwners.value(tool.name);
        if (existingOwner == providerName) {
            m_toolSchemas.insert(tool.name, tool);
            return;
        }
        qWarning() << "[ToolDispatcher] 工具名冲突:" << tool.name << "provider:"
                   << providerName << "existing:" << existingOwner;
        return;
    }

    m_toolIndex.insert(tool.name, provider);
    m_toolSchemas.insert(tool.name, tool);
    m_toolOwners.insert(tool.name, providerName);
}
