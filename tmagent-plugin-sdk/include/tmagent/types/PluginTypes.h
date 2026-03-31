#ifndef TMAGENT_TYPES_PLUGINTYPES_H
#define TMAGENT_TYPES_PLUGINTYPES_H

#include <QString>
#include <QStringList>
#include <QJsonObject>

namespace TmAgent {

/**
 * @brief ToolPluginDescriptor 结构 - 工具插件元数据
 * 
 * 描述工具插件的基本信息，包括标识、版本、提供的工具列表等。
 */
struct ToolPluginDescriptor {
    QString pluginId;            // 插件唯一标识（kebab-case）
    QString displayName;         // 显示名称
    QString version;             // 插件版本（语义化版本）
    QString description;         // 插件描述
    QString category;            // 插件分类（如 "tools", "utilities"）
    QStringList toolNames;       // 提供的工具名称列表
    QJsonObject configSchema;    // 配置 Schema（JSON Schema 格式）
    
    // SDK 版本兼容性
    int sdkVersionMajor = 1;     // SDK 主版本号
    int sdkVersionMinor = 0;     // SDK 次版本号
    
    /**
     * @brief 验证元数据有效性
     * @return true 如果元数据有效
     */
    bool isValid() const {
        return !pluginId.trimmed().isEmpty() 
            && !displayName.trimmed().isEmpty()
            && !version.trimmed().isEmpty();
    }
};

/**
 * @brief ToolPluginHealth 结构 - 插件健康状态
 * 
 * 表示插件的运行健康状态和诊断信息。
 */
struct ToolPluginHealth {
    QString status;              // 状态："healthy", "degraded", "unhealthy"
    QStringList diagnostics;     // 诊断信息列表
    
    // 便捷构造函数
    ToolPluginHealth(const QString& s = "healthy", 
                     const QStringList& diag = QStringList())
        : status(s), diagnostics(diag) 
    {}
    
    /**
     * @brief 检查是否健康
     * @return true 如果状态为 "healthy"
     */
    bool isHealthy() const {
        return status == "healthy";
    }
};

} // namespace TmAgent

#endif // TMAGENT_TYPES_PLUGINTYPES_H
