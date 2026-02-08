#ifndef IDENTITY_H
#define IDENTITY_H

#include <QObject>
#include <QString>
#include <QUuid>

class IdentityProfile;

/**
 * @brief 身份抽象——系统中的第一公民
 *
 * 用户和 Agent 在数据模型上统一为 Identity。
 * 继承 QObject 以利用 parent-child 自动销毁和信号槽。
 */
class Identity : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(IdentityType type READ type CONSTANT)

public:
    enum class IdentityType { User, Agent };
    Q_ENUM(IdentityType)

    // ---- 工厂方法 ----
    static Identity* createUser(const QString& name, QObject* parent = nullptr);
    static Identity* createAgent(const QString& name,
                                 IdentityProfile* profile,
                                 QObject* parent = nullptr);

    // ---- 基本属性 ----
    QString id() const;
    QString name() const;
    void setName(const QString& name);
    IdentityType type() const;
    bool isAgent() const;
    bool isUser() const;

    // ---- Agent 专属 ----
    IdentityProfile* profile() const;
    void setProfile(IdentityProfile* profile);

    QString avatar() const;
    void setAvatar(const QString& avatar);

signals:
    void nameChanged(const QString& name);
    void profileChanged();

private:
    explicit Identity(IdentityType type, const QString& name,
                      QObject* parent = nullptr);

    QString m_id;
    QString m_name;
    IdentityType m_type;
    QString m_avatar;
    IdentityProfile* m_profile = nullptr; // parent = this（Agent 专属）
};

#endif // IDENTITY_H
