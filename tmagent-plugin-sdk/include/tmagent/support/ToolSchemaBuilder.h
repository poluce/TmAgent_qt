#ifndef TMAGENT_TOOLSCHEMABUILDER_H
#define TMAGENT_TOOLSCHEMABUILDER_H

#include <tmagent/types/ToolTypes.h>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace TmAgent {

/**
 * @brief Helper functions for building JSON Schema for tool parameters
 * 
 * These functions simplify the creation of JSON Schema objects that define
 * tool input parameters according to the JSON Schema specification.
 */

/**
 * @brief Convert a list of field names to a JSON array for "required" field
 * 
 * @param fields List of required field names
 * @return QJsonArray containing the field names
 */
inline QJsonArray requiredFieldsArray(const QStringList& fields)
{
    QJsonArray required;
    for (const QString& field : fields)
        required.append(field);
    return required;
}

/**
 * @brief Create an enum constraint object from a list of string values
 * 
 * @param values List of allowed enum values
 * @return QJsonObject with "enum" field containing the values
 */
inline QJsonObject stringListEnum(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values)
        array.append(value);

    QJsonObject out;
    out.insert(QStringLiteral("enum"), array);
    return out;
}

/**
 * @brief Create a property schema for a single parameter
 * 
 * @param type The JSON Schema type (string, number, boolean, object, array)
 * @param description Human-readable description of the parameter
 * @param extra Additional schema properties (e.g., enum, pattern, minimum, maximum)
 * @return QJsonObject representing the property schema
 */
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

/**
 * @brief Create a property schema with enum constraint
 * 
 * @param type The JSON Schema type (typically "string")
 * @param description Human-readable description of the parameter
 * @param enumValues List of allowed values
 * @return QJsonObject representing the property schema with enum constraint
 */
inline QJsonObject makePropertySchema(const QString& type, const QString& description, 
                                     const QStringList& enumValues) {
    QJsonObject schema = makePropertySchema(type, description);
    QJsonArray enumArray;
    for (const QString& value : enumValues) {
        enumArray.append(value);
    }
    schema["enum"] = enumArray;
    return schema;
}

/**
 * @brief Create a string property schema with pattern constraint
 * 
 * @param description Human-readable description of the parameter
 * @param pattern Regular expression pattern the string must match
 * @return QJsonObject representing the string property schema with pattern
 */
inline QJsonObject makeStringPropertySchema(const QString& description, const QString& pattern) {
    QJsonObject schema = makePropertySchema("string", description);
    if (!pattern.isEmpty()) {
        schema["pattern"] = pattern;
    }
    return schema;
}

/**
 * @brief Create a number property schema with min/max constraints
 * 
 * @param description Human-readable description of the parameter
 * @param minimum Minimum allowed value (optional, use QVariant() to omit)
 * @param maximum Maximum allowed value (optional, use QVariant() to omit)
 * @return QJsonObject representing the number property schema with constraints
 */
inline QJsonObject makeNumberPropertySchema(const QString& description, 
                                           const QVariant& minimum = QVariant(),
                                           const QVariant& maximum = QVariant()) {
    QJsonObject schema = makePropertySchema("number", description);
    if (minimum.isValid()) {
        schema["minimum"] = minimum.toDouble();
    }
    if (maximum.isValid()) {
        schema["maximum"] = maximum.toDouble();
    }
    return schema;
}

/**
 * @brief Create an array property schema
 * 
 * @param description Human-readable description of the parameter
 * @param itemSchema Schema for array items
 * @return QJsonObject representing the array property schema
 */
inline QJsonObject makeArrayPropertySchema(const QString& description, 
                                          const QJsonObject& itemSchema) {
    QJsonObject schema = makePropertySchema("array", description);
    schema["items"] = itemSchema;
    return schema;
}

/**
 * @brief Create an object property schema
 * 
 * @param description Human-readable description of the parameter
 * @param properties Map of property names to their schemas
 * @param required List of required property names
 * @return QJsonObject representing the object property schema
 */
inline QJsonObject makeObjectPropertySchema(const QString& description,
                                           const QJsonObject& properties,
                                           const QStringList& required = QStringList()) {
    QJsonObject schema = makePropertySchema("object", description);
    schema["properties"] = properties;
    if (!required.isEmpty()) {
        QJsonArray requiredArray;
        for (const QString& field : required) {
            requiredArray.append(field);
        }
        schema["required"] = requiredArray;
    }
    return schema;
}

/**
 * @brief Create a complete tool schema (returns QJsonObject for inputSchema field)
 * 
 * @param name Tool name (must be unique) - not used in schema, for documentation only
 * @param description Tool description - not used in schema, for documentation only  
 * @param properties Map of parameter names to their property schemas
 * @param required List of required parameter names
 * @return QJsonObject representing the complete tool input schema
 */
inline QJsonObject makeToolInputSchema(const QString& name,
                                       const QString& description,
                                       const QJsonObject& properties,
                                       const QStringList& required = QStringList()) {
    Q_UNUSED(name);
    Q_UNUSED(description);
    
    QJsonObject schema;
    schema["type"] = "object";
    schema["properties"] = properties;
    
    if (!required.isEmpty()) {
        QJsonArray requiredArray;
        for (const QString& field : required) {
            requiredArray.append(field);
        }
        schema["required"] = requiredArray;
    }
    
    return schema;
}

/**
 * @brief Create a complete Tool object with schema (compatibility overload)
 * 
 * This overload returns a Tool object instead of just the schema,
 * providing compatibility with legacy code.
 * 
 * @param name Tool name (must be unique)
 * @param description Human-readable description of what the tool does
 * @param properties Map of parameter names to their property schemas
 * @param required List of required parameter names
 * @return Tool object with name, description, and inputSchema populated
 */
inline Tool makeToolWithSchema(const QString& name,
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

} // namespace TmAgent

#endif // TMAGENT_TOOLSCHEMABUILDER_H
