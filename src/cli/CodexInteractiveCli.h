#ifndef CODEXINTERACTIVECLI_H
#define CODEXINTERACTIVECLI_H

#include "CodexAppServerClient.h"
#include <QObject>
#include <QString>
#include <QStringList>

class CodexInteractiveCli : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString codexBin;
        QString workspaceDir;
        QString resumeThreadId;
        bool viaWsl = false;
        bool verbose = false;
    };

    explicit CodexInteractiveCli(const Options& opts, QObject* parent = nullptr);

    void run();

    static constexpr int ExitSuccess = 0;
    static constexpr int ExitError = 1;

signals:
    void done(int exitCode);

public slots:
    void onStdinLine(const QString& line);

private:
    struct PendingApproval {
        QString requestId;
        QString method;
        QString threadId;
        QString turnId;
        QString itemId;
        QString command;
        QString cwd;
        QString reason;
        QString grantRoot;
        QStringList availableDecisions;

        bool isActive() const { return !requestId.isEmpty(); }
        void clear() { *this = PendingApproval(); }
    };

    void enterRepl();
    void promptInput();
    void processLine(const QString& line);
    void print(const QString& msg);
    void printErr(const QString& msg);
    void printApprovalPrompt();
    void handleApprovalInput(const QString& line);
    void startThread();
    void sendUserTurn(const QString& text);
    void finishTurn(bool success, const QString& summary = QString());

    void onTransportError(const QString& message);
    void onResponseReceived(const QString& requestId, const QJsonValue& result);
    void onResponseErrorReceived(const QString& requestId, int code, const QString& message, const QJsonObject& data);
    void onThreadStarted(const QString& threadId, const QJsonObject& thread);
    void onTurnStarted(const QString& threadId, const QString& turnId, const QString& status);
    void onTurnCompleted(const QString& threadId, const QString& turnId, const QString& status, const QJsonObject& error);
    void onAssistantMessageDelta(const QString& threadId, const QString& turnId, const QString& itemId, const QString& delta);
    void onAssistantMessageCompleted(const QString& threadId, const QString& turnId, const QString& itemId, const QString& text);
    void onCommandApprovalRequested(const QString& requestId,
                                    const QString& threadId,
                                    const QString& turnId,
                                    const QString& itemId,
                                    const QString& command,
                                    const QString& cwd,
                                    const QString& reason,
                                    const QStringList& availableDecisions);
    void onFileChangeApprovalRequested(const QString& requestId,
                                       const QString& threadId,
                                       const QString& turnId,
                                       const QString& itemId,
                                       const QString& reason,
                                       const QString& grantRoot);

    Options m_opts;
    CodexAppServerClient* m_client = nullptr;
    QString m_threadId;
    QString m_activeTurnId;
    QString m_initializeRequestId;
    QString m_threadStartRequestId;
    QString m_turnStartRequestId;
    bool m_waitingForTurn = false;
    bool m_streamStarted = false;
    bool m_startedRepl = false;
    PendingApproval m_pendingApproval;
};

#endif // CODEXINTERACTIVECLI_H
