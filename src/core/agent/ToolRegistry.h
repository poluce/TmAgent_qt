#ifndef TOOLREGISTRY_H
#define TOOLREGISTRY_H

#include "ToolTypes.h"
#include <QMap>
#include <QString>

/**
 * @brief 工具工厂接口
 */
class IToolFactory {
public:
    virtual ~IToolFactory() = default;
    virtual ITool* create() = 0;
};

/**
 * @brief 工具注册表单例
 *
 * 职责：管理所有可用工具的工厂，支持静态自注册和动态创建。
 */
class ToolRegistry {
public:
    static ToolRegistry* instance();

    /**
     * @brief 注册工具工厂
     * @param name 工具唯一名称
     * @param factory 工厂实例（生命周期由注册表管理）
     */
    void registerFactory(const QString& name, IToolFactory* factory);

    /**
     * @brief 创建所有已注册的工具实例
     * @return 工具实例列表（生命周期归调用者管理，通常是 ToolDispatcher）
     */
    QList<ITool*> createAllTools();

    /**
     * @brief 获取所有已注册工具的名称列表
     */
    QStringList registeredTools() const;

private:
    ToolRegistry() = default;
    QMap<QString, IToolFactory*> m_factories;
};

/**
 * @brief 工具自注册辅助模板类
 */
template <typename T>
class ToolRegisterer : public IToolFactory {
public:
    ToolRegisterer(const QString& name)
    {
        ToolRegistry::instance()->registerFactory(name, this);
    }
    ITool* create() override { return new T(); }
};

/**
 * @brief 实现文件中的注册宏
 * 用法：在 .cpp 文件中调用 REGISTER_TOOL_INSTANCE(CreateFileTool, "create_file")
 */
#define REGISTER_TOOL_INSTANCE(ClassName, ToolName) \
    static ToolRegisterer<ClassName> g_reg_##ClassName(ToolName);

#endif // TOOLREGISTRY_H
