#ifndef CODEXAPPSERVERCLIENT_H
#define CODEXAPPSERVERCLIENT_H

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QStringList>

class CodexAppServerClient : public QObject {
    Q_OBJECT
public:
    struct LaunchOptions {
        QString program;
        QStringList extraArguments;
        QString workingDirectory;
        bool viaWsl = false;
        QString clientName;
        QString clientTitle;
        QString clientVersion;
        bool experimentalApi = true;
        QStringList optOutNotificationMethods;
    };

    explicit CodexAppServerClient(QObject* parent = nullptr);
    ~CodexAppServerClient() override;

    static LaunchOptions defaultLaunchOptions();
    static QStringList defaultOptOutNotificationMethods();
    static QJsonObject makeTextInput(const QString& text);

    void setLaunchOptions(const LaunchOptions& options);
    LaunchOptions launchOptions() const;

    void start();
    void shutdown(int timeoutMs = 2000);

    bool isRunning() const;
    bool isReady() const;
    QString programDisplayName() const;
    QString effectiveServerWorkingDirectory() const;

    QString sendRequest(const QString& method, const QJsonObject& params = QJsonObject());
    void sendNotification(const QString& method, const QJsonObject& params = QJsonObject(), bool includeParams = true);
    void sendServerRequestResult(const QString& requestId, const QJsonValue& result = QJsonObject());
    void sendServerRequestError(const QString& requestId, int code, const QString& message, const QJsonObject& data = QJsonObject());

    QString requestInitialize();
    void completeInitializeHandshake();

    QString requestThreadStart(const QJsonObject& overrides = QJsonObject());
    QString requestThreadResume(const QString& threadId, const QJsonObject& overrides = QJsonObject());
    QString requestTurnStartText(const QString& threadId, const QString& text, const QJsonObject& overrides = QJsonObject());

signals:
    void started();
    void stopped(int exitCode, QProcess::ExitStatus exitStatus);
    void readyChanged(bool ready);
    void stderrLineReceived(const QString& line);
    void transportError(const QString& message);
    void responseReceived(const QString& requestId, const QJsonValue& result);
    void responseErrorReceived(const QString& requestId, int code, const QString& message, const QJsonObject& data);
    void notificationReceived(const QString& method, const QJsonValue& params);
    void serverRequestReceived(const QString& requestId, const QString& method, const QJsonValue& params);
    void threadStarted(const QString& threadId, const QJsonObject& thread);
    void turnStarted(const QString& threadId, const QString& turnId, const QString& status);
    void turnCompleted(const QString& threadId, const QString& turnId, const QString& status, const QJsonObject& error);
    void assistantMessageDelta(const QString& threadId, const QString& turnId, const QString& itemId, const QString& delta);
    void assistantMessageCompleted(const QString& threadId, const QString& turnId, const QString& itemId, const QString& text);
    void commandExecutionApprovalRequested(const QString& requestId,
                                           const QString& threadId,
                                           const QString& turnId,
                                           const QString& itemId,
                                           const QString& command,
                                           const QString& cwd,
                                           const QString& reason,
                                           const QStringList& availableDecisions);
    void fileChangeApprovalRequested(const QString& requestId,
                                     const QString& threadId,
                                     const QString& turnId,
                                     const QString& itemId,
                                     const QString& reason,
                                     const QString& grantRoot);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessErrorOccurred(QProcess::ProcessError error);

private:
    QString nextRequestId();
    void emitReadyChanged(bool ready);
    void ensureProcess();
    void drainBufferedLines(QByteArray* buffer, bool stderrStream);
    void handleJsonRpcLine(const QByteArray& rawLine);
    void writeJsonRpcObject(const QJsonObject& object);
    QString normalizeId(const QJsonValue& idValue) const;
    QString resolveProgram() const;
    QStringList resolveArguments() const;
    QString toWslPath(const QString& path) const;

    QProcess* m_process = nullptr;
    LaunchOptions m_launchOptions;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    qint64 m_nextRequestId = 1;
    bool m_ready = false;
};

#endif // CODEXAPPSERVERCLIENT_H
