#ifndef LSPCLIENT_H
#define LSPCLIENT_H

#include <QObject>
#include <QProcess>
#include <QHash>
#include <QJsonObject>
#include <functional>
#include "LspProtocol.h"

class JsonRpcTransport;

/**
 * @brief LSP 客户端
 *
 * 管理与语言服务器的完整交互：启动/关闭、initialize 握手、
 * 文档同步、查询请求（definition/references/hover 等）。
 */
class LspClient : public QObject
{
    Q_OBJECT

public:
    enum class State {
        NotStarted,
        Starting,
        Initializing,
        Running,
        ShuttingDown,
        Stopped
    };
    Q_ENUM(State)

    explicit LspClient(const QString &serverPath,
                       const QStringList &args = {},
                       QObject *parent = nullptr);
    ~LspClient();

    // 生命周期
    bool start(const QString &rootPath);
    void shutdown();
    State state() const { return m_state; }
    bool isReady() const { return m_state == State::Running; }

    // 文档同步
    void notifyDidOpen(const QString &filePath, const QString &text,
                       const QString &languageId = QString());
    void notifyDidChange(const QString &filePath, const QString &text);
    void notifyDidClose(const QString &filePath);

    // LSP 请求
    using LocationCallback = std::function<void(const QList<Lsp::Location>&)>;
    using HoverCallback = std::function<void(const Lsp::Hover&)>;
    using SymbolCallback = std::function<void(const QList<Lsp::DocumentSymbol>&)>;
    using WorkspaceSymbolCallback = std::function<void(const QList<Lsp::SymbolInformation>&)>;
    using CallHierarchyCallback = std::function<void(const QList<Lsp::CallHierarchyItem>&)>;
    using IncomingCallsCallback = std::function<void(const QList<Lsp::CallHierarchyIncomingCall>&)>;
    using OutgoingCallsCallback = std::function<void(const QList<Lsp::CallHierarchyOutgoingCall>&)>;
    using DiagnosticsCallback = std::function<void(const QString&, const QList<Lsp::Diagnostic>&)>;

    void requestDefinition(const QString &filePath, int line, int character, LocationCallback callback);
    void requestReferences(const QString &filePath, int line, int character, LocationCallback callback);
    void requestHover(const QString &filePath, int line, int character, HoverCallback callback);
    void requestDocumentSymbols(const QString &filePath, SymbolCallback callback);
    void requestWorkspaceSymbols(const QString &query, WorkspaceSymbolCallback callback);
    void requestImplementation(const QString &filePath, int line, int character, LocationCallback callback);
    void requestPrepareCallHierarchy(const QString &filePath, int line, int character, CallHierarchyCallback callback);
    void requestIncomingCalls(const Lsp::CallHierarchyItem &item, IncomingCallsCallback callback);
    void requestOutgoingCalls(const Lsp::CallHierarchyItem &item, OutgoingCallsCallback callback);
    bool ensureDocumentOpen(const QString &filePath, const QString &languageId = QString());

signals:
    void initialized();
    void stateChanged(LspClient::State state);
    void diagnosticsReceived(const QString &uri, const QList<Lsp::Diagnostic> &diagnostics);
    void errorOccurred(const QString &error);
    
private slots:
    void onMessageReceived(const QJsonObject &message);
    void onTransportError(const QString &error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    
private:
    void setState(State state);
    void sendInitialize();
    void handleInitializeResponse(const QJsonObject &result);
    void handleResponse(int id, const QJsonValue &result, const QJsonValue &error);
    void handleNotification(const QString &method, const QJsonObject &params);
    
    int nextRequestId() { return ++m_requestId; }
    QString detectLanguageId(const QString &filePath);
    
    QString m_serverPath;
    QStringList m_args;
    QString m_rootPath;
    
    QProcess *m_process = nullptr;
    JsonRpcTransport *m_transport = nullptr;
    
    State m_state = State::NotStarted;
    int m_requestId = 0;
    
    // 请求回调映射
    QHash<int, std::function<void(const QJsonValue&)>> m_callbacks;
    QHash<QString, int> m_documentVersions;
};

#endif // LSPCLIENT_H
