#include "IdentityManager.h"
#include "core/model/IdentityProfile.h"
#include <QCoreApplication>
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

Identity* IdentityManager::userIdentity()
{
    if (!m_userIdentity) {
        m_userIdentity = Identity::createUser(QStringLiteral("Me"), this);
        m_identities.insert(m_userIdentity->id(), m_userIdentity);
    }
    if (m_userIdentity->avatar().trimmed().isEmpty()) {
        const QString avatarPath = resolveDefaultUserAvatarPath();
        if (!avatarPath.isEmpty())
            m_userIdentity->setAvatar(avatarPath);
    }
    return m_userIdentity;
}

Identity* IdentityManager::createAgent(const QString& name, IdentityProfile* profile)
{
    auto* agent = Identity::createAgent(name, profile, this);
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
