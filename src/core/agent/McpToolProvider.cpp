#include "McpToolProvider.h"
#include "core/agent/AgentEventBus.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QEventLoop>
#include <QUrl>

McpToolProvider::McpToolProvider(QObject* parent)
    : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
}

void McpToolProvider::addServer(const ServerConfig& config)
{
    if (!config.url.isValid()) {
        AgentEventBus::instance()->postLog("MCP server URL 无效，已忽略", "warning");
        return;
    }
    m_servers.append(config);
    m_cacheTimer.invalidate();
}

void McpToolProvider::clearServers()
{
    m_servers.clear();
    m_tools.clear();
    m_toolIndex.clear();
    m_cacheTimer.invalidate();
}

bool McpToolProvider::addServerFromSpec(const QString& spec)
{
    if (spec.trimmed().isEmpty())
        return false;

    // 格式: name|url|token|header|prefix|async
    const QStringList parts = spec.split('|');
    ServerConfig cfg;
    if (parts.size() == 1) {
        cfg.url = QUrl(parts[0].trimmed());
        cfg.name = cfg.url.host();
    } else {
        cfg.name = parts.value(0).trimmed();
        cfg.url = QUrl(parts.value(1).trimmed());
        cfg.authToken = parts.value(2).trimmed();
        const QString header = parts.value(3).trimmed();
        if (!header.isEmpty())
            cfg.authHeader = header;
        const QString prefixFlag = parts.value(4).trimmed();
        if (!prefixFlag.isEmpty())
            cfg.prefixToolName = (prefixFlag != "0");
        const QString asyncFlag = parts.value(5).trimmed();
        if (!asyncFlag.isEmpty()) {
            cfg.async = (asyncFlag == "1" || asyncFlag.compare("true", Qt::CaseInsensitive) == 0
                || asyncFlag.compare("async", Qt::CaseInsensitive) == 0);
        }
    }

    if (cfg.name.isEmpty())
        cfg.name = cfg.url.host();
    if (!cfg.url.isValid())
        return false;

    addServer(cfg);
    return true;
}

void McpToolProvider::setCacheTtlMs(int ttlMs)
{
    if (ttlMs < 1000)
        ttlMs = 1000;
    m_cacheTtlMs = ttlMs;
}

QList<Tool> McpToolProvider::listTools() const
{
    refreshToolsIfNeeded();
    return m_tools.values();
}

ToolResult McpToolProvider::execute(const ToolCall& call)
{
    if (!refreshToolsIfNeeded()) {
        return ToolResult("MCP 工具未就绪", "MCP 工具不可用", false);
    }

    if (!m_toolIndex.contains(call.name)) {
        refreshTools();
    }
    if (!m_toolIndex.contains(call.name)) {
        return ToolResult(QString("错误: 未知的 MCP 工具 %1").arg(call.name), "执行失败", false);
    }

    const ToolInfo info = m_toolIndex.value(call.name);
    const ServerConfig* server = findServer(info.serverName);
    if (!server) {
        return ToolResult(QString("错误: MCP server 未找到: %1").arg(info.serverName), "执行失败", false);
    }

    if (server->async) {
        QJsonObject payload;
        payload["jsonrpc"] = "2.0";
        payload["id"] = m_nextId++;
        payload["method"] = "tools/call";
        QJsonObject params;
        params["name"] = info.originalName;
        params["arguments"] = call.input;
        payload["params"] = params;

        QNetworkRequest request = buildRequest(*server);
        QNetworkReply* reply = m_manager->post(request, QJsonDocument(payload).toJson());

        auto* timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
        timer->start(server->timeoutMs);

        const QString toolId = call.id;
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, toolId]() {
            ToolResult result;
            if (reply->error() != QNetworkReply::NoError) {
                const QString err = reply->errorString();
                result = ToolResult(QString("错误: MCP 请求失败: %1").arg(err), "执行失败", false);
            } else {
                const QByteArray data = reply->readAll();
                result = handleRpcResponse(data);
            }
            reply->deleteLater();
            AgentEventBus::instance()->postToolResult(toolId, result.rawContent);
        });

        return ToolResult(makeDeferredToolResult("MCP 请求已发送，等待结果"), "MCP 请求已发送", true);
    }

    return callTool(*server, info.originalName, call.input);
}

bool McpToolProvider::refreshToolsIfNeeded() const
{
    if (!m_cacheTimer.isValid()) {
        return refreshTools();
    }
    if (m_cacheTimer.elapsed() > m_cacheTtlMs) {
        return refreshTools();
    }
    return !m_tools.isEmpty();
}

