#ifndef JSONRPCTRANSPORT_H
#define JSONRPCTRANSPORT_H

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QProcess>

/**
 * @brief JSON-RPC 2.0 传输层
 *
 * 负责 LSP 消息的编解码和进程通信。
 * LSP 使用 HTTP 风格的消息头：Content-Length: xxx\r\n\r\n{json}
 *
 * 仿照 Qt 6 的 QLanguageServerJsonRpcTransport 设计
 */
class JsonRpcTransport : public QObject {
    Q_OBJECT

public:
    explicit JsonRpcTransport(QProcess* process, QObject* parent = nullptr);

    /**
     * @brief 发送 JSON-RPC 消息
     * @param message JSON 对象，会自动添加 "jsonrpc": "2.0"
     */
    void sendMessage(const QJsonObject& message);

    /**
     * @brief 发送请求（带 ID）
     * @param method 方法名，如 "textDocument/definition"
     * @param params 参数对象
     * @param id 请求 ID
     */
    void sendRequest(const QString& method, const QJsonObject& params, int id);

    /**
     * @brief 发送通知（无 ID，无响应）
     * @param method 方法名，如 "textDocument/didOpen"
     * @param params 参数对象
     */
    void sendNotification(const QString& method, const QJsonObject& params);

signals:
    /**
     * @brief 收到消息时发出
     * @param message 解析后的 JSON 对象
     */
    void messageReceived(const QJsonObject& message);

    /**
     * @brief 发生错误时发出
     * @param error 错误描述
     */
    void errorOccurred(const QString& error);

private slots:
    void onReadyRead();
    void onProcessError(QProcess::ProcessError error);

private:
    /**
     * @brief 尝试从缓冲区解析消息
     * @return 是否成功解析了一条消息
     */
    bool tryParseMessage();

    /**
     * @brief 编码消息为 LSP 格式
     * @param json JSON 数据
     * @return 带消息头的完整数据
     */
    QByteArray encodeMessage(const QByteArray& json);

    QProcess* m_process;
    QByteArray m_buffer;
    int m_expectedLength = -1; // 期望的消息体长度
};

#endif // JSONRPCTRANSPORT_H
