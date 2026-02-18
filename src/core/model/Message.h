#ifndef MESSAGE_H
#define MESSAGE_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QtGlobal>

/**
 * @brief 消息内容（值对象）
 */
struct MessageContent {
    enum class Type { Text,
                      ToolCall,
                      ToolResult,
                      System,
                      File };

    Type type = Type::Text;
    QString text;
    QJsonObject payload; // 结构化数据（工具调用参数、文件信息等）
};

/**
 * @brief 消息值对象（POD struct，不继承 QObject）
 *
 * 消息数量可能数千条，每条都创建 QObject 开销过大，
 * 因此 Message 设计为轻量值对象。
 */
struct Message {
    enum class Status {
        Pending,
        Streaming,
        Completed,
        Cancelled,
        Interrupted,
        Error
    };

    QString id;
    QString sessionId;
    QString traceId;
    QString turnId;
    qint64 seq = 0;
    QString senderId;
    QStringList mentions; // @的目标 Identity ID 列表
    MessageContent content;
    QDateTime timestamp;
    Status status = Status::Completed;

    bool isValid() const { return !id.isEmpty() && !sessionId.isEmpty(); }

    // ---- 工厂方法 ----

    static Message createText(const QString& sessionId, const QString& senderId, const QString& text)
    {
        Message msg = make(sessionId, senderId, MessageContent::Type::Text);
        msg.content.text = text;
        return msg;
    }

    static Message createToolCall(const QString& sessionId, const QString& senderId, const QString& toolName, const QJsonObject& args)
    {
        Message msg = make(sessionId, senderId, MessageContent::Type::ToolCall);
        msg.content.text = toolName;
        msg.content.payload = args;
        return msg;
    }

    static Message createToolResult(const QString& sessionId, const QString& senderId, const QString& toolCallId, const QString& result)
    {
        Message msg = make(sessionId, senderId, MessageContent::Type::ToolResult);
        msg.content.text = result;
        QJsonObject payload;
        payload.insert(QStringLiteral("tool_call_id"), toolCallId);
        msg.content.payload = payload;
        return msg;
    }

    static Message createSystem(const QString& sessionId, const QString& text)
    {
        Message msg = make(sessionId, QString(), MessageContent::Type::System);
        msg.content.text = text;
        return msg;
    }

private:
    static Message make(const QString& sessionId, const QString& senderId, MessageContent::Type type)
    {
        Message msg;
        msg.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        msg.sessionId = sessionId;
        msg.senderId = senderId;
        msg.content.type = type;
        msg.timestamp = QDateTime::currentDateTime();
        msg.status = Status::Completed;
        return msg;
    }
};

#endif // MESSAGE_H
