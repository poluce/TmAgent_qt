#include "LspClient.h"
#include "JsonRpcTransport.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QDebug>
#include <QFile>

//=============================================================================
// 语言 ID 映射
//=============================================================================

static const QHash<QString, QString> LANGUAGE_EXTENSIONS = {
    {".c", "c"},
    {".h", "c"},
    {".cpp", "cpp"},
    {".cxx", "cpp"},
    {".cc", "cpp"},
    {".hpp", "cpp"},
    {".hxx", "cpp"},
    {".py", "python"},
    {".js", "javascript"},
    {".ts", "typescript"},
    {".jsx", "javascriptreact"},
    {".tsx", "typescriptreact"},
    {".go", "go"},
    {".rs", "rust"},
    {".java", "java"},
    {".cs", "csharp"},
    {".rb", "ruby"},
    {".php", "php"},
    {".swift", "swift"},
    {".kt", "kotlin"},
    {".scala", "scala"},
    {".lua", "lua"},
    {".sh", "shellscript"},
    {".bash", "shellscript"},
    {".zsh", "shellscript"},
    {".json", "json"},
    {".yaml", "yaml"},
    {".yml", "yaml"},
    {".xml", "xml"},
    {".html", "html"},
    {".css", "css"},
    {".scss", "scss"},
    {".less", "less"},
    {".md", "markdown"},
    {".sql", "sql"},
};

//=============================================================================
// 构造/析构
//=============================================================================

LspClient::LspClient(const QString &serverPath, const QStringList &args, QObject *parent)
    : QObject(parent)
    , m_serverPath(serverPath)
    , m_args(args)
{
}

LspClient::~LspClient()
{
    if (m_state != State::Stopped && m_state != State::NotStarted) {
        shutdown();
    }
}

//=============================================================================
// 生命周期
//=============================================================================

bool LspClient::start(const QString &rootPath)
{
    if (m_state != State::NotStarted && m_state != State::Stopped) {
        qWarning() << "LspClient: 已经启动";
        return false;
    }
    
    m_rootPath = rootPath;
    setState(State::Starting);
    
    // 创建进程
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(rootPath);
    
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &LspClient::onProcessFinished);
    
    // 启动进程
    qDebug() << "LspClient: 启动" << m_serverPath << m_args;
    m_process->start(m_serverPath, m_args);
    
    if (!m_process->waitForStarted(5000)) {
        emit errorOccurred(QString("启动 LSP 服务器失败: %1").arg(m_process->errorString()));
        setState(State::Stopped);
        return false;
    }
    
    // 创建传输层
    m_transport = new JsonRpcTransport(m_process, this);
    connect(m_transport, &JsonRpcTransport::messageReceived,
            this, &LspClient::onMessageReceived);
    connect(m_transport, &JsonRpcTransport::errorOccurred,
            this, &LspClient::onTransportError);
    
    // 发送初始化请求
    setState(State::Initializing);
    sendInitialize();
    
    return true;
}

void LspClient::shutdown()
{
    if (m_state == State::Stopped || m_state == State::NotStarted) {
        return;
    }
    
    setState(State::ShuttingDown);
    
    // 发送 shutdown 请求
    if (m_transport && m_process && m_process->state() == QProcess::Running) {
        m_transport->sendRequest("shutdown", QJsonObject(), nextRequestId());
        
        // 等待一小段时间让服务器处理
        m_process->waitForFinished(1000);
        
        // 发送 exit 通知
        m_transport->sendNotification("exit", QJsonObject());
        
        // 等待进程退出
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
        }
    }
    
    // 清理
    if (m_transport) {
        m_transport->deleteLater();
        m_transport = nullptr;
    }
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    
    m_callbacks.clear();
    m_documentVersions.clear();
    
    setState(State::Stopped);
}

void LspClient::setState(State state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}

//=============================================================================
// 初始化
//=============================================================================

