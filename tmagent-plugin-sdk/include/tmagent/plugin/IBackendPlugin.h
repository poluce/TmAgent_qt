#ifndef TMAGENT_IBACKENDPLUGIN_H
#define TMAGENT_IBACKENDPLUGIN_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include "../types/BackendTypes.h"

namespace TmAgent {

class IDelegateBackend;
class ITeammateBackend;

/**
 * @brief 后端插件接口
 * 
 * 后端插件提供 AI 模型接口实现，支持两种模式：
 * - 委托后端（Delegate Backend）：用于子任务委托
 * - 队友后端（Teammate Backend）：用于持久化协作
 * 
 * 插件必须至少支持一种模式。
 */
class IBackendPlugin {
public:
    virtual ~IBackendPlugin() = default;
    
    /**
     * @brief 返回后端元数据
     * @return BackendDescriptor 包含后端 ID、显示名称、版本等信息
     */
    virtual BackendDescriptor descriptor() const = 0;
    
    /**
     * @brief 创建委托后端实例
     * @param parent 父对象，用于 Qt 内存管理
     * @return IDelegateBackend* 委托后端实例，如果不支持则返回 nullptr
     */
    virtual IDelegateBackend* createDelegateBackend(QObject* parent = nullptr) = 0;
    
    /**
     * @brief 创建队友后端实例
     * @param parent 父对象，用于 Qt 内存管理
     * @return ITeammateBackend* 队友后端实例，如果不支持则返回 nullptr
     */
    virtual ITeammateBackend* createTeammateBackend(QObject* parent = nullptr) = 0;
};

} // namespace TmAgent

#define TMAGENT_BACKEND_PLUGIN_IID "org.tmagent.BackendPlugin/1.0"
Q_DECLARE_INTERFACE(TmAgent::IBackendPlugin, TMAGENT_BACKEND_PLUGIN_IID)

#endif // TMAGENT_IBACKENDPLUGIN_H
