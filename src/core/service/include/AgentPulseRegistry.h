#ifndef AGENTPULSEREGISTRY_H
#define AGENTPULSEREGISTRY_H

#include "AgentPulse.h"
#include <QHash>
#include <QObject>
#include <QString>
#include <functional>

class AgentPulseRegistry {
public:
    struct Dependencies {
        QObject* owner = nullptr;
        QHash<QString, AgentPulse*>* pulses = nullptr;
        std::function<QString(AgentPulse::State)> stateText;
        std::function<void(const QString&, const QString&)> emitStateChanged;
        std::function<void(const QString&)> emitHardTimeout;
    };

    explicit AgentPulseRegistry(const Dependencies& dependencies)
        : m_dependencies(dependencies)
    {
    }

    AgentPulse* find(const QString& agentId) const
    {
        if (!m_dependencies.pulses)
            return nullptr;
        return m_dependencies.pulses->value(agentId.trimmed(), nullptr);
    }

    AgentPulse* ensure(const QString& agentId)
    {
        if (!m_dependencies.pulses)
            return nullptr;

        const QString key = agentId.trimmed();
        if (key.isEmpty())
            return nullptr;

        if (m_dependencies.pulses->contains(key))
            return m_dependencies.pulses->value(key, nullptr);

        QObject* owner = m_dependencies.owner;
        auto* pulse = new AgentPulse(key, owner);
        pulse->start(1000);

        const auto emitStateChanged = m_dependencies.emitStateChanged;
        const auto emitHardTimeout = m_dependencies.emitHardTimeout;
        const auto stateText = m_dependencies.stateText;
        QObject::connect(
            pulse,
            &AgentPulse::stateChanged,
            owner ? owner : pulse,
            [emitStateChanged, stateText](const QString& changedAgentId, AgentPulse::State state) {
                if (emitStateChanged)
                    emitStateChanged(changedAgentId, stateText ? stateText(state) : QString());
            });
        QObject::connect(
            pulse,
            &AgentPulse::hardTimeoutReached,
            owner ? owner : pulse,
            [emitHardTimeout](const QString& changedAgentId) {
                if (emitHardTimeout)
                    emitHardTimeout(changedAgentId);
            });

        m_dependencies.pulses->insert(key, pulse);
        return pulse;
    }

    void reportProgress(const QString& agentId, const QString& summary = QString())
    {
        AgentPulse* pulse = ensure(agentId);
        if (!pulse)
            return;
        pulse->reportProgress(summary);
    }

private:
    Dependencies m_dependencies;
};

#endif // AGENTPULSEREGISTRY_H