void LspClient::sendInitialize()
{
    QJsonObject capabilities;
    
    // 文本文档能力
    QJsonObject textDocumentSync;
    textDocumentSync["openClose"] = true;
    textDocumentSync["change"] = 1;  // Full sync
    
    QJsonObject textDocument;
    textDocument["synchronization"] = textDocumentSync;
    textDocument["publishDiagnostics"] = QJsonObject{{"versionSupport", true}};
    
    capabilities["textDocument"] = textDocument;
    
    // 工作区能力
    QJsonObject workspace;
    workspace["configuration"] = true;
    workspace["workspaceFolders"] = true;
    capabilities["workspace"] = workspace;
    
    // 参数
    QJsonObject params;
    params["processId"] = static_cast<int>(QCoreApplication::applicationPid());
    params["rootUri"] = Lsp::pathToUri(m_rootPath);
    params["capabilities"] = capabilities;
    
    QJsonArray workspaceFolders;
    workspaceFolders.append(QJsonObject{
        {"uri", Lsp::pathToUri(m_rootPath)},
        {"name", QFileInfo(m_rootPath).fileName()}
    });
    params["workspaceFolders"] = workspaceFolders;
    
    int id = nextRequestId();
    m_callbacks[id] = [this](const QJsonValue &result) {
        handleInitializeResponse(result.toObject());
    };
    
    m_transport->sendRequest("initialize", params, id);
}

void LspClient::handleInitializeResponse(const QJsonObject &result)
{
    qDebug() << "LspClient: 初始化响应" << result;
    
    // 发送 initialized 通知
    m_transport->sendNotification("initialized", QJsonObject());
    
    setState(State::Running);
    emit initialized();
}

//=============================================================================
// 消息处理
//=============================================================================

void LspClient::onMessageReceived(const QJsonObject &message)
{
    // 响应消息
    if (message.contains("id") && (message.contains("result") || message.contains("error"))) {
        int id = message["id"].toInt();
        handleResponse(id, message.value("result"), message.value("error"));
        return;
    }
    
    // 通知消息
    if (message.contains("method") && !message.contains("id")) {
        QString method = message["method"].toString();
        QJsonObject params = message["params"].toObject();
        handleNotification(method, params);
        return;
    }
    
    // 请求消息（服务器发给客户端的请求）
    if (message.contains("method") && message.contains("id")) {
        QString method = message["method"].toString();
        int id = message["id"].toInt();
        
        // 处理常见的服务器请求
        if (method == "workspace/configuration") {
            // 返回空配置
            QJsonObject response;
            response["jsonrpc"] = "2.0";
            response["id"] = id;
            response["result"] = QJsonArray{QJsonObject()};
            m_transport->sendMessage(response);
        } else if (method == "client/registerCapability" || 
                   method == "client/unregisterCapability") {
            // 确认注册
            QJsonObject response;
            response["jsonrpc"] = "2.0";
            response["id"] = id;
            response["result"] = QJsonValue();
            m_transport->sendMessage(response);
        } else if (method == "window/workDoneProgress/create") {
            // 确认进度
            QJsonObject response;
            response["jsonrpc"] = "2.0";
            response["id"] = id;
            response["result"] = QJsonValue();
            m_transport->sendMessage(response);
        }
    }
}

void LspClient::handleResponse(int id, const QJsonValue &result, const QJsonValue &error)
{
    auto it = m_callbacks.find(id);
    if (it == m_callbacks.end()) {
        return;
    }
    
    auto callback = it.value();
    m_callbacks.erase(it);
    
    if (!error.isUndefined() && !error.isNull()) {
        QString errorMsg;
        if (error.isObject()) {
            errorMsg = error.toObject().value("message").toString();
        } else if (error.isString()) {
            errorMsg = error.toString();
        } else {
            errorMsg = "未知错误";
        }
        qWarning() << "LspClient: 请求错误" << id << errorMsg;
        emit errorOccurred(errorMsg);
        return;
    }
    
    callback(result);
}

void LspClient::handleNotification(const QString &method, const QJsonObject &params)
{
    if (method == "textDocument/publishDiagnostics") {
        QString uri = params["uri"].toString();
        QJsonArray diagnosticsArr = params["diagnostics"].toArray();
        
        QList<Lsp::Diagnostic> diagnostics;
        for (const auto &d : diagnosticsArr) {
            diagnostics.append(Lsp::Diagnostic::fromJson(d.toObject()));
        }
        
        emit diagnosticsReceived(uri, diagnostics);
    }
    // 可以添加其他通知处理
}

void LspClient::onTransportError(const QString &error)
{
    emit errorOccurred(error);
}

void LspClient::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    qDebug() << "LspClient: 进程退出" << exitCode << status;
    if (m_state != State::ShuttingDown && m_state != State::Stopped) {
        emit errorOccurred(QString("LSP 服务器意外退出 (代码: %1)").arg(exitCode));
        setState(State::Stopped);
    }
}

//=============================================================================
// 文档同步
//=============================================================================

