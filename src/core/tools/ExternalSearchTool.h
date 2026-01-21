#ifndef EXTERNALSEARCHTOOL_H
#define EXTERNALSEARCHTOOL_H

#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>

/**
 * @brief 外部搜索工具 (对标 opencode 的 codesearch/websearch)
 * 
 * 接入 Exa AI API 提供高质量的代码和网页上下文。
 */
class ExternalSearchTool {
public:
    static constexpr const char* CODESEARCH = "codesearch";
    static constexpr const char* WEBSEARCH = "websearch";

    /**
     * @brief 执行代码搜索 (针对库、SDK、示例)
     */
    static QString executeCodeSearch(const QJsonObject& input) {
        return callExaApi("get_code_context_exa", input);
    }

    /**
     * @brief 执行通用网页搜索
     */
    static QString executeWebSearch(const QJsonObject& input) {
        return callExaApi("web_search_exa", input);
    }

private:
    /**
     * @brief 通过 MCP 代理调用 Exa AI API
     */
    static QString callExaApi(const QString& methodName, const QJsonObject& args) {
        const QString baseUrl = "https://mcp.exa.ai/mcp";

        QNetworkAccessManager manager;
        QNetworkRequest request;
        request.setUrl(QUrl(baseUrl));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Accept", "application/json");

        // 构造 MCP 风格的任务调用
        QJsonObject mcpRequest;
        mcpRequest["jsonrpc"] = "2.0";
        mcpRequest["id"] = 1;
        mcpRequest["method"] = "tools/call";
        
        QJsonObject params;
        params["name"] = methodName;
        params["arguments"] = args;
        mcpRequest["params"] = params;

        QNetworkReply* reply = manager.post(request, QJsonDocument(mcpRequest).toJson());

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        
        timer.start(30000); // 搜索通常较慢，给予30秒
        loop.exec();

        if (reply->error() != QNetworkReply::NoError) {
            QString err = "搜索失败: " + reply->errorString();
            reply->deleteLater();
            return err;
        }

        QByteArray data = reply->readAll();
        reply->deleteLater();

        // 解析 SSE 或直接 JSON (Exa MCP 接口可能返回流或完整结果)
        // 仿照 opencode 的解析逻辑
        QString responseText = QString::fromUtf8(data);
        QStringList lines = responseText.split('\n');
        for (const QString& line : lines) {
            if (line.startsWith("data: ")) {
                QJsonDocument doc = QJsonDocument::fromJson(line.mid(6).toUtf8());
                QJsonObject result = doc.object()["result"].toObject();
                QJsonArray contentArr = result["content"].toArray();
                if (!contentArr.isEmpty()) {
                    return contentArr[0].toObject()["text"].toString();
                }
            }
        }

        // 如果不是 SSE 格式，尝试直接解析
        QJsonDocument fullDoc = QJsonDocument::fromJson(data);
        if (!fullDoc.isNull()) {
            QJsonArray contentArr = fullDoc.object()["result"].toObject()["content"].toArray();
            if (!contentArr.isEmpty()) {
                 return contentArr[0].toObject()["text"].toString();
            }
        }

        return "未找到相关结果。";
    }
};

#endif // EXTERNALSEARCHTOOL_H
