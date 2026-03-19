#ifndef CODEXTEAMMATEMANAGER_H
#define CODEXTEAMMATEMANAGER_H

#include "core/model/CodexTeammate.h"
#include <QHash>
#include <QJsonValue>
#include <QObject>
#include <QString>

class CodexAppServerClient;

/**
 * @brief Codex 队友管理器
 *
 * 管理一个长驻的 CodexAppServerClient 进程和多个持久化队友。
 * 每个队友对应一个 Codex Thread，可多轮对话。
 */
class CodexTeammateManager : public QObject {
    Q_OBJECT
public:
    static CodexTeammateManager* instance();

    struct CreateResult {
        bool success = false;
        QString teammateId;
        QString threadId;
        QString error;
    };

    struct MessageResult {
        bool success = false;
        QString turnId;
        QString error;
    };

    // ── 队友生命周期 ──
    CreateResult createTeammate(const CodexTeammate::Config& config);
    bool removeTeammate(const QString& teammateId, QString* error = nullptr);

    // ── 对话 ──
    MessageResult sendMessage(const QString& teammateId, const QString& text);

    // ── 查询 ──
    CodexTeammate* teammate(const QString& teammateId) const;
    CodexTeammate* findByName(const QString& name) const;
    QList<CodexTeammate*> allTeammates() const;
    int teammateCount() const;

    // ── 进程管理 ──
    bool isServerRunning() const;
    void ensureServerRunning();
    void shutdownServer();

signals:
    void teammateCreated(const QString& teammateId);
    void teammateRemoved(const QString& teammateId);
    void serverStatusChanged(bool running);

private:
    explicit CodexTeammateManager(QObject* parent = nullptr);
    Q_DISABLE_COPY(CodexTeammateManager)

    void connectServerSignals();
    void onServerStarted();
    void onServerStopped(int exitCode);
    void onResponseReceived(const QString& requestId, const QJsonValue& result);
    void onResponseError(const QString& requestId, int code, const QString& message);
    void onTransportError(const QString& message);
    void onTurnCompleted(const QString& threadId, const QString& turnId,
                         const QString& status, const QJsonObject& error);
    void onAssistantMessageDelta(const QString& threadId, const QString& turnId,
                                 const QString& itemId, const QString& delta);
    void onAssistantMessageCompleted(const QString& threadId, const QString& turnId,
                                     const QString& itemId, const QString& text);
    void onCommandApproval(const QString& requestId, const QString& threadId,
                           const QString& turnId, const QString& itemId,
                           const QString& command, const QString& cwd,
                           const QString& reason, const QStringList& decisions);
    void onFileChangeApproval(const QString& requestId, const QString& threadId,
                              const QString& turnId, const QString& itemId,
                              const QString& reason, const QString& grantRoot);

    CodexTeammate* findByThreadId(const QString& threadId) const;

    struct PendingRequest {
        QString teammateId;
        enum Type { Initialize, ThreadStart, TurnStart };
        Type type;
    };

    CodexAppServerClient* m_server = nullptr;
    QHash<QString, CodexTeammate*> m_teammates;       // teammateId → teammate
    QHash<QString, QString> m_threadToTeammate;        // threadId → teammateId
    QHash<QString, PendingRequest> m_pendingRequests;  // requestId → pending
    QHash<QString, QString> m_accumulatedText;         // threadId → accumulated turn text
    QString m_initializeRequestId;
    bool m_serverReady = false;
};

#endif // CODEXTEAMMATEMANAGER_H
