#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>

#include "core/agent/ToolDispatcher.h"
#include "core/agent/ToolPluginManager.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/tools/MemoryTool.h"

namespace {

int fail(const QString& expected, const QString& actual)
{
    qDebug().noquote() << "  [期望]" << expected;
    qDebug().noquote() << "  [实际]" << actual;
    std::fprintf(stderr, "  [期望] %s\n", expected.toUtf8().constData());
    std::fprintf(stderr, "  [实际] %s\n", actual.toUtf8().constData());
    std::fflush(stderr);
    return 1;
}

const Tool* findTool(const QList<Tool>& tools, const QString& name)
{
    for (const Tool& tool : tools) {
        if (tool.name == name)
            return &tool;
    }
    return nullptr;
}

bool requiredContains(const Tool& tool, const QString& field)
{
    const QJsonArray required = tool.inputSchema.value(QStringLiteral("required")).toArray();
    for (const QJsonValue& value : required) {
        if (value.toString() == field)
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    ToolDispatcher* dispatcher = ToolDispatcher::instance();
    dispatcher->clearProviders();
    dispatcher->setDefaultAgentConfig(LLMConfig {});

    MemoryTool::setWriteHandler([](const QJsonObject&) {
        return ToolResult(QStringLiteral("ok"), QStringLiteral("ok"), true);
    });

    ChatPersistenceService persistence;
    ToolPluginManager manager(dispatcher, nullptr);
    manager.setConfigObject(persistence.loadToolPluginConfigObject());
    manager.initialize();

    const QList<ToolPluginManager::ProviderBinding> bindings = manager.activeProviders();
    if (bindings.isEmpty())
        return fail(QStringLiteral("至少加载一个第一方工具插件"), QStringLiteral("0"));

    for (const ToolPluginManager::ProviderBinding& binding : bindings)
        dispatcher->registerProvider(binding.provider, binding.providerName);

    const QList<Tool> schemas = dispatcher->getAllToolSchemas();
    if (schemas.isEmpty())
        return fail(QStringLiteral("dispatcher 中存在工具 schema"), QStringLiteral("0"));

    const Tool* createTeammate = findTool(schemas, QStringLiteral("create_teammate"));
    if (!createTeammate)
        return fail(QStringLiteral("存在 create_teammate schema"), QStringLiteral("missing"));

    const QJsonObject createProps = createTeammate->inputSchema.value(QStringLiteral("properties")).toObject();
    if (!createProps.contains(QStringLiteral("name")))
        return fail(QStringLiteral("create_teammate.properties 包含 name"), QStringLiteral("missing"));
    if (!requiredContains(*createTeammate, QStringLiteral("name")))
        return fail(QStringLiteral("create_teammate.required 包含 name"), QStringLiteral("missing"));

    if (!findTool(schemas, QStringLiteral("message_teammate")))
        return fail(QStringLiteral("存在 message_teammate schema"), QStringLiteral("missing"));
    if (!findTool(schemas, QStringLiteral("list_teammates")))
        return fail(QStringLiteral("存在 list_teammates schema"), QStringLiteral("missing"));
    if (!findTool(schemas, QStringLiteral("cancel_teammate_turn")))
        return fail(QStringLiteral("存在 cancel_teammate_turn schema"), QStringLiteral("missing"));
    if (!findTool(schemas, QStringLiteral("rename_teammate")))
        return fail(QStringLiteral("存在 rename_teammate schema"), QStringLiteral("missing"));
    if (findTool(schemas, QStringLiteral("delegate_task")))
        return fail(QStringLiteral("delegate_task 不再暴露"), QStringLiteral("found"));
    if (findTool(schemas, QStringLiteral("delegate_status")))
        return fail(QStringLiteral("delegate_status 不再暴露"), QStringLiteral("found"));

    if (!findTool(schemas, QStringLiteral("create_file")))
        return fail(QStringLiteral("存在 create_file schema"), QStringLiteral("missing"));
    if (!findTool(schemas, QStringLiteral("execute_command")))
        return fail(QStringLiteral("存在 execute_command schema"), QStringLiteral("missing"));
    if (!findTool(schemas, QStringLiteral("scheduler_list")))
        return fail(QStringLiteral("存在 scheduler_list schema"), QStringLiteral("missing"));
    const Tool* schedulerCreate = findTool(schemas, QStringLiteral("scheduler_create"));
    if (!schedulerCreate)
        return fail(QStringLiteral("存在 scheduler_create schema"), QStringLiteral("missing"));
    const Tool* schedulerUpdate = findTool(schemas, QStringLiteral("scheduler_update"));
    if (!schedulerUpdate)
        return fail(QStringLiteral("存在 scheduler_update schema"), QStringLiteral("missing"));
    if (!findTool(schemas, QStringLiteral("scheduler_delete")))
        return fail(QStringLiteral("存在 scheduler_delete schema"), QStringLiteral("missing"));
    if (!findTool(schemas, QStringLiteral("scheduler_run")))
        return fail(QStringLiteral("存在 scheduler_run schema"), QStringLiteral("missing"));

    const QJsonObject schedulerCreateProps =
        schedulerCreate->inputSchema.value(QStringLiteral("properties")).toObject();
    if (!schedulerCreateProps.contains(QStringLiteral("schedule_type")))
        return fail(QStringLiteral("scheduler_create.properties 包含 schedule_type"), QStringLiteral("missing"));
    if (!schedulerCreateProps.contains(QStringLiteral("run_at")))
        return fail(QStringLiteral("scheduler_create.properties 包含 run_at"), QStringLiteral("missing"));

    const QJsonObject schedulerUpdateProps =
        schedulerUpdate->inputSchema.value(QStringLiteral("properties")).toObject();
    if (!schedulerUpdateProps.contains(QStringLiteral("schedule_type")))
        return fail(QStringLiteral("scheduler_update.properties 包含 schedule_type"), QStringLiteral("missing"));
    if (!schedulerUpdateProps.contains(QStringLiteral("run_at")))
        return fail(QStringLiteral("scheduler_update.properties 包含 run_at"), QStringLiteral("missing"));

    ToolCall blockedCall;
    blockedCall.id = QStringLiteral("tool-call-1");
    blockedCall.name = QStringLiteral("execute_command");
    blockedCall.input.insert(QStringLiteral("command"), QStringLiteral("rm -rf /"));
    blockedCall.input.insert(QStringLiteral("_agent_workspace"), QDir::currentPath());
    const ToolResult blockedResult = dispatcher->dispatch(blockedCall);
    if (blockedResult.success)
        return fail(QStringLiteral("execute_command 被策略拒绝时 success=false"), QStringLiteral("true"));
    if (blockedResult.data.value(QStringLiteral("error_category")).toString() != QStringLiteral("policy_blocked"))
        return fail(QStringLiteral("error_category=policy_blocked"),
                    blockedResult.data.value(QStringLiteral("error_category")).toString());
    if (!blockedResult.rawContent.contains(QStringLiteral("error_category: policy_blocked"))
        || !blockedResult.rawContent.contains(QStringLiteral("retryable: true"))
        || !blockedResult.rawContent.contains(QStringLiteral("ask_user_required: false"))
        || !blockedResult.rawContent.contains(QStringLiteral("next_action_hint:"))) {
        return fail(QStringLiteral("rawContent 包含结构化失败字段"), blockedResult.rawContent);
    }

    qDebug().noquote() << "ToolPlugin schema smoke test passed.";
    return 0;
}
