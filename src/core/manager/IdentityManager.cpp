#include "IdentityManager.h"
#include "core/model/IdentityProfile.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

namespace {
QString resolveDefaultUserAvatarPath()
{
    const QString relativeAvatar = QStringLiteral("resources/avatars/default_agents/product_manager.png");
    const QStringList candidates = {
        QDir::current().absoluteFilePath(relativeAvatar),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(relativeAvatar),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("avatars/default_agents/product_manager.png"))
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path))
            return QFileInfo(path).absoluteFilePath();
    }
    return QString();
}
} // namespace

IdentityManager* IdentityManager::instance()
{
    static IdentityManager s_instance;
    return &s_instance;
}

IdentityManager::IdentityManager(QObject* parent)
    : QObject(parent)
{
}

Identity* IdentityManager::userIdentity(const QString& preferredId)
{
    const QString desiredId = preferredId.trimmed();
    if (m_userIdentity && !desiredId.isEmpty() && m_userIdentity->id() != desiredId) {
        m_identities.remove(m_userIdentity->id());
        m_userIdentity->deleteLater();
        m_userIdentity = nullptr;
    }

    if (!m_userIdentity) {
        m_userIdentity = Identity::createUser(QStringLiteral("Me"), this, desiredId);
        m_identities.insert(m_userIdentity->id(), m_userIdentity);
    }
    if (m_userIdentity->avatar().trimmed().isEmpty()) {
        const QString avatarPath = resolveDefaultUserAvatarPath();
        if (!avatarPath.isEmpty())
            m_userIdentity->setAvatar(avatarPath);
    }
    return m_userIdentity;
}

Identity* IdentityManager::createAgent(const QString& name, IdentityProfile* profile, const QString& preferredId)
{
    const QString desiredId = preferredId.trimmed();
    if (!desiredId.isEmpty() && m_identities.contains(desiredId))
        qWarning() << "[IdentityManager] duplicate preferredId, fallback to generated id:" << desiredId;
    auto* agent = Identity::createAgent(
        name,
        profile,
        this,
        (!desiredId.isEmpty() && !m_identities.contains(desiredId)) ? desiredId : QString());
    m_identities.insert(agent->id(), agent);
    emit agentCreated(agent);
    return agent;
}

Identity* IdentityManager::findById(const QString& id) const
{
    return m_identities.value(id, nullptr);
}

Identity* IdentityManager::findByName(const QString& name) const
{
    for (Identity* identity : m_identities) {
        if (identity->name() == name)
            return identity;
    }
    return nullptr;
}

QList<Identity*> IdentityManager::allAgents() const
{
    QList<Identity*> agents;
    for (Identity* identity : m_identities) {
        if (identity->isAgent())
            agents.append(identity);
    }
    return agents;
}

QList<Identity*> IdentityManager::allIdentities() const
{
    return m_identities.values();
}

bool IdentityManager::removeAgent(const QString& id)
{
    Identity* identity = m_identities.value(id, nullptr);
    if (!identity || identity->isUser())
        return false;
    m_identities.remove(id);
    emit agentRemoved(id);
    identity->deleteLater();
    return true;
}
