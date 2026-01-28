#include "ModelFactory.h"

ModelFactory::ModelFactory(QObject* parent) : QObject(parent) {}

ModelFactory::~ModelFactory() {
    m_providers.clear();
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
}

void ModelFactory::unregisterProvider(const QString& modelId) {
    LLMProvider* p = m_providers.take(modelId);
    if (p) {
        p->setParent(nullptr);
        p->deleteLater();
    }
}

QStringList ModelFactory::registeredModelIds() const {
    return m_providers.keys();
}
