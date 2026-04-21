#ifndef TMAGENT_TOOLSCHEMASUPPORT_H
#define TMAGENT_TOOLSCHEMASUPPORT_H

/**
 * @file ToolSchemaSupport.h
 * @brief Compatibility header for legacy ToolSchemaSupport usage
 * 
 * This header provides backward compatibility for code that includes
 * "ToolSchemaSupport.h" instead of "ToolSchemaBuilder.h".
 * 
 * All functionality is now provided by ToolSchemaBuilder.h
 */

#include <tmagent/support/ToolSchemaBuilder.h>

// For backward compatibility, expose TmAgent namespace functions at global scope
using TmAgent::requiredFieldsArray;
using TmAgent::stringListEnum;
using TmAgent::makePropertySchema;

// Legacy makeToolSchema function that returns Tool object
// This matches the signature from src/core/tools/ToolSchemaSupport.h
inline TmAgent::Tool makeToolSchema(const QString& name,
                                   const QString& description,
                                   const QJsonObject& properties,
                                   const QStringList& required = QStringList())
{
    return TmAgent::makeToolWithSchema(name, description, properties, required);
}

#endif // TMAGENT_TOOLSCHEMASUPPORT_H
