#ifndef TOOLSCHEMALOADER_H
#define TOOLSCHEMALOADER_H

#include "core/agent/ToolTypes.h"
#include <QMap>
#include <QString>
#include <QVector>

/**
 * @brief 从 YAML 文件加载工具定义，转换为 Tool 对象
 */
class ToolSchemaLoader {
public:
    static QVector<Tool> loadFromFile(const QString& yamlPath);
    static Tool getToolSchema(const QString& name);
    static QVector<Tool> getAllTools();
    static void reload(const QString& yamlPath);

private:
    static QMap<QString, Tool> s_toolCache;
};

#endif // TOOLSCHEMALOADER_H
