#ifndef MEMBERCARD_H
#define MEMBERCARD_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QJsonObject>

/**
 * @brief 会员卡数据模型类
 * 
 * 管理会员卡的基本信息，包括：
 * - 卡号、手机号、凤凰卡标识
 * - 发卡时间、有效期
 * - 罚款记录、状态管理
 */
class MemberCard : public QObject
{
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
        StatusActive = 0,      // 激活状态
        StatusFrozen,          // 冻结状态（有罚款未处理）
        StatusExpired,         // 已过期
        StatusLost,            // 挂失状态
        StatusCancelled        // 已注销
    };
    Q_ENUM(CardStatus)

    explicit MemberCard(QObject *parent = nullptr);
    MemberCard(const QString &cardNumber, const QString &phoneNumber, QObject *parent = nullptr);
    
    // 序列化/反序列化
    QJsonObject toJson() const;
    static MemberCard* fromJson(const QJsonObject &json, QObject *parent = nullptr);
    
    // 核心业务方法
    bool isValid() const;
    bool isExpired() const;
    bool hasUnpaidPenalty() const;
    void addPenalty(double amount, const QString &reason);
    void payPenalty(double amount);
    
    // Getter/Setter
    QString cardNumber() const;
    void setCardNumber(const QString &cardNumber);
    
    QString phoneNumber() const;
    void setPhoneNumber(const QString &phoneNumber);
    
    bool isPhoenixCard() const;
    void setIsPhoenixCard(bool isPhoenixCard);
    
    QDateTime issueTime() const;
    void setIssueTime(const QDateTime &issueTime);
    
    QDateTime expiryTime() const;
    void setExpiryTime(const QDateTime &expiryTime);
    
    double totalPenalty() const;
    void setTotalPenalty(double totalPenalty);
    
    CardStatus status() const;
    void setStatus(CardStatus status);
    
    // 罚款记录相关
    QList<QJsonObject> penaltyRecords() const;
    void addPenaltyRecord(const QJsonObject &record);
    void clearPenaltyRecords();

signals:
    void cardNumberChanged();
    void phoneNumberChanged();
    void isPhoenixCardChanged();
    void issueTimeChanged();
    void expiryTimeChanged();
    void totalPenaltyChanged();
    void statusChanged();
    void penaltyAdded(double amount, const QString &reason);
    void penaltyPaid(double amount);
    void cardFrozen();
    void cardActivated();

private:
    QString m_cardNumber;          // 卡号
    QString m_phoneNumber;         // 手机号
    bool m_isPhoenixCard;          // 是否为凤凰卡
    QDateTime m_issueTime;         // 发卡时间
    QDateTime m_expiryTime;        // 过期时间
    double m_totalPenalty;         // 总罚款金额
    CardStatus m_status;           // 卡状态
    QList<QJsonObject> m_penaltyRecords; // 罚款记录列表
};

#endif // MEMBERCARD_H