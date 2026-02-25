#include "Identity.h"
#include "IdentityProfile.h"

Identity::Identity(IdentityType type, const QString& name, QObject* parent, const QString& fixedId)
    : QObject(parent)
    , m_id(fixedId.trimmed().isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : fixedId.trimmed())
    , m_name(name)
    , m_type(type)
{
}

Identity* Identity::createUser(const QString& name, QObject* parent, const QString& fixedId)
{
    return new Identity(IdentityType::User, name, parent, fixedId);
}

Identity* Identity::createAgent(const QString& name, IdentityProfile* profile, QObject* parent, const QString& fixedId)
{
    auto* identity = new Identity(IdentityType::Agent, name, parent, fixedId);
    if (profile) {
        profile->setParent(identity);
        identity->m_profile = profile;
    }
    return identity;
}

QString Identity::id() const { return m_id; }

QString Identity::name() const { return m_name; }

void Identity::setName(const QString& name)
{
    if (m_name != name) {
        m_name = name;
        emit nameChanged(name);
    }
}

Identity::IdentityType Identity::type() const { return m_type; }

bool Identity::isAgent() const { return m_type == IdentityType::Agent; }

bool Identity::isUser() const { return m_type == IdentityType::User; }

IdentityProfile* Identity::profile() const { return m_profile; }

void Identity::setProfile(IdentityProfile* profile)
{
    if (m_profile == profile)
        return;
    if (m_profile)
        m_profile->deleteLater();
    m_profile = profile;
    if (m_profile)
        m_profile->setParent(this);
    emit profileChanged();
}

QString Identity::avatar() const { return m_avatar; }

void Identity::setAvatar(const QString& avatar) { m_avatar = avatar; }