void LspClient::notifyDidOpen(const QString &filePath, const QString &text, 
                               const QString &languageId)
{
    if (!isReady()) return;
    
    QString uri = Lsp::pathToUri(filePath);
    QString langId = languageId.isEmpty() ? detectLanguageId(filePath) : languageId;
    
    m_documentVersions[uri] = 0;
    
    QJsonObject textDocument;
    textDocument["uri"] = uri;
    textDocument["languageId"] = langId;
    textDocument["version"] = 0;
    textDocument["text"] = text;
    
    QJsonObject params;
    params["textDocument"] = textDocument;
    
    m_transport->sendNotification("textDocument/didOpen", params);
}

void LspClient::notifyDidChange(const QString &filePath, const QString &text)
{
    if (!isReady()) return;
    
    QString uri = Lsp::pathToUri(filePath);
    int version = m_documentVersions.value(uri, 0) + 1;
    m_documentVersions[uri] = version;
    
    QJsonObject textDocument;
    textDocument["uri"] = uri;
    textDocument["version"] = version;
    
    QJsonArray contentChanges;
    contentChanges.append(QJsonObject{{"text", text}});
    
    QJsonObject params;
    params["textDocument"] = textDocument;
    params["contentChanges"] = contentChanges;
    
    m_transport->sendNotification("textDocument/didChange", params);
}

void LspClient::notifyDidClose(const QString &filePath)
{
    if (!isReady()) return;
    
    QString uri = Lsp::pathToUri(filePath);
    m_documentVersions.remove(uri);
    
    QJsonObject params;
    params["textDocument"] = QJsonObject{{"uri", uri}};
    
    m_transport->sendNotification("textDocument/didClose", params);
}

//=============================================================================
// LSP 请求
//=============================================================================

void LspClient::requestDefinition(const QString &filePath, int line, int character,
                                   LocationCallback callback)
{
    if (!isReady()) return;
    
    QString uri = Lsp::pathToUri(filePath);
    
    QJsonObject params;
    params["textDocument"] = QJsonObject{{"uri", uri}};
    params["position"] = QJsonObject{{"line", line}, {"character", character}};
    
    int id = nextRequestId();
    m_callbacks[id] = [callback](const QJsonValue &result) {
        QList<Lsp::Location> locations;
        
        if (result.isArray()) {
            for (const auto &loc : result.toArray()) {
                locations.append(Lsp::Location::fromJson(loc.toObject()));
            }
        } else if (result.isObject()) {
            locations.append(Lsp::Location::fromJson(result.toObject()));
        }
        
        callback(locations);
    };
    
    m_transport->sendRequest("textDocument/definition", params, id);
}

void LspClient::requestReferences(const QString &filePath, int line, int character,
                                   LocationCallback callback)
{
    if (!isReady()) return;
    
    QString uri = Lsp::pathToUri(filePath);
    
    QJsonObject params;
    params["textDocument"] = QJsonObject{{"uri", uri}};
    params["position"] = QJsonObject{{"line", line}, {"character", character}};
    params["context"] = QJsonObject{{"includeDeclaration", true}};
    
    int id = nextRequestId();
    m_callbacks[id] = [callback](const QJsonValue &result) {
        QList<Lsp::Location> locations;
        
        for (const auto &loc : result.toArray()) {
            locations.append(Lsp::Location::fromJson(loc.toObject()));
        }
        
        callback(locations);
    };
    
    m_transport->sendRequest("textDocument/references", params, id);
}

void LspClient::requestHover(const QString &filePath, int line, int character,
                              HoverCallback callback)
{
    if (!isReady()) return;
    
    QString uri = Lsp::pathToUri(filePath);
    
    QJsonObject params;
    params["textDocument"] = QJsonObject{{"uri", uri}};
    params["position"] = QJsonObject{{"line", line}, {"character", character}};
    
    int id = nextRequestId();
    m_callbacks[id] = [callback](const QJsonValue &result) {
        Lsp::Hover hover;
        if (!result.isNull()) {
            hover = Lsp::Hover::fromJson(result.toObject());
        }
        callback(hover);
    };
    
    m_transport->sendRequest("textDocument/hover", params, id);
}

void LspClient::requestDocumentSymbols(const QString &filePath, SymbolCallback callback)
{
    if (!isReady()) return;
    
    QString uri = Lsp::pathToUri(filePath);
    
    QJsonObject params;
    params["textDocument"] = QJsonObject{{"uri", uri}};
    
    int id = nextRequestId();
    m_callbacks[id] = [callback](const QJsonValue &result) {
        QList<Lsp::DocumentSymbol> symbols;
        
        for (const auto &sym : result.toArray()) {
            symbols.append(Lsp::DocumentSymbol::fromJson(sym.toObject()));
        }
        
        callback(symbols);
    };
    
    m_transport->sendRequest("textDocument/documentSymbol", params, id);
}

