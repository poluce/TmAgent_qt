#ifndef LEGACYPLUGINADAPTER_H
#define LEGACYPLUGINADAPTER_H

#include <tmagent/plugin/IToolPlugin.h>
#include "IToolPlugin.h"
#include <QObject>

/**
 * @brief LegacyPluginAdapter - 旧插件接口适配器
 * 
 * 此适配器将旧版本的 IToolPlugin 接口桥接到新的 TmAgent::IToolPlugin SDK 接口。
 * 这允许主应用在迁移期间同时支持旧接口和新 SDK 接口的插件。
 * 
 * 适配器的工作原理：
 * 1. 包装旧插件实例
 * 2. 将旧格式的元数据转换为新格式（标记 sdkVersionMajor=0 表示旧版本）
 * 3. 桥接所有接口方法调用
 * 
 * 使用场景：
 * - 在 PluginManager 中，当检测到插件使用旧接口时自动创建适配器
 * - 确保旧插件在新架构下继续正常工作
 * 
 * 需求: 23.1-23.7
 */
class LegacyPluginAdapter : public QObject, public TmAgent::IToolPlugin {
    Q_OBJECT
    Q_INTERFACES(TmAgent::IToolPlugin)
    
public:
    /**
     * @brief 构造函数
     * @param legacyPlugin 旧插件实例指针（不获取所有权）
     * @param parent 父对象
     */
    explicit LegacyPluginAdapter(::IToolPlugin* legacyPlugin, QObject* parent = nullptr);
    
    /**
     * @brief 返回插件元数据（转换为新格式）
     * 
     * 将旧格式的 ToolPluginDescriptor 转换为新格式：
     * - 复制所有基本字段
     * - 设置 sdkVersionMajor=0, sdkVersionMinor=0 标记为旧版本
     * 
     * @return TmAgent::ToolPluginDescriptor 新格式的插件描述符
     */
    TmAgent::ToolPluginDescriptor descriptor() const override;
    
    /**
     * @brief 创建工具提供者实例
     * 
     * 直接调用旧插件的 createProvider 方法。
     * 
     * @param host 宿主回调接口指针
     * @param parent 父对象指针
     * @return IToolProvider* 工具提供者实例
     */
    TmAgent::IToolProvider* createProvider(TmAgent::IToolPluginHost* host, 
                                          QObject* parent) override;
    
    /**
     * @brief 配置工具提供者
     * 
     * 直接调用旧插件的 configureProvider 方法。
     * 
     * @param provider 要配置的提供者实例
     * @param config 配置数据
     * @param error 错误信息输出
     * @return bool 配置是否成功
     */
    bool configureProvider(TmAgent::IToolProvider* provider,
                          const QJsonObject& config,
                          QString* error = nullptr) override;
    
    /**
     * @brief 健康检查
     * 
     * 调用旧插件的 health 方法并转换结果格式。
     * 
     * @param provider 要检查的提供者实例
     * @return TmAgent::ToolPluginHealth 新格式的健康状态
     */
    TmAgent::ToolPluginHealth health(const TmAgent::IToolProvider* provider) const override;
    
    /**
     * @brief 获取被包装的旧插件实例
     * @return ::IToolPlugin* 旧插件指针
     */
    ::IToolPlugin* legacyPlugin() const { return m_legacyPlugin; }
    
private:
    /**
     * @brief 转换旧格式元数据到新格式
     * @param oldDesc 旧格式描述符
     * @return TmAgent::ToolPluginDescriptor 新格式描述符
     */
    static TmAgent::ToolPluginDescriptor convertDescriptor(const ::ToolPluginDescriptor& oldDesc);
    
    /**
     * @brief 转换旧格式健康状态到新格式
     * @param oldHealth 旧格式健康状态
     * @return TmAgent::ToolPluginHealth 新格式健康状态
     */
    static TmAgent::ToolPluginHealth convertHealth(const ::ToolPluginHealth& oldHealth);
    
    ::IToolPlugin* m_legacyPlugin;  // 被包装的旧插件实例（不拥有所有权）
};

#endif // LEGACYPLUGINADAPTER_H
