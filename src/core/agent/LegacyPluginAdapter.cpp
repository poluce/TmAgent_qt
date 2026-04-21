#include "LegacyPluginAdapter.h"
#include "IToolProvider.h"
#include <QDebug>

LegacyPluginAdapter::LegacyPluginAdapter(::IToolPlugin* legacyPlugin, QObject* parent)
    : QObject(parent)
    , m_legacyPlugin(legacyPlugin)
{
    Q_ASSERT(legacyPlugin != nullptr);
    
    qWarning() << "[LegacyPluginAdapter] Wrapping legacy plugin interface."
               << "This plugin should be migrated to the SDK interface.";
}

TmAgent::ToolPluginDescriptor LegacyPluginAdapter::descriptor() const
{
    if (!m_legacyPlugin) {
        qCritical() << "[LegacyPluginAdapter] Legacy plugin pointer is null";
        return TmAgent::ToolPluginDescriptor();
    }
    
    const ::ToolPluginDescriptor oldDesc = m_legacyPlugin->descriptor();
    return convertDescriptor(oldDesc);
}

TmAgent::IToolProvider* LegacyPluginAdapter::createProvider(TmAgent::IToolPluginHost* host, 
                                                            QObject* parent)
{
    if (!m_legacyPlugin) {
        qCritical() << "[LegacyPluginAdapter] Legacy plugin pointer is null";
        return nullptr;
    }
    
    // 旧接口和新接口的 IToolPluginHost 和 IToolProvider 是相同的
    // 因为它们都在全局命名空间中定义，所以可以直接传递
    // 注意：这里假设旧接口的 IToolPluginHost 和 IToolProvider 与 SDK 中的定义兼容
    return m_legacyPlugin->createProvider(host, parent);
}

bool LegacyPluginAdapter::configureProvider(TmAgent::IToolProvider* provider,
                                           const QJsonObject& config,
                                           QString* error)
{
    if (!m_legacyPlugin) {
        if (error)
            *error = QStringLiteral("Legacy plugin pointer is null");
        return false;
    }
    
    return m_legacyPlugin->configureProvider(provider, config, error);
}

TmAgent::ToolPluginHealth LegacyPluginAdapter::health(const TmAgent::IToolProvider* provider) const
{
    if (!m_legacyPlugin) {
        qCritical() << "[LegacyPluginAdapter] Legacy plugin pointer is null";
        return TmAgent::ToolPluginHealth("unhealthy", 
                                        QStringList() << "Legacy plugin pointer is null");
    }
    
    const ::ToolPluginHealth oldHealth = m_legacyPlugin->health(provider);
    return convertHealth(oldHealth);
}

TmAgent::ToolPluginDescriptor LegacyPluginAdapter::convertDescriptor(
    const ::ToolPluginDescriptor& oldDesc)
{
    TmAgent::ToolPluginDescriptor newDesc;
    
    // 复制所有基本字段
    newDesc.pluginId = oldDesc.pluginId;
    newDesc.displayName = oldDesc.displayName;
    newDesc.version = oldDesc.version;
    newDesc.description = oldDesc.description;
    newDesc.category = oldDesc.category;
    newDesc.toolNames = oldDesc.toolNames;
    newDesc.configSchema = oldDesc.configSchema;
    
    // 标记为旧版本插件（sdkVersionMajor=0 表示使用旧接口）
    newDesc.sdkVersionMajor = 0;
    newDesc.sdkVersionMinor = 0;
    
    return newDesc;
}

TmAgent::ToolPluginHealth LegacyPluginAdapter::convertHealth(
    const ::ToolPluginHealth& oldHealth)
{
    TmAgent::ToolPluginHealth newHealth;
    
    // 转换状态字符串
    // 旧格式使用 "ok", "error", "disabled", "unknown"
    // 新格式使用 "healthy", "degraded", "unhealthy"
    if (oldHealth.state == QLatin1String("ok")) {
        newHealth.status = QStringLiteral("healthy");
    } else if (oldHealth.state == QLatin1String("error")) {
        newHealth.status = QStringLiteral("unhealthy");
    } else if (oldHealth.state == QLatin1String("disabled")) {
        newHealth.status = QStringLiteral("unhealthy");
        newHealth.diagnostics << QStringLiteral("Plugin is disabled");
    } else {
        newHealth.status = QStringLiteral("unhealthy");
        newHealth.diagnostics << QStringLiteral("Unknown health state: ") + oldHealth.state;
    }
    
    // 添加消息到诊断信息
    if (!oldHealth.message.isEmpty()) {
        newHealth.diagnostics << oldHealth.message;
    }
    
    // 添加工具数量信息
    if (oldHealth.toolCount > 0) {
        newHealth.diagnostics << QStringLiteral("Tool count: %1").arg(oldHealth.toolCount);
    }
    
    return newHealth;
}
