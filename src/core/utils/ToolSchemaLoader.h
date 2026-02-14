#ifndef TOOLSCHEMALOADER_H
#define TOOLSCHEMALOADER_H

#include <QString>
#include <QVector>
#include <QMap>
#include "core/agent/ToolTypes.h"

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
    static QString s_lastLoadedPath;
};

#endif // TOOLSCHEMALOADER_H
