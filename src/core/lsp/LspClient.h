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
 * 管理与语言服务器的完整交互，包括：
 * - 启动/关闭服务器进程
 * - initialize/initialized 握手
 * - 文档同步（didOpen/didChange/didClose）
 * - 查询请求（definition/references/hover 等）
 * 
 * 仿照 Qt 6 的 QLanguageServer 和 opencode 的 LSPClient 设计
 */
class LspClient : public QObject
{
    Q_OBJECT
    
public:
    /**
     * @brief 客户端状态
     */
    enum class State {
        NotStarted,     // 未启动
        Starting,       // 正在启动
        Initializing,   // 正在初始化握手
        Running,        // 正常运行
        ShuttingDown,   // 正在关闭
        Stopped         // 已停止
    };
    Q_ENUM(State)
    
    /**
     * @brief 构造函数
     * @param serverPath 语言服务器可执行文件路径
     * @param args 启动参数
     * @param parent 父对象
     */
    explicit LspClient(const QString &serverPath, 
                       const QStringList &args = {},
                       QObject *parent = nullptr);
    ~LspClient();
    
    //=========================================================================
    // 生命周期
    //=========================================================================
    
    /**
     * @brief 启动语言服务器
     * @param rootPath 项目根目录
     * @return 是否成功启动进程
     */
    bool start(const QString &rootPath);
    
    /**
     * @brief 关闭语言服务器
     */
    void shutdown();
    
    /**
     * @brief 获取当前状态
     */
    State state() const { return m_state; }
    
    /**
     * @brief 是否已初始化完成
     */
    bool isReady() const { return m_state == State::Running; }
    
    //=========================================================================
    // 文档同步
    //=========================================================================
    
    /**
     * @brief 通知服务器打开文档
     * @param filePath 文件路径
     * @param text 文件内容
     * @param languageId 语言标识（如 "cpp", "python"）
     */
    void notifyDidOpen(const QString &filePath, const QString &text, 
                       const QString &languageId = QString());
    
    /**
     * @brief 通知服务器文档内容变更
     * @param filePath 文件路径
     * @param text 新的文件内容
     */
    void notifyDidChange(const QString &filePath, const QString &text);
    
    /**
     * @brief 通知服务器关闭文档
     * @param filePath 文件路径
     */
    void notifyDidClose(const QString &filePath);
    
    //=========================================================================
    // LSP 请求
    //=========================================================================
    
    using LocationCallback = std::function<void(const QList<Lsp::Location>&)>;
    using HoverCallback = std::function<void(const Lsp::Hover&)>;
    using SymbolCallback = std::function<void(const QList<Lsp::DocumentSymbol>&)>;
    using WorkspaceSymbolCallback = std::function<void(const QList<Lsp::SymbolInformation>&)>;
    using CallHierarchyCallback = std::function<void(const QList<Lsp::CallHierarchyItem>&)>;
    using IncomingCallsCallback = std::function<void(const QList<Lsp::CallHierarchyIncomingCall>&)>;
    using OutgoingCallsCallback = std::function<void(const QList<Lsp::CallHierarchyOutgoingCall>&)>;
    using DiagnosticsCallback = std::function<void(const QString&, const QList<Lsp::Diagnostic>&)>;
    
    /**
     * @brief 跳转到定义
     */
    void requestDefinition(const QString &filePath, int line, int character, 
                           LocationCallback callback);
    
    /**
     * @brief 查找引用
     */
    void requestReferences(const QString &filePath, int line, int character,
                           LocationCallback callback);
    
    /**
     * @brief 悬停提示
     */
    void requestHover(const QString &filePath, int line, int character,
                      HoverCallback callback);
    
    /**
     * @brief 获取文档符号
     */
    void requestDocumentSymbols(const QString &filePath, SymbolCallback callback);

    /**
     * @brief 获取工作区符号
     */
    void requestWorkspaceSymbols(const QString &query, WorkspaceSymbolCallback callback);

    /**
     * @brief 获取实现
     */
    void requestImplementation(const QString &filePath, int line, int character,
                               LocationCallback callback);

    /**
     * @brief 准备调用层级
     */
    void requestPrepareCallHierarchy(const QString &filePath, int line, int character,
                                     CallHierarchyCallback callback);

    /**
     * @brief 获取呼入调用
     */
    void requestIncomingCalls(const Lsp::CallHierarchyItem &item, IncomingCallsCallback callback);

    /**
     * @brief 获取呼出调用
     */
    void requestOutgoingCalls(const Lsp::CallHierarchyItem &item, OutgoingCallsCallback callback);

    /**
     * @brief 确保文档已打开（didOpen）
     * @param filePath 文件路径
     * @param languageId 语言标识（可选）
     * @return 成功则返回 true
     */
    bool ensureDocumentOpen(const QString &filePath, const QString &languageId = QString());
    
signals:
    /**
     * @brief 初始化完成
     */
    void initialized();
    
    /**
     * @brief 状态变化
     */
    void stateChanged(LspClient::State state);
    
    /**
     * @brief 收到诊断信息
     */
    void diagnosticsReceived(const QString &uri, const QList<Lsp::Diagnostic> &diagnostics);
    
    /**
     * @brief 发生错误
     */
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
    
    // 文档版本跟踪
    QHash<QString, int> m_documentVersions;
};

#endif // LSPCLIENT_H
