#ifndef SESSION_H
#define SESSION_H

#include "Message.h"
#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUuid>

/**
 * @brief 会话——两个或多个 Identity 之间的对话通道
 *
 * Session 是共享的：用户和 Agent 看到的是同一个 Session 指针。
 * 继承 QObject 以利用 parent-child 自动销毁和信号槽。
 */
class Session : public QObject {
    Q_OBJECT
public:
    enum class SessionType { Private, Group };
    Q_ENUM(SessionType)

    // ---- 工厂方法 ----
    static Session* createPrivate(const QString& participantA,
                                  const QString& participantB,
                                  QObject* parent = nullptr);
    static Session* createGroup(const QString& ownerId,
                                const QStringList& participantIds,
                                const QString& title = QString(),
                                QObject* parent = nullptr);

    // ---- 基本属性 ----
    QString id() const;
    void setId(const QString& id); // 仅用于持久化恢复
    SessionType type() const;
    QString ownerId() const;

    // ---- 参与者 ----
    QStringList participantIds() const;
    bool hasParticipant(const QString& identityId) const;
    void addParticipant(const QString& identityId);
    void removeParticipant(const QString& identityId);

    // ---- 消息管理 ----
    void addMessage(const Message& msg);
    int messageCount() const;
    Message messageAt(int index) const;
    QList<Message> allMessages() const;
    Message lastMessage() const;
    void clearMessages();

    // ---- LLM 对话历史（兼容现有 LLMAgent 接口）----
    QJsonArray llmHistory() const;
    void setLlmHistory(const QJsonArray& history);

    // ---- IO 历史（请求/响应 JSON 记录）----
    QJsonArray ioHistory() const;
    void setIoHistory(const QJsonArray& history);

    // ---- 元信息 ----
    QString title() const;
    void setTitle(const QString& title);
    QDateTime createdAt() const;
    QDateTime lastActiveAt() const;

    // ---- 流式状态 ----
    struct StreamState {
        QString buffer;
        bool hasPendingMessage = false;
        bool lastMsgIsTool = false;
        bool isStreaming = false;
    };
    StreamState& streamState();
    const StreamState& streamState() const;
    bool isStreaming() const;

signals:
    void messageAdded(const Message& msg);
    void participantAdded(const QString& identityId);
    void participantRemoved(const QString& identityId);
    void titleChanged(const QString& title);
    void llmHistoryChanged();
    void ioHistoryChanged();

private:
    explicit Session(SessionType type, QObject* parent = nullptr);

    QString m_id;
    SessionType m_type;
    QString m_ownerId;
    QStringList m_participantIds;
    QList<Message> m_messages;
    QString m_title;
    QDateTime m_createdAt;
    QDateTime m_lastActiveAt;

    // 兼容现有 LLMAgent 的对话历史格式
    QJsonArray m_llmHistory;
    QJsonArray m_ioHistory;

    // 流式输出状态
    StreamState m_streamState;
};

#endif // SESSION_H
