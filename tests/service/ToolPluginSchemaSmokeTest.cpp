#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

#include "core/agent/ToolDispatcher.h"
#include "core/agent/ToolPluginManager.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/tools/MemoryTool.h"

namespace {

int fail(const QString& expected, const QString& actual)
{
    qDebug().noquote() << "  [期望]" << expected;
    qDebug().noquote() << "  [实际]" << actual;
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
    if (!findTool(schemas, QStringLiteral("rename_teammate")))
        return fail(QStringLiteral("存在 rename_teammate schema"), QStringLiteral("missing"));

    if (!findTool(schemas, QStringLiteral("create_file")))
        return fail(QStringLiteral("存在 create_file schema"), QStringLiteral("missing"));
    if (!findTool(schemas, QStringLiteral("execute_command")))
        return fail(QStringLiteral("存在 execute_command schema"), QStringLiteral("missing"));

    qDebug().noquote() << "ToolPlugin schema smoke test passed.";
    return 0;
}
