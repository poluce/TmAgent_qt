#ifndef MCPTOOLPROVIDER_H
#define MCPTOOLPROVIDER_H

#include "IToolProvider.h"
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QVector>

class McpToolProvider : public QObject, public IToolProvider {
    Q_OBJECT
public:
    struct ServerConfig {
        QString name;
        QUrl url;
        QString authHeader = "Authorization";
        QString authToken;
        int timeoutMs = 30000;
        bool prefixToolName = true;
        bool async = false;
    };

    explicit McpToolProvider(QObject* parent = nullptr);

    void addServer(const ServerConfig& config);
    bool addServerFromSpec(const QString& spec);
    void clearServers();
    void setCacheTtlMs(int ttlMs);

    QList<Tool> listTools() const override;
    ToolResult execute(const ToolCall& call) override;

private:
    struct ToolInfo {
        QString serverName;
        QString originalName;
        Tool schema;
    };

    bool refreshToolsIfNeeded() const;
    bool refreshTools() const;
    ToolResult callTool(const ServerConfig& config, const QString& toolName, const QJsonObject& args) const;
    ToolResult handleRpcResponse(const QByteArray& data) const;
    QString extractTextFromMcpResult(const QJsonObject& result) const;
    QNetworkReply* postJson(
        const ServerConfig& config,
        const QJsonObject& payload,
        int timeoutMs,
        QString& errorOut
    ) const;
    QNetworkRequest buildRequest(const ServerConfig& config) const;
    const ServerConfig* findServer(const QString& name) const;

    mutable QNetworkAccessManager* m_manager = nullptr;
    QVector<ServerConfig> m_servers;
    mutable QMap<QString, Tool> m_tools;
    mutable QMap<QString, ToolInfo> m_toolIndex;
    mutable QElapsedTimer m_cacheTimer;
    int m_cacheTtlMs = 60000;
    mutable int m_nextId = 1;
};

#endif // MCPTOOLPROVIDER_H
