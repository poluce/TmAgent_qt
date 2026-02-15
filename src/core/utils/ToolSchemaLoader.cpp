#include "ToolSchemaLoader.h"
#include <QFile>
#include <QDebug>
#include <QJsonArray>
#include <yaml-cpp/yaml.h>

// 静态成员初始化
QMap<QString, Tool> ToolSchemaLoader::s_toolCache;
QString ToolSchemaLoader::s_lastLoadedPath;

QVector<Tool> ToolSchemaLoader::loadFromFile(const QString& yamlPath) {
    QVector<Tool> tools;

    QFile file(yamlPath);
    if (!file.exists()) {
        qWarning() << "[ToolSchemaLoader] YAML 文件不存在:" << yamlPath;
        return tools;
    }

    try {
        YAML::Node root = YAML::LoadFile(yamlPath.toStdString());

        if (!root["tools"]) {
            qWarning() << "[ToolSchemaLoader] YAML 文件缺少 'tools' 节点";
            return tools;
        }

        for (const auto& toolNode : root["tools"]) {
            Tool tool;
            tool.name = QString::fromStdString(toolNode["name"].as<std::string>());
            tool.description = QString::fromStdString(toolNode["description"].as<std::string>());

            QJsonObject properties;
            QJsonArray required;

            if (toolNode["parameters"]) {
                for (const auto& paramNode : toolNode["parameters"]) {
                    QString paramName = QString::fromStdString(paramNode["name"].as<std::string>());
                    QString paramType = QString::fromStdString(paramNode["type"].as<std::string>());
                    QString paramDesc = QString::fromStdString(paramNode["description"].as<std::string>(""));
                    bool isRequired = paramNode["required"].as<bool>(false);

                    QJsonObject paramSchema;
                    paramSchema["type"] = paramType;
                    if (!paramDesc.isEmpty())
                        paramSchema["description"] = paramDesc;

                    if (paramType == "array" && paramNode["items"]) {
                        YAML::Node itemsNode = paramNode["items"];
                        QJsonObject itemsSchema;
                        itemsSchema["type"] = QString::fromStdString(itemsNode["type"].as<std::string>("object"));

                        if (itemsNode["properties"]) {
                            QJsonObject nestedProps;
                            QJsonArray nestedRequired;
                            for (const auto& nestedProp : itemsNode["properties"]) {
                                QString nestedName = QString::fromStdString(nestedProp["name"].as<std::string>());
                                QJsonObject nestedSchema;
                                nestedSchema["type"] = QString::fromStdString(nestedProp["type"].as<std::string>("string"));
                                if (nestedProp["description"])
                                    nestedSchema["description"] = QString::fromStdString(nestedProp["description"].as<std::string>());
                                nestedProps[nestedName] = nestedSchema;
                                nestedRequired.append(nestedName);
                            }
                            itemsSchema["properties"] = nestedProps;
                            if (!nestedRequired.isEmpty())
                                itemsSchema["required"] = nestedRequired;
                        }
                        paramSchema["items"] = itemsSchema;
                    }

                    properties[paramName] = paramSchema;
                    if (isRequired)
                        required.append(paramName);
                }
            }

            QJsonObject inputSchema;
            inputSchema["type"] = "object";
            inputSchema["properties"] = properties;
            inputSchema["required"] = required;
            tool.inputSchema = inputSchema;

            tools.append(tool);
            s_toolCache[tool.name] = tool;
        }

        s_lastLoadedPath = yamlPath;
        qInfo() << "[ToolSchemaLoader] 成功加载" << tools.size() << "个工具定义";

    } catch (const YAML::Exception& e) {
        qCritical() << "[ToolSchemaLoader] YAML 解析错误:" << e.what();
    }

    return tools;
}

Tool ToolSchemaLoader::getToolSchema(const QString& name) {
    if (s_toolCache.contains(name)) {
        return s_toolCache[name];
    }
    
    qWarning() << "[ToolSchemaLoader] 未找到工具:" << name;
    return Tool();
}

QVector<Tool> ToolSchemaLoader::getAllTools() {
    QVector<Tool> tools;
    for (const Tool& tool : s_toolCache) {
        tools.append(tool);
    }
    return tools;
}

void ToolSchemaLoader::reload(const QString& yamlPath) {
    s_toolCache.clear();
    loadFromFile(yamlPath);
}
