#include "chat_widget_command.h"
#include <algorithm>

void ChatWidgetCommandRegistry::registerCommand(const ChatWidgetCommand& command)
{
    for (int i = 0; i < m_commands.size(); ++i) {
        if (m_commands[i].name.compare(command.name, Qt::CaseInsensitive) == 0) {
            m_commands[i] = command;
            return;
        }
    }
    m_commands.append(command);
}

void ChatWidgetCommandRegistry::unregisterCommand(const QString& name)
{
    for (int i = 0; i < m_commands.size(); ++i) {
        if (m_commands[i].name.compare(name, Qt::CaseInsensitive) == 0) {
            m_commands.removeAt(i);
            return;
        }
    }
}

void ChatWidgetCommandRegistry::clearCommands()
{
    m_commands.clear();
}

QList<ChatWidgetCommand> ChatWidgetCommandRegistry::allCommands() const
{
    QList<ChatWidgetCommand> sorted = m_commands;
    std::sort(sorted.begin(), sorted.end(), [](const ChatWidgetCommand& a, const ChatWidgetCommand& b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.name < b.name;
    });
    return sorted;
}

QList<ChatWidgetCommand> ChatWidgetCommandRegistry::commandsInCategory(const QString& category) const
{
    QList<ChatWidgetCommand> result;
    for (const ChatWidgetCommand& cmd : m_commands) {
        if (cmd.category.compare(category, Qt::CaseInsensitive) == 0)
            result.append(cmd);
    }
    return result;
}

QList<ChatWidgetCommand> ChatWidgetCommandRegistry::matchCommands(const QString& prefix) const
{
    QList<ChatWidgetCommand> result;
    const QString normalized = prefix.startsWith('/') ? prefix.mid(1) : prefix;
    for (const ChatWidgetCommand& cmd : m_commands) {
        const QString cmdName = cmd.name.startsWith('/') ? cmd.name.mid(1) : cmd.name;
        if (normalized.isEmpty() || cmdName.startsWith(normalized, Qt::CaseInsensitive))
            result.append(cmd);
    }
    std::sort(result.begin(), result.end(), [](const ChatWidgetCommand& a, const ChatWidgetCommand& b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.name < b.name;
    });
    return result;
}

ChatWidgetCommand ChatWidgetCommandRegistry::findCommand(const QString& name) const
{
    const QString normalized = name.startsWith('/') ? name : ('/' + name);
    for (const ChatWidgetCommand& cmd : m_commands) {
        if (cmd.name.compare(normalized, Qt::CaseInsensitive) == 0)
            return cmd;
    }
    return ChatWidgetCommand();
}

bool ChatWidgetCommandRegistry::hasCommand(const QString& name) const
{
    const QString normalized = name.startsWith('/') ? name : ('/' + name);
    for (const ChatWidgetCommand& cmd : m_commands) {
        if (cmd.name.compare(normalized, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QStringList ChatWidgetCommandRegistry::categories() const
{
    QStringList cats;
    for (const ChatWidgetCommand& cmd : m_commands) {
        if (!cmd.category.isEmpty() && !cats.contains(cmd.category, Qt::CaseInsensitive))
            cats.append(cmd.category);
    }
    return cats;
}

ChatWidgetCommandRegistry::ParsedCommand ChatWidgetCommandRegistry::parse(const QString& input) const
{
    ParsedCommand result;
    result.rawText = input;

    const QString trimmed = input.trimmed();
    if (!trimmed.startsWith('/'))
        return result;

    const QStringList parts = trimmed.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return result;

    result.commandName = parts.first();
    if (!hasCommand(result.commandName))
        return result;

    result.valid = true;
    for (int i = 1; i < parts.size(); ++i)
        result.arguments.append(parts[i]);

    return result;
}

void ChatWidgetCommandRegistry::registerDefaults()
{
    // 不再注册硬编码命令，由宿主通过 registerCommand() 注入
}
