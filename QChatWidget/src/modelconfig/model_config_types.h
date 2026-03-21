#ifndef MODEL_CONFIG_TYPES_H
#define MODEL_CONFIG_TYPES_H

#include <QList>
#include <QString>

/**
 * @brief 厂商字段定义
 */
struct ModelConfigField {
    QString key;          // 数据键名 (如 "apiKey")
    QString label;        // 界面显示的标签 (如 "API Key")
    QString placeholder;  // 占位符
    QString defaultValue; // 默认值
    bool isPassword;      // 是否为密码输入
    bool isRequired;      // 是否必填

    ModelConfigField(const QString& k,
                     const QString& l,
                     const QString& p = "",
                     const QString& d = "",
                     bool pass = false,
                     bool req = true)
        : key(k)
        , label(l)
        , placeholder(p)
        , defaultValue(d)
        , isPassword(pass)
        , isRequired(req)
    {
    }
};

/**
 * @brief 厂商完整配置
 */
struct ModelConfigProvider {
    QString id;                     // 唯一标识 (如 "openai")
    QString name;                   // 显示名称 (如 "OpenAI")
    QString description;            // 描述
    QList<ModelConfigField> fields; // 包含的字段列表

    ModelConfigProvider() = default;
    ModelConfigProvider(const QString& i, const QString& n, const QString& d = "")
        : id(i)
        , name(n)
        , description(d)
    {
    }
};

#endif // MODEL_CONFIG_TYPES_H