bool McpToolProvider::refreshTools() const
{
    m_tools.clear();
    m_toolIndex.clear();

    if (m_servers.isEmpty())
        return false;

    for (const ServerConfig& server : m_servers) {
        QJsonObject payload;
        payload["jsonrpc"] = "2.0";
        payload["id"] = m_nextId++;
        payload["method"] = "tools/list";

        QString error;
        QNetworkReply* reply = postJson(server, payload, server.timeoutMs, error);
        if (!reply) {
            AgentEventBus::instance()->postLog(
                QString("MCP tools/list 失败: %1").arg(error),
                "warning"
            );
            continue;
        }

        QByteArray data = reply->readAll();
        reply->deleteLater();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull()) {
            AgentEventBus::instance()->postLog("MCP tools/list 返回非 JSON", "warning");
            continue;
        }

        const QJsonObject resultObj = doc.object().value("result").toObject();
        const QJsonArray tools = resultObj.value("tools").toArray();

        for (const QJsonValue& v : tools) {
            const QJsonObject toolObj = v.toObject();
            const QString originalName = toolObj.value("name").toString();
            if (originalName.isEmpty())
                continue;

            Tool tool;
            tool.description = toolObj.value("description").toString();
            QJsonObject schemaObj = toolObj.value("inputSchema").toObject();
            if (schemaObj.isEmpty()) {
                schemaObj = toolObj.value("parameters").toObject();
            }
            tool.inputSchema = schemaObj;

            const QString exposedName = server.prefixToolName && !server.name.isEmpty()
                ? server.name + ":" + originalName
                : originalName;
            tool.name = exposedName;

            if (!m_toolIndex.contains(tool.name)) {
                ToolInfo info;
                info.serverName = server.name;
                info.originalName = originalName;
                info.schema = tool;
                m_toolIndex.insert(tool.name, info);
                m_tools.insert(tool.name, tool);
            }
        }
    }

    m_cacheTimer.restart();
    return !m_tools.isEmpty();
}

ToolResult McpToolProvider::callTool(
    const ServerConfig& config,
    const QString& toolName,
    const QJsonObject& args
) const
{
    QJsonObject payload;
    payload["jsonrpc"] = "2.0";
    payload["id"] = m_nextId++;
    payload["method"] = "tools/call";
    QJsonObject params;
    params["name"] = toolName;
    params["arguments"] = args;
    payload["params"] = params;

    QString error;
    QNetworkReply* reply = postJson(config, payload, config.timeoutMs, error);
    if (!reply) {
        return ToolResult(QString("MCP 调用失败: %1").arg(error), "执行失败", false);
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    return handleRpcResponse(data);
}

ToolResult McpToolProvider::handleRpcResponse(const QByteArray& data) const
{
    // 支持 SSE 或完整 JSON
    const QString responseText = QString::fromUtf8(data);
    const QStringList lines = responseText.split('\n');
    for (const QString& line : lines) {
        if (!line.startsWith("data: "))
            continue;
        QJsonDocument doc = QJsonDocument::fromJson(line.mid(6).toUtf8());
        if (doc.isNull())
            continue;
        const QJsonObject result = doc.object().value("result").toObject();
        const QString text = extractTextFromMcpResult(result);
        if (!text.isEmpty()) {
            return ToolResult(text, text.left(200), true);
        }
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        return ToolResult("MCP 返回非 JSON", "执行失败", false);
    }

    const QJsonObject obj = doc.object();
    if (obj.contains("error")) {
        const QJsonObject err = obj.value("error").toObject();
        const QString msg = err.value("message").toString();
        return ToolResult(msg.isEmpty() ? "MCP error" : msg, "执行失败", false);
    }

    const QJsonObject result = obj.value("result").toObject();
    QString text = extractTextFromMcpResult(result);
    if (text.isEmpty()) {
        text = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }
    return ToolResult(text, text.left(200), true);
}

QString McpToolProvider::extractTextFromMcpResult(const QJsonObject& result) const
{
    const QJsonArray contentArr = result.value("content").toArray();
    if (contentArr.isEmpty())
        return QString();
    const QJsonObject first = contentArr.first().toObject();
    const QString text = first.value("text").toString();
    if (!text.isEmpty())
        return text;
    return QString();
}

QNetworkReply* McpToolProvider::postJson(
    const ServerConfig& config,
    const QJsonObject& payload,
    int timeoutMs,
    QString& errorOut
) const
{
    QNetworkRequest request = buildRequest(config);

    QNetworkReply* reply = m_manager->post(request, QJsonDocument(payload).toJson());

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        errorOut = reply->errorString();
        reply->deleteLater();
        return nullptr;
    }

    return reply;
}

QNetworkRequest McpToolProvider::buildRequest(const ServerConfig& config) const
{
    QNetworkRequest request;
    request.setUrl(config.url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    if (!config.authToken.isEmpty()) {
        const QString headerName = config.authHeader.isEmpty() ? "Authorization" : config.authHeader;
        QString headerValue = config.authToken;
        if (headerName.compare("Authorization", Qt::CaseInsensitive) == 0
            && !config.authToken.startsWith("Bearer ")) {
            headerValue = "Bearer " + config.authToken;
        }
        request.setRawHeader(headerName.toUtf8(), headerValue.toUtf8());
    }
    return request;
}

const McpToolProvider::ServerConfig* McpToolProvider::findServer(const QString& name) const
{
    for (int i = 0; i < m_servers.size(); ++i) {
        if (m_servers[i].name == name) {
            return &m_servers[i];
        }
    }
    return nullptr;
}
