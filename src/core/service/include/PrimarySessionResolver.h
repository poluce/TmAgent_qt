#ifndef PRIMARYSESSIONRESOLVER_H
#define PRIMARYSESSIONRESOLVER_H

#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/Session.h"
#include <functional>

class PrimarySessionResolver {
public:
    struct Dependencies {
        IdentityManager* identityManager = nullptr;
        SessionManager* sessionManager = nullptr;
        std::function<QString()> userIdentityId;
        std::function<Session*(const QString&, const QString&, const QString&)> createSessionForIdentityAs;
    };

    explicit PrimarySessionResolver(const Dependencies& dependencies)
        : m_dependencies(dependencies)
    {
    }

    QString resolveForAgent(const QString& agentId,
                            bool createIfMissing,
                            bool isolated,
                            const QString& titleSuffix = QString()) const
    {
        if (!m_dependencies.sessionManager || !m_dependencies.identityManager)
            return QString();

        const QString trimmedAgentId = agentId.trimmed();
        if (trimmedAgentId.isEmpty())
            return QString();

        const QList<Session*> sessions = m_dependencies.sessionManager->sessionsForIdentity(trimmedAgentId);
        Session* best = nullptr;
        for (Session* session : sessions) {
            if (!session)
                continue;
            if (!best || session->lastActiveAt() > best->lastActiveAt())
                best = session;
        }
        if (best && !isolated)
            return best->id();

        if (!createIfMissing && !isolated)
            return best ? best->id() : QString();

        Identity* identity = m_dependencies.identityManager->findById(trimmedAgentId);
        if (!identity || !identity->isAgent() || !m_dependencies.userIdentityId || !m_dependencies.createSessionForIdentityAs)
            return QString();

        const QString userId = m_dependencies.userIdentityId().trimmed();
        if (userId.isEmpty())
            return QString();

        QString title = identity->name();
        const QString suffix = titleSuffix.trimmed();
        if (!suffix.isEmpty())
            title = QStringLiteral("%1 [%2]").arg(title, suffix);

        Session* session = m_dependencies.createSessionForIdentityAs(userId, trimmedAgentId, title);
        return session ? session->id() : QString();
    }

private:
    Dependencies m_dependencies;
};

#endif // PRIMARYSESSIONRESOLVER_H
