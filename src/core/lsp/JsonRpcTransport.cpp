#include "JsonRpcTransport.h"
#include <QDebug>
#include <QRegularExpression>

JsonRpcTransport::JsonRpcTransport(QProcess *process, QObject *parent)
    : QObject(parent)
    , m_process(process)
{
    connect(m_process, &QProcess::readyReadStandardOutput, 
            this, &JsonRpcTransport::onReadyRead);
    connect(m_process, &QProcess::errorOccurred, 
            this, &JsonRpcTransport::onProcessError);
}

JsonRpcTransport::~JsonRpcTransport()
{
}

void JsonRpcTransport::sendMessage(const QJsonObject &message)
{
    QJsonObject msg = message;
    if (!msg.contains("jsonrpc")) {
        msg["jsonrpc"] = "2.0";
    }
    
    QByteArray json = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    QByteArray data = encodeMessage(json);
    
    qDebug() << "[LSP TX]" << json.left(200);
    m_process->write(data);
}

void JsonRpcTransport::sendRequest(const QString &method, const QJsonObject &params, int id)
{
    QJsonObject message;
    message["jsonrpc"] = "2.0";
    message["id"] = id;
    message["method"] = method;
    if (!params.isEmpty()) {
        message["params"] = params;
    }
    sendMessage(message);
}

void JsonRpcTransport::sendNotification(const QString &method, const QJsonObject &params)
{
    QJsonObject message;
    message["jsonrpc"] = "2.0";
    message["method"] = method;
    if (!params.isEmpty()) {
        message["params"] = params;
    }
    sendMessage(message);
}

void JsonRpcTransport::onReadyRead()
{
    m_buffer.append(m_process->readAllStandardOutput());
    
    // 循环解析，因为缓冲区可能包含多条消息
    while (tryParseMessage()) {
        // 继续解析下一条
    }
}

void JsonRpcTransport::onProcessError(QProcess::ProcessError error)
{
    QString errorStr;
    switch (error) {
        case QProcess::FailedToStart:
            errorStr = "LSP 服务器启动失败";
            break;
        case QProcess::Crashed:
            errorStr = "LSP 服务器崩溃";
            break;
        case QProcess::WriteError:
            errorStr = "写入 LSP 服务器失败";
            break;
        case QProcess::ReadError:
            errorStr = "读取 LSP 服务器失败";
            break;
        default:
            errorStr = "LSP 服务器未知错误";
    }
    emit errorOccurred(errorStr);
}

bool JsonRpcTransport::tryParseMessage()
{
    // LSP 消息格式：
    // Content-Length: <length>\r\n
    // \r\n
    // <json body>
    
    // 如果还没解析出消息长度
    if (m_expectedLength < 0) {
        // 查找头部结束位置
        int headerEnd = m_buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return false;  // 头部还不完整
        }
        
        // 解析 Content-Length
        QByteArray header = m_buffer.left(headerEnd);
        static QRegularExpression contentLengthRegex("Content-Length:\\s*(\\d+)", 
                                                      QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = contentLengthRegex.match(QString::fromUtf8(header));
        
        if (!match.hasMatch()) {
            emit errorOccurred("无效的 LSP 消息头：缺少 Content-Length");
            m_buffer.clear();
            return false;
        }
        
        m_expectedLength = match.captured(1).toInt();
        m_buffer = m_buffer.mid(headerEnd + 4);  // 跳过 \r\n\r\n
    }
    
    // 检查消息体是否完整
    if (m_buffer.size() < m_expectedLength) {
        return false;  // 消息体还不完整
    }
    
    // 提取 JSON 消息体
    QByteArray jsonData = m_buffer.left(m_expectedLength);
    m_buffer = m_buffer.mid(m_expectedLength);
    m_expectedLength = -1;  // 重置
    
    // 解析 JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred(QString("JSON 解析错误：%1").arg(parseError.errorString()));
        return true;  // 继续尝试解析下一条
    }
    
    if (!doc.isObject()) {
        emit errorOccurred("无效的 JSON-RPC 消息：不是对象");
        return true;
    }
    
    QJsonObject message = doc.object();
    qDebug() << "[LSP RX]" << jsonData.left(200);
    
    emit messageReceived(message);
    return true;
}

QByteArray JsonRpcTransport::encodeMessage(const QByteArray &json)
{
    QByteArray header = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n";
    return header + json;
}
