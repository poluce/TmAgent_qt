#ifndef TMAGENT_ITOOLPLUGIN_H
#define TMAGENT_ITOOLPLUGIN_H

#include <tmagent/types/PluginTypes.h>
#include <QObject>

namespace TmAgent {

// Forward declarations
class IToolProvider;
class IToolPluginHost;

/**
 * @brief 工具插件接口
 * 
 * 所有工具插件必须实现此接口。工具插件提供一组可被 AI Agent 调用的工具。
 * 
 * 使用示例：
 * @code
 * class MyToolPlugin : public QObject, public TmAgent::IToolPlugin {
 *     Q_OBJECT
 *     Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "my_tool.json")
 *     Q_INTERFACES(TmAgent::IToolPlugin)
 * public:
 *     ToolPluginDescriptor descriptor() const override;
 *     IToolProvider* createProvider(IToolPluginHost* host, QObject* parent) override;
 * };
 * @endcode
 */
class IToolPlugin {
public:
    virtual ~IToolPlugin() = default;
    
    /**
     * @brief 返回插件元数据
     * 
     * 此方法在插件加载时被调用，用于获取插件的基本信息。
     * 主应用会使用这些信息进行版本兼容性检查和 UI 展示。
     * 
     * @return ToolPluginDescriptor 插件描述符，包含 ID、版本、工具列表等信息
     * 
     * @note 此方法应该是轻量级的，不应执行耗时操作
     * @note 返回的描述符必须通过 isValid() 验证
     */
    virtual ToolPluginDescriptor descriptor() const = 0;
    
    /**
     * @brief 创建工具提供者实例
     * 
     * 此方法在插件加载成功后被调用，用于创建实际执行工具逻辑的提供者对象。
     * 提供者对象的生命周期由主应用管理（通过 Qt 父子对象机制）。
     * 
     * @param host 宿主回调接口指针，提供者可以通过此接口访问主应用服务
     * @param parent 父对象指针，用于 Qt 对象树管理（当 parent 销毁时自动释放提供者）
     * @return IToolProvider* 工具提供者实例指针，不能为 nullptr
     * 
     * @note 返回的对象必须继承 QObject 并设置 parent
     * @note 主应用负责管理返回对象的生命周期
     */
    virtual IToolProvider* createProvider(IToolPluginHost* host, 
                                         QObject* parent) = 0;
    
    /**
     * @brief 配置工具提供者（可选）
     * 
     * 此方法在创建提供者后被调用，用于应用用户配置。
     * 如果插件不需要配置，可以使用默认实现（返回 true）。
     * 
     * @param provider 要配置的提供者实例
     * @param config 配置数据（JSON 对象格式）
     * @param error 如果配置失败，设置错误信息（可选）
     * @return bool 配置是否成功
     * 
     * @note 配置 Schema 应在 descriptor().configSchema 中声明
     * @note 如果返回 false，主应用可能拒绝加载插件或使用默认配置
     */
    virtual bool configureProvider(IToolProvider* provider,
                                  const QJsonObject& config,
                                  QString* error = nullptr) {
        Q_UNUSED(provider);
        Q_UNUSED(config);
        Q_UNUSED(error);
        return true;
    }
    
    /**
     * @brief 健康检查（可选）
     * 
     * 此方法用于检查提供者的运行状态。主应用可以定期调用此方法
     * 来监控插件健康状况。
     * 
     * @param provider 要检查的提供者实例
     * @return ToolPluginHealth 健康状态信息
     * 
     * @note 默认实现返回 "healthy" 状态
     * @note 此方法应该是轻量级的，不应执行耗时操作
     */
    virtual ToolPluginHealth health(const IToolProvider* provider) const {
        Q_UNUSED(provider);
        ToolPluginHealth h;
        h.status = "healthy";
        return h;
    }
};

} // namespace TmAgent

// Qt 插件系统 IID 定义
#define TMAGENT_TOOL_PLUGIN_IID "org.tmagent.ToolPlugin/1.0"
Q_DECLARE_INTERFACE(TmAgent::IToolPlugin, TMAGENT_TOOL_PLUGIN_IID)

#endif // TMAGENT_ITOOLPLUGIN_H
