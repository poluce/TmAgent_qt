#ifndef JSONSCHEMAVALIDATOR_H
#define JSONSCHEMAVALIDATOR_H

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

/**
 * @brief 简单的 JSON Schema 验证器
 * 
 * 支持 JSON Schema Draft 7 的基本特性：
 * - 类型验证（string, number, boolean, object, array, null）
 * - 必需字段验证（required）
 * - 字符串长度限制（minLength, maxLength）
 * - 数组大小限制（minItems, maxItems）
 * - 数值范围限制（minimum, maximum）
 * - 枚举值验证（enum）
 * - 嵌套对象和数组
 * 
 * 需求: 17.1-17.4, 60.1-60.5
 */
class JsonSchemaValidator {
public:
    explicit JsonSchemaValidator(const QJsonObject& schema);
    
    /**
     * @brief 验证 JSON 数据是否符合 Schema
     * @param data 待验证的 JSON 数据
     * @return true 如果验证通过
     */
    bool validate(const QJsonValue& data);
    
    /**
     * @brief 获取最后一次验证的错误信息
     * @return 错误信息列表
     */
    QStringList errors() const { return m_errors; }
    
    /**
     * @brief 获取格式化的错误信息
     * @return 单个字符串，包含所有错误
     */
    QString errorString() const;
    
private:
    bool validateType(const QJsonValue& data, const QString& expectedType, const QString& path);
    bool validateObject(const QJsonObject& obj, const QJsonObject& schema, const QString& path);
    bool validateArray(const QJsonArray& arr, const QJsonObject& schema, const QString& path);
    bool validateString(const QString& str, const QJsonObject& schema, const QString& path);
    bool validateNumber(double num, const QJsonObject& schema, const QString& path);
    bool validateEnum(const QJsonValue& data, const QJsonArray& enumValues, const QString& path);
    
    void addError(const QString& path, const QString& message);
    QString typeOf(const QJsonValue& value) const;
    
    QJsonObject m_schema;
    QStringList m_errors;
    
    // 安全限制常量
    static constexpr int MAX_STRING_LENGTH = 1024 * 1024;  // 1MB
    static constexpr int MAX_ARRAY_SIZE = 10000;           // 10000 items
    static constexpr int MAX_OBJECT_PROPERTIES = 1000;     // 1000 properties
};

#endif // JSONSCHEMAVALIDATOR_H