void LspClient::requestWorkspaceSymbols(const QString &query, WorkspaceSymbolCallback callback)
{
    if (!isReady()) return;
    
    QJsonObject params;
    params["query"] = query;
    
    int id = nextRequestId();
    m_callbacks[id] = [callback](const QJsonValue &result) {
        QList<Lsp::SymbolInformation> symbols;
        for (const auto &sym : result.toArray()) {
            symbols.append(Lsp::SymbolInformation::fromJson(sym.toObject()));
        }
        callback(symbols);
    };
    
    m_transport->sendRequest("workspace/symbol", params, id);
}

void LspClient::requestImplementation(const QString &filePath, int line, int character,
                                      LocationCallback callback)
{
    if (!isReady()) return;
    
    QString uri = Lsp::pathToUri(filePath);
    QJsonObject params;
    params["textDocument"] = QJsonObject{{"uri", uri}};
    params["position"] = QJsonObject{{"line", line}, {"character", character}};
    
    int id = nextRequestId();
    m_callbacks[id] = [callback](const QJsonValue &result) {
        QList<Lsp::Location> locations;
        if (result.isArray()) {
            for (const auto &loc : result.toArray()) {
                locations.append(Lsp::Location::fromJson(loc.toObject()));
            }
        } else if (result.isObject()) {
            locations.append(Lsp::Location::fromJson(result.toObject()));
        }
        callback(locations);
    };
    
    m_transport->sendRequest("textDocument/implementation", params, id);
}

void LspClient::requestPrepareCallHierarchy(const QString &filePath, int line, int character,
                                            CallHierarchyCallback callback)
{
    if (!isReady()) return;
    
    QString uri = Lsp::pathToUri(filePath);
    QJsonObject params;
    params["textDocument"] = QJsonObject{{"uri", uri}};
    params["position"] = QJsonObject{{"line", line}, {"character", character}};
    
    int id = nextRequestId();
    m_callbacks[id] = [callback](const QJsonValue &result) {
        QList<Lsp::CallHierarchyItem> items;
        for (const auto &it : result.toArray()) {
            items.append(Lsp::CallHierarchyItem::fromJson(it.toObject()));
        }
        callback(items);
    };
    
    m_transport->sendRequest("textDocument/prepareCallHierarchy", params, id);
}

void LspClient::requestIncomingCalls(const Lsp::CallHierarchyItem &item, IncomingCallsCallback callback)
{
    if (!isReady()) return;
    
    QJsonObject params;
    params["item"] = item.toJson();
    
    int id = nextRequestId();
    m_callbacks[id] = [callback](const QJsonValue &result) {
        QList<Lsp::CallHierarchyIncomingCall> calls;
        for (const auto &c : result.toArray()) {
            calls.append(Lsp::CallHierarchyIncomingCall::fromJson(c.toObject()));
        }
        callback(calls);
    };
    
    m_transport->sendRequest("callHierarchy/incomingCalls", params, id);
}

void LspClient::requestOutgoingCalls(const Lsp::CallHierarchyItem &item, OutgoingCallsCallback callback)
{
    if (!isReady()) return;
    
    QJsonObject params;
    params["item"] = item.toJson();
    
    int id = nextRequestId();
    m_callbacks[id] = [callback](const QJsonValue &result) {
        QList<Lsp::CallHierarchyOutgoingCall> calls;
        for (const auto &c : result.toArray()) {
            calls.append(Lsp::CallHierarchyOutgoingCall::fromJson(c.toObject()));
        }
        callback(calls);
    };
    
    m_transport->sendRequest("callHierarchy/outgoingCalls", params, id);
}

bool LspClient::ensureDocumentOpen(const QString &filePath, const QString &languageId)
{
    if (!isReady()) return false;

    QString uri = Lsp::pathToUri(filePath);
    if (m_documentVersions.contains(uri)) {
        return true;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred(QString("无法打开文件: %1").arg(filePath));
        return false;
    }

    QString text = QString::fromUtf8(file.readAll());
    notifyDidOpen(filePath, text, languageId);
    return true;
}

//=============================================================================
// 辅助函数
//=============================================================================

QString LspClient::detectLanguageId(const QString &filePath)
{
    QString ext = QFileInfo(filePath).suffix();
    if (!ext.startsWith('.')) {
        ext = '.' + ext;
    }
    return LANGUAGE_EXTENSIONS.value(ext.toLower(), "plaintext");
}
