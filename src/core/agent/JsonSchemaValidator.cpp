#include "JsonSchemaValidator.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QDebug>

JsonSchemaValidator::JsonSchemaValidator(const QJsonObject& schema)
    : m_schema(schema)
{
}

bool JsonSchemaValidator::validate(const QJsonValue& data)
{
    m_errors.clear();
    
    if (m_schema.isEmpty()) {
        // 空 Schema 表示接受任何数据
        return true;
    }
    
    return validateObject(data.toObject(), m_schema, "$");
}

QString JsonSchemaValidator::errorString() const
{
    if (m_errors.isEmpty()) {
        return QString();
    }
    return m_errors.join("; ");
}

bool JsonSchemaValidator::validateType(const QJsonValue& data, const QString& expectedType, const QString& path)
{
    const QString actualType = typeOf(data);
    
    if (expectedType == "string" && !data.isString()) {
        addError(path, QString("expected string, got %1").arg(actualType));
        return false;
    }
    if (expectedType == "number" && !data.isDouble()) {
        addError(path, QString("expected number, got %1").arg(actualType));
        return false;
    }
    if (expectedType == "integer" && (!data.isDouble() || data.toDouble() != qint64(data.toDouble()))) {
        addError(path, QString("expected integer, got %1").arg(actualType));
        return false;
    }
    if (expectedType == "boolean" && !data.isBool()) {
        addError(path, QString("expected boolean, got %1").arg(actualType));
        return false;
    }
    if (expectedType == "object" && !data.isObject()) {
        addError(path, QString("expected object, got %1").arg(actualType));
        return false;
    }
    if (expectedType == "array" && !data.isArray()) {
        addError(path, QString("expected array, got %1").arg(actualType));
        return false;
    }
    if (expectedType == "null" && !data.isNull()) {
        addError(path, QString("expected null, got %1").arg(actualType));
        return false;
    }
    
    return true;
}

bool JsonSchemaValidator::validateObject(const QJsonObject& obj, const QJsonObject& schema, const QString& path)
{
    bool valid = true;
    
    // 需求 17.4: 限制对象属性数量
    if (obj.size() > MAX_OBJECT_PROPERTIES) {
        addError(path, QString("object has too many properties (%1 > %2)")
                 .arg(obj.size()).arg(MAX_OBJECT_PROPERTIES));
        return false;
    }
    
    // 验证类型
    if (schema.contains("type")) {
        const QString type = schema.value("type").toString();
        if (!type.isEmpty() && !validateType(obj, type, path)) {
            valid = false;
        }
    }
    
    // 验证必需字段
    if (schema.contains("required")) {
        const QJsonArray required = schema.value("required").toArray();
        for (const QJsonValue& reqValue : required) {
            const QString reqField = reqValue.toString();
            if (!obj.contains(reqField)) {
                addError(path, QString("missing required property '%1'").arg(reqField));
                valid = false;
            }
        }
    }
    
    // 验证属性
    if (schema.contains("properties")) {
        const QJsonObject properties = schema.value("properties").toObject();
        
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            const QString& key = it.key();
            const QJsonValue& value = it.value();
            
            if (properties.contains(key)) {
                const QJsonObject propSchema = properties.value(key).toObject();
                const QString propPath = path + "." + key;
                
                // 验证属性类型
                if (propSchema.contains("type")) {
                    const QString propType = propSchema.value("type").toString();
                    
                    if (propType == "string" && value.isString()) {
                        if (!validateString(value.toString(), propSchema, propPath)) {
                            valid = false;
                        }
                    } else if (propType == "number" && value.isDouble()) {
                        if (!validateNumber(value.toDouble(), propSchema, propPath)) {
                            valid = false;
                        }
                    } else if (propType == "integer" && value.isDouble()) {
                        if (!validateNumber(value.toDouble(), propSchema, propPath)) {
                            valid = false;
                        }
                    } else if (propType == "boolean" && !value.isBool()) {
                        addError(propPath, "expected boolean");
                        valid = false;
                    } else if (propType == "object" && value.isObject()) {
                        if (!validateObject(value.toObject(), propSchema, propPath)) {
                            valid = false;
                        }
                    } else if (propType == "array" && value.isArray()) {
                        if (!validateArray(value.toArray(), propSchema, propPath)) {
                            valid = false;
                        }
                    } else if (!validateType(value, propType, propPath)) {
                        valid = false;
                    }
                }
                
                // 验证枚举值
                if (propSchema.contains("enum")) {
                    if (!validateEnum(value, propSchema.value("enum").toArray(), propPath)) {
                        valid = false;
                    }
                }
            }
        }
    }
    
    return valid;
}

