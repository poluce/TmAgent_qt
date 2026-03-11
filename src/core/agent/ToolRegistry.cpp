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
}

QList<ITool*> ToolRegistry::createAllTools()
{
    QList<ITool*> tools;
    for (auto it = m_factories.begin(); it != m_factories.end(); ++it) {
        tools.append(it.value()->create());
    }
    return tools;
}

QStringList ToolRegistry::registeredTools() const
{
    return m_factories.keys();
}
