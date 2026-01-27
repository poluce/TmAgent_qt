#include "ModelFactory.h"

ModelFactory::ModelFactory(QObject* parent) : QObject(parent) {
    m_router = new ModelRouter(this);
}

ModelFactory::~ModelFactory() {
    // m_router 的 parent 是 this，会随 this 析构
    // m_providers 中的 provider 若 parent 为本 factory，也会自动析构
    m_providers.clear();
}

LLMProvider* ModelFactory::getProvider(const QStringList& capabilities,
                                       const RouterRequest& constraints) const {
    RouterRequest req = constraints;
    req.requiredCapabilities = capabilities.isEmpty() ? req.requiredCapabilities : capabilities;
    RouterResult res = m_router->selectModel(req);
    if (!res.success || res.modelId.isEmpty())
        return nullptr;
    return m_providers.value(res.modelId, nullptr);
}

LLMProvider* ModelFactory::getProviderByModelId(const QString& modelId) const {
    return m_providers.value(modelId, nullptr);
}

void ModelFactory::registerProvider(const CapabilityDescriptor& descriptor, LLMProvider* provider) {
    if (!provider)
        return;
    const QString id = descriptor.modelId;
    if (id.isEmpty())
        return;

    LLMProvider* old = m_providers.value(id, nullptr);
    if (old && old != provider) {
        old->setParent(nullptr);
        old->deleteLater();
    }

    provider->setParent(this);
    m_providers.insert(id, provider);
    m_router->registerModel(descriptor);
}

void ModelFactory::unregisterProvider(const QString& modelId) {
    LLMProvider* p = m_providers.take(modelId);
    if (p) {
        p->setParent(nullptr);
        p->deleteLater();
    }
    m_router->unregisterModel(modelId);
}

QStringList ModelFactory::registeredModelIds() const {
    return m_router->registeredModelIds();
}
