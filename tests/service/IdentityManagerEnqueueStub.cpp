#include "core/manager/IdentityManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"

namespace {

IdentityManager* g_identityManager = nullptr;
Identity* g_userIdentity = nullptr;
QHash<QString, Identity*> g_identityById;

} // namespace

IdentityManager* IdentityManager::instance()
{
    if (!g_identityManager)
        g_identityManager = reinterpret_cast<IdentityManager*>(0x1);
    return g_identityManager;
}

Identity* IdentityManager::userIdentity(const QString& preferredId)
{
    if (!g_userIdentity) {
        const QString fixedId = preferredId.trimmed().isEmpty()
            ? QStringLiteral("user-1")
            : preferredId.trimmed();
        g_userIdentity = Identity::createUser(QStringLiteral("Me"), nullptr, fixedId);
        g_identityById.insert(g_userIdentity->id(), g_userIdentity);
    }
    return g_userIdentity;
}

Identity* IdentityManager::createAgent(const QString& name, IdentityProfile* profile, const QString& preferredId)
{
    const QString fixedId = preferredId.trimmed().isEmpty()
        ? QStringLiteral("agent-stub")
        : preferredId.trimmed();
    Identity* agent = Identity::createAgent(name, profile, nullptr, fixedId);
    g_identityById.insert(agent->id(), agent);
    return agent;
}

Identity* IdentityManager::findById(const QString& id) const
{
    return g_identityById.value(id.trimmed(), nullptr);
}
