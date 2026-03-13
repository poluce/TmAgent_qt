#ifndef CHAT_WIDGET_COMMAND_H
#define CHAT_WIDGET_COMMAND_H

#include <QList>
#include <QString>
#include <QStringList>

struct ChatWidgetCommand {
    QString name;
    QString description;
    QString category;
    QStringList parameterHints;
    bool hasParameters = false;
    int priority = 0;
};

class ChatWidgetCommandRegistry {
public:
    struct ParsedCommand {
        bool valid = false;
        QString commandName;
        QStringList arguments;
        QString rawText;
    };

    void registerCommand(const ChatWidgetCommand& command);
    void unregisterCommand(const QString& name);
    void clearCommands();
    QList<ChatWidgetCommand> allCommands() const;
    QList<ChatWidgetCommand> commandsInCategory(const QString& category) const;
    QList<ChatWidgetCommand> matchCommands(const QString& prefix) const;
    ChatWidgetCommand findCommand(const QString& name) const;
    bool hasCommand(const QString& name) const;
    QStringList categories() const;
    ParsedCommand parse(const QString& input) const;
    void registerDefaults();

private:
    QList<ChatWidgetCommand> m_commands;
};

#endif // CHAT_WIDGET_COMMAND_H
