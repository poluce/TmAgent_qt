#ifndef MEMBERCARD_H
#define MEMBERCARD_H

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>

/**
 * @brief 会员卡数据模型类
 *
 * 管理会员卡的基本信息，包括：
 * - 卡号、手机号、凤凰卡标识
 * - 发卡时间、有效期
 * - 罚款记录、状态管理
 */
class MemberCard : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString cardNumber READ cardNumber WRITE setCardNumber NOTIFY cardNumberChanged)
    Q_PROPERTY(QString phoneNumber READ phoneNumber WRITE setPhoneNumber NOTIFY phoneNumberChanged)
    Q_PROPERTY(bool isPhoenixCard READ isPhoenixCard WRITE setIsPhoenixCard NOTIFY isPhoenixCardChanged)
    Q_PROPERTY(QDateTime issueTime READ issueTime WRITE setIssueTime NOTIFY issueTimeChanged)
    Q_PROPERTY(QDateTime expiryTime READ expiryTime WRITE setExpiryTime NOTIFY expiryTimeChanged)
    Q_PROPERTY(double totalPenalty READ totalPenalty WRITE setTotalPenalty NOTIFY totalPenaltyChanged)
    Q_PROPERTY(CardStatus status READ status WRITE setStatus NOTIFY statusChanged)

public:
    enum CardStatus {
        StatusActive = 0,
        StatusFrozen,
        StatusExpired,
        StatusLost,
        StatusCancelled
    };
    Q_ENUM(CardStatus)

    explicit MemberCard(QObject* parent = nullptr);
    MemberCard(const QString& cardNumber, const QString& phoneNumber, QObject* parent = nullptr);

    // 序列化/反序列化
    QJsonObject toJson() const;
    static MemberCard* fromJson(const QJsonObject& json, QObject* parent = nullptr);

    // 核心业务方法
    bool isValid() const;
    bool isExpired() const;
    bool hasUnpaidPenalty() const;
    void addPenalty(double amount, const QString& reason);
    void payPenalty(double amount);

    // Getter/Setter
    QString cardNumber() const;
    void setCardNumber(const QString& cardNumber);

    QString phoneNumber() const;
    void setPhoneNumber(const QString& phoneNumber);

    bool isPhoenixCard() const;
    void setIsPhoenixCard(bool isPhoenixCard);

    QDateTime issueTime() const;
    void setIssueTime(const QDateTime& issueTime);

    QDateTime expiryTime() const;
    void setExpiryTime(const QDateTime& expiryTime);

    double totalPenalty() const;
    void setTotalPenalty(double totalPenalty);

    CardStatus status() const;
    void setStatus(CardStatus status);

    // 罚款记录相关
    QList<QJsonObject> penaltyRecords() const;
    void addPenaltyRecord(const QJsonObject& record);
    void clearPenaltyRecords();

signals:
    void cardNumberChanged();
    void phoneNumberChanged();
    void isPhoenixCardChanged();
    void issueTimeChanged();
    void expiryTimeChanged();
    void totalPenaltyChanged();
    void statusChanged();
    void penaltyAdded(double amount, const QString& reason);
    void penaltyPaid(double amount);
    void cardFrozen();
    void cardActivated();

private:
    QString m_cardNumber;
    QString m_phoneNumber;
    bool m_isPhoenixCard;
    QDateTime m_issueTime;
    QDateTime m_expiryTime;
    double m_totalPenalty;
    CardStatus m_status;
    QList<QJsonObject> m_penaltyRecords;
};

#endif // MEMBERCARD_H