#ifndef INTERACTIVECLI_H
#define INTERACTIVECLI_H

#include "core/agent/ToolTypes.h"
#include <QObject>
#include <QString>

class ChatService;

/**
 * @brief 交互式 CLI 对话模式
 *
 * 复用 ChatService 完整对话链路，支持助手选择、多轮对话、流式输出。
 * 用法: TmAgentCli --interactive [--verbose]
 */
class InteractiveCli : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString modelConfigPath;
        bool verbose = false;
    };

    explicit InteractiveCli(const Options& opts, QObject* parent = nullptr);

    void run();

    static constexpr int ExitSuccess = 0;
    static constexpr int ExitError = 1;

signals:
    void done(int exitCode);

public slots:
    void onStdinLine(const QString& line);

private:
    void initServices();
    void showAgentList();
    void selectAgent(int idx);
    void enterRepl();
    void processLine(const QString& line);
    void promptInput();

    // ChatService 信号处理
    void onStreamData(const QString& sessionId, const QString& data);
    void onFinished(const QString& sessionId, const QString& content);
    void onError(const QString& sessionId, const QString& error);
    void onToolEvent(const QString& sessionId, const ToolExecutionEvent& event);

    void print(const QString& msg);
    void printErr(const QString& msg);

    Options m_opts;
    ChatService* m_chatService = nullptr;
    QString m_currentSessionId;
    QString m_currentAgentId;
    bool m_waitingForResponse = false;
    bool m_streamStarted = false;
};

#endif // INTERACTIVECLI_H
