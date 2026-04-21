#ifndef MODELFACTORYADAPTER_H
#define MODELFACTORYADAPTER_H

#include "IModelFactory.h"
#include <QObject>

class ModelFactory;

/**
 * @brief ModelFactoryAdapter - 桥接 SDK IModelFactory 接口到 ModelFactory
 * 
 * 此适配器类实现 SDK 的 IModelFactory 接口，将模型创建请求转发到主应用的 ModelFactory。
 * 用于后端插件创建 LLM Provider，而无需直接依赖 ModelFactory。
 */
class ModelFactoryAdapter : public QObject, public TmAgent::IModelFactory {
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param factory ModelFactory 实例指针
     * @param parent 父对象（用于 Qt 内存管理）
     */
    explicit ModelFactoryAdapter(ModelFactory* factory, QObject* parent = nullptr);
    ~ModelFactoryAdapter() override;
    
    // IModelFactory 接口实现
    QObject* createProvider(const TmAgent::AgentConfig& config,
                           QString* error = nullptr) override;
    QStringList getModelCapabilities(const QString& modelId) const override;

private:
    ModelFactory* m_factory;
};

#endif // MODELFACTORYADAPTER_H