bool JsonSchemaValidator::validateArray(const QJsonArray& arr, const QJsonObject& schema, const QString& path)
{
    bool valid = true;
    
    // 需求 17.4: 限制数组大小
    if (arr.size() > MAX_ARRAY_SIZE) {
        addError(path, QString("array is too large (%1 > %2)")
                 .arg(arr.size()).arg(MAX_ARRAY_SIZE));
        return false;
    }
    
    // 验证数组大小限制
    if (schema.contains("minItems")) {
        const int minItems = schema.value("minItems").toInt();
        if (arr.size() < minItems) {
            addError(path, QString("array has too few items (%1 < %2)")
                     .arg(arr.size()).arg(minItems));
            valid = false;
        }
    }
    
    if (schema.contains("maxItems")) {
        const int maxItems = schema.value("maxItems").toInt();
        if (arr.size() > maxItems) {
            addError(path, QString("array has too many items (%1 > %2)")
                     .arg(arr.size()).arg(maxItems));
            valid = false;
        }
    }
    
    // 验证数组元素
    if (schema.contains("items")) {
        const QJsonObject itemSchema = schema.value("items").toObject();
        
        for (int i = 0; i < arr.size(); ++i) {
            const QJsonValue& item = arr.at(i);
            const QString itemPath = QString("%1[%2]").arg(path).arg(i);
            
            if (itemSchema.contains("type")) {
                const QString itemType = itemSchema.value("type").toString();
                
                if (itemType == "string" && item.isString()) {
                    if (!validateString(item.toString(), itemSchema, itemPath)) {
                        valid = false;
                    }
                } else if (itemType == "number" && item.isDouble()) {
                    if (!validateNumber(item.toDouble(), itemSchema, itemPath)) {
                        valid = false;
                    }
                } else if (itemType == "object" && item.isObject()) {
                    if (!validateObject(item.toObject(), itemSchema, itemPath)) {
                        valid = false;
                    }
                } else if (itemType == "array" && item.isArray()) {
                    if (!validateArray(item.toArray(), itemSchema, itemPath)) {
                        valid = false;
                    }
                } else if (!validateType(item, itemType, itemPath)) {
                    valid = false;
                }
            }
        }
    }
    
    return valid;
}

bool JsonSchemaValidator::validateString(const QString& str, const QJsonObject& schema, const QString& path)
{
    bool valid = true;
    
    // 需求 17.3: 限制字符串长度不超过 1MB
    if (str.size() > MAX_STRING_LENGTH) {
        addError(path, QString("string is too long (%1 > %2 characters)")
                 .arg(str.size()).arg(MAX_STRING_LENGTH));
        return false;
    }
    
    // 验证最小长度
    if (schema.contains("minLength")) {
        const int minLength = schema.value("minLength").toInt();
        if (str.size() < minLength) {
            addError(path, QString("string is too short (%1 < %2 characters)")
                     .arg(str.size()).arg(minLength));
            valid = false;
        }
    }
    
    // 验证最大长度
    if (schema.contains("maxLength")) {
        const int maxLength = schema.value("maxLength").toInt();
        if (str.size() > maxLength) {
            addError(path, QString("string is too long (%1 > %2 characters)")
                     .arg(str.size()).arg(maxLength));
            valid = false;
        }
    }
    
    // 验证正则表达式模式
    if (schema.contains("pattern")) {
        const QString pattern = schema.value("pattern").toString();
        QRegularExpression regex(pattern);
        if (!regex.match(str).hasMatch()) {
            addError(path, QString("string does not match pattern '%1'").arg(pattern));
            valid = false;
        }
    }
    
    return valid;
}

bool JsonSchemaValidator::validateNumber(double num, const QJsonObject& schema, const QString& path)
{
    bool valid = true;
    
    // 验证最小值
    if (schema.contains("minimum")) {
        const double minimum = schema.value("minimum").toDouble();
        if (num < minimum) {
            addError(path, QString("number is too small (%1 < %2)")
                     .arg(num).arg(minimum));
            valid = false;
        }
    }
    
    // 验证最大值
    if (schema.contains("maximum")) {
        const double maximum = schema.value("maximum").toDouble();
        if (num > maximum) {
            addError(path, QString("number is too large (%1 > %2)")
                     .arg(num).arg(maximum));
            valid = false;
        }
    }
    
    // 验证排他最小值
    if (schema.contains("exclusiveMinimum")) {
        const double exclusiveMinimum = schema.value("exclusiveMinimum").toDouble();
        if (num <= exclusiveMinimum) {
            addError(path, QString("number must be greater than %1").arg(exclusiveMinimum));
            valid = false;
        }
    }
    
    // 验证排他最大值
    if (schema.contains("exclusiveMaximum")) {
        const double exclusiveMaximum = schema.value("exclusiveMaximum").toDouble();
        if (num >= exclusiveMaximum) {
            addError(path, QString("number must be less than %1").arg(exclusiveMaximum));
            valid = false;
        }
    }
    
    return valid;
}

bool JsonSchemaValidator::validateEnum(const QJsonValue& data, const QJsonArray& enumValues, const QString& path)
{
    for (const QJsonValue& enumValue : enumValues) {
        if (data == enumValue) {
            return true;
        }
    }
    
    addError(path, QString("value is not one of the allowed enum values"));
    return false;
}

void JsonSchemaValidator::addError(const QString& path, const QString& message)
{
    m_errors.append(QString("%1: %2").arg(path, message));
}

QString JsonSchemaValidator::typeOf(const QJsonValue& value) const
{
    if (value.isNull()) return "null";
    if (value.isBool()) return "boolean";
    if (value.isDouble()) return "number";
    if (value.isString()) return "string";
    if (value.isArray()) return "array";
    if (value.isObject()) return "object";
    return "unknown";
}
