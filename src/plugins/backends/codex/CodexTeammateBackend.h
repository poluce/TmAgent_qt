#ifndef CODEXTEAMMATEBACKEND_H
#define CODEXTEAMMATEBACKEND_H

#include "ITeammateBackend.h"
#include <QHash>
#include <QObject>
#include <QString>

class CodexAppServerClient;
class QTimer;

/**
 * @brief Codex 后端实现
 *
 * 管理一个长驻的 CodexAppServerClient 进程，
 * 每个队友对应一个 Codex Thread。
 */
class CodexTeammateBackend : public QObject, public ITeammateBackend {
    Q_OBJECT
public:
    explicit CodexTeammateBackend(QObject* parent = nullptr);
    ~CodexTeammateBackend() override;

    QString backendId() const override { return QStringLiteral("codex"); }
    bool ensureReady(QString* error = nullptr) override;
    bool isReady() const override;
    CreateResult createSession(Teammate* mate) override;
    SendResult sendMessage(Teammate* mate, const QString& text) override;
    bool cancelTurn(Teammate* mate, QString* error = nullptr) override;
    void destroySession(Teammate* mate) override;
    void shutdown() override;

private:
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

    Teammate* findByThreadId(const QString& threadId) const;

    struct PendingRequest {
        Teammate* mate = nullptr;
        enum Type { Initialize, ThreadStart, TurnStart };
        Type type;
    };

    CodexAppServerClient* m_server = nullptr;
    QHash<QString, Teammate*> m_threadToMate;          // threadId → Teammate*
    QHash<QString, PendingRequest> m_pendingRequests;   // requestId → pending
    QHash<QString, QString> m_accumulatedText;          // threadId → accumulated text
    QHash<QString, QTimer*> m_turnTimeoutTimers;        // threadId → timeout timer
    QString m_initializeRequestId;
    bool m_serverReady = false;

    void startTurnTimeout(const QString& threadId, int timeoutMs);
    void cancelTurnTimeout(const QString& threadId);
    static constexpr int kDefaultTurnTimeoutMs = 120000; // 2 分钟
};

#endif // CODEXTEAMMATEBACKEND_H
