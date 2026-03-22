#ifndef AGENTTOOLNAMES_H
#define AGENTTOOLNAMES_H

#include <QStringList>
#include <QLatin1String>

/**
 * @brief 所有 Team 协作工具名的唯一权威列表
 *
 * AgentRuntime、LLMAgent、ToolDispatcher、AgentTool 均引用此处，
 * 新增或删除工具只需修改这一个文件。
 */
namespace AgentToolNames {

inline const QStringList& all()
{
    static const QStringList names = {
        QStringLiteral("create_teammate"),
        QStringLiteral("message_teammate"),
        QStringLiteral("list_teammates"),
        QStringLiteral("cancel_teammate_turn"),
        QStringLiteral("remove_teammate"),
        QStringLiteral("rename_teammate"),
        QStringLiteral("get_teammate_status"),
        QStringLiteral("message_between_teammates")
    };
    return names;
}

inline bool isTeamTool(const QString& name)
{
    return all().contains(name);
}

inline bool isDelegateTool(const QString& name)
{
    return isTeamTool(name);
}

} // namespace AgentToolNames

#endif // AGENTTOOLNAMES_H
