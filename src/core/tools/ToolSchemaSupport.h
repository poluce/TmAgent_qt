#ifndef TOOLSCHEMASUPPORT_H
#define TOOLSCHEMASUPPORT_H

#include "core/agent/ToolTypes.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

inline QJsonArray requiredFieldsArray(const QStringList& fields)
{
    QJsonArray required;
    for (const QString& field : fields)
        required.append(field);
    return required;
}

inline QJsonObject stringListEnum(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values)
        array.append(value);

    QJsonObject out;
    out.insert(QStringLiteral("enum"), array);
    return out;
}

inline QJsonObject makePropertySchema(const QString& type,
                                      const QString& description,
                                      const QJsonObject& extra = QJsonObject())
{
    QJsonObject schema;
    schema.insert(QStringLiteral("type"), type);
    if (!description.trimmed().isEmpty())
        schema.insert(QStringLiteral("description"), description);
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        schema.insert(it.key(), it.value());
    return schema;
}

inline Tool makeToolSchema(const QString& name,
                           const QString& description,
                           const QJsonObject& properties,
                           const QStringList& required = QStringList())
{
    Tool tool;
    tool.name = name;
    tool.description = description;

    QJsonObject inputSchema;
    inputSchema.insert(QStringLiteral("type"), QStringLiteral("object"));
    inputSchema.insert(QStringLiteral("properties"), properties);
    inputSchema.insert(QStringLiteral("required"), requiredFieldsArray(required));
    tool.inputSchema = inputSchema;
    return tool;
}

#endif // TOOLSCHEMASUPPORT_H
