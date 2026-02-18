#include "ToolRegistry.h"
#include <QDebug>

ToolRegistry* ToolRegistry::instance()
{
    static ToolRegistry inst;
    return &inst;
}

void ToolRegistry::registerFactory(const QString& name, IToolFactory* factory)
{
    if (m_factories.contains(name)) {
        qWarning() << "[ToolRegistry] 警告: 重复注册工具工厂:" << name;
        return;
    }
    m_factories[name] = factory;
    qDebug() << "[ToolRegistry] 注册工厂成功:" << name;
}

QList<ITool*> ToolRegistry::createAllTools()
{
    QList<ITool*> tools;
    for (auto it = m_factories.begin(); it != m_factories.end(); ++it) {
        tools.append(it.value()->create());
    }
    qDebug() << "[ToolRegistry] 创建了" << tools.size() << "个工具实例";
    return tools;
}

QStringList ToolRegistry::registeredTools() const
{
    return m_factories.keys();
}
