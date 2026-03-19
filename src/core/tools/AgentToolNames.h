#ifndef AGENTTOOLNAMES_H
#define AGENTTOOLNAMES_H

#include <QStringList>
#include <QLatin1String>

/**
 * @brief 所有 delegate/teammate 工具名的唯一权威列表
 *
 * AgentRuntime、LLMAgent、ToolDispatcher、AgentTool 均引用此处，
 * 新增或删除工具只需修改这一个文件。
 */
namespace AgentToolNames {

inline const QStringList& all()
{
    static const QStringList names = {
        QStringLiteral("delegate_task"),
        QStringLiteral("delegate_status"),
        QStringLiteral("delegate_cancel"),
        QStringLiteral("delegate_list_active"),
        QStringLiteral("create_teammate"),
        QStringLiteral("message_teammate"),
        QStringLiteral("list_teammates"),
        QStringLiteral("remove_teammate"),
        QStringLiteral("rename_teammate"),
        QStringLiteral("get_teammate_status"),
        QStringLiteral("message_between_teammates")
    };
    return names;
}

inline bool isDelegateTool(const QString& name)
{
    return all().contains(name);
}

} // namespace AgentToolNames

#endif // AGENTTOOLNAMES_H
