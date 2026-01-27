#include "ModelRouter.h"

ModelRouter::ModelRouter(QObject* parent) : QObject(parent) {}

ModelRouter::~ModelRouter() = default;

RouterResult ModelRouter::selectModel(const RouterRequest& request) const {
    RouterResult out;
    out.success = false;

    if (m_descriptors.isEmpty()) {
        out.decisionReason = QStringLiteral("no_models_registered");
        return out;
    }

    // 筛选满足必需能力的模型
    QList<CapabilityDescriptor> candidates;
    for (const auto& d : m_descriptors) {
        bool ok = true;
        for (const QString& cap : request.requiredCapabilities) {
            if (!d.supports(cap)) {
                ok = false;
                break;
            }
        }
        if (ok)
            candidates.append(d);
    }

    if (candidates.isEmpty()) {
        out.decisionReason = QStringLiteral("no_model_matches_capabilities");
        return out;
    }

    // 若指定了偏好且满足能力，优先使用
    if (!request.preferredModelId.isEmpty()) {
        for (const auto& d : candidates) {
            if (d.modelId == request.preferredModelId) {
                out.modelId = d.modelId;
                out.decisionReason = QStringLiteral("preferred");
                out.success = true;
                for (const auto& c : candidates) {
                    if (c.modelId != out.modelId)
                        out.fallbackChain.append(c.modelId);
                }
                return out;
            }
        }
    }

    // 否则使用默认模型（isDefault），若无则取第一个
    for (const auto& d : candidates) {
        if (d.isDefault) {
            out.modelId = d.modelId;
            out.decisionReason = QStringLiteral("default");
            out.success = true;
            for (const auto& c : candidates) {
                if (c.modelId != out.modelId)
                    out.fallbackChain.append(c.modelId);
            }
            return out;
        }
    }

    out.modelId = candidates.first().modelId;
    out.decisionReason = QStringLiteral("first_available");
    out.success = true;
    for (int i = 1; i < candidates.size(); ++i)
        out.fallbackChain.append(candidates.at(i).modelId);
    return out;
}

void ModelRouter::registerModel(const CapabilityDescriptor& descriptor) {
    for (int i = 0; i < m_descriptors.size(); ++i) {
        if (m_descriptors[i].modelId == descriptor.modelId) {
            m_descriptors[i] = descriptor;
            return;
        }
    }
    m_descriptors.append(descriptor);
}

void ModelRouter::unregisterModel(const QString& modelId) {
    for (int i = m_descriptors.size() - 1; i >= 0; --i) {
        if (m_descriptors[i].modelId == modelId) {
            m_descriptors.removeAt(i);
            return;
        }
    }
}

QStringList ModelRouter::registeredModelIds() const {
    QStringList ids;
    for (const auto& d : m_descriptors)
        ids.append(d.modelId);
    return ids;
}
