#include "cli/InteractiveCli.h"
#include "core/agent/DelegateTaskScheduler.h"
#include "core/manager/IdentityManager.h"
#include "core/model/Identity.h"
#include "core/model/Session.h"
#include "core/service/ChatService.h"

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstring>

extern QMutex g_consoleMutex;

namespace {

static void rawPrint(const char* text)
{
    QMutexLocker locker(&g_consoleMutex);
    std::fputs(text, stdout);
    std::fflush(stdout);
}

static void rawPrintLn(const char* text)
{
    QMutexLocker locker(&g_consoleMutex);
    std::fputs(text, stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// 从 stdin 阻塞读一行（跨平台，用 C 标准库）
static QString readLineFromStdin()
{
    char buf[4096];
    if (!std::fgets(buf, sizeof(buf), stdin))
        return QString(); // EOF
    // 去除末尾换行
    size_t len = std::strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return QString::fromLocal8Bit(buf, static_cast<int>(len));
}

// 后台 stdin 读取线程
class StdinReaderThread : public QThread {
public:
    explicit StdinReaderThread(InteractiveCli* cli, QObject* parent = nullptr)
        : QThread(parent), m_cli(cli) { }

protected:
    void run() override
    {
        while (!isInterruptionRequested()) {
            char buf[4096];
            if (!std::fgets(buf, sizeof(buf), stdin)) {
                // EOF
                QMetaObject::invokeMethod(m_cli, "onStdinLine", Qt::QueuedConnection, Q_ARG(QString, QString()));
                return;
            }
            size_t len = std::strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
                buf[--len] = '\0';
            QString line = QString::fromLocal8Bit(buf, static_cast<int>(len));
            QMetaObject::invokeMethod(m_cli, "onStdinLine", Qt::QueuedConnection, Q_ARG(QString, line));
        }
    }

private:
    InteractiveCli* m_cli;
};

} // namespace

InteractiveCli::InteractiveCli(const Options& opts, QObject* parent)
    : QObject(parent)
    , m_opts(opts)
{
}

void InteractiveCli::run()
{
    initServices();
    if (!m_chatService) {
        printErr(QStringLiteral("ChatService 初始化失败"));
        emit done(ExitError);
        return;
    }
    showAgentList();
}

void InteractiveCli::initServices()
{
    // ChatService 内部会通过 ConfigService::loadConfig() 加载模型配置
    m_chatService = new ChatService(this);
    m_chatService->initialize();
    m_chatService->loadConfig();
    m_chatService->loadSessionsFromDisk();

    // 连接信号
    connect(m_chatService, &ChatService::streamDataReceived, this, &InteractiveCli::onStreamData);
    connect(m_chatService, &ChatService::finished, this, &InteractiveCli::onFinished);
    connect(m_chatService, &ChatService::errorOccurred, this, &InteractiveCli::onError);
    connect(m_chatService, &ChatService::toolEvent, this, &InteractiveCli::onToolEvent);
}

void InteractiveCli::showAgentList()
{
    rawPrintLn("\n=== TmAgent Interactive CLI ===\n");

    IdentityManager* im = IdentityManager::instance();
    const QList<Identity*> agents = im->allAgents();

    if (agents.isEmpty()) {
        rawPrintLn("没有已注册的助手，将创建默认助手...");
        Session* session = m_chatService->createNewSession();
        if (!session) {
            printErr(QStringLiteral("无法创建默认会话"));
            emit done(ExitError);
            return;
        }
        m_currentSessionId = session->id();
        m_currentAgentId = m_chatService->agentDisplayNameForSession(m_currentSessionId);
        print(QStringLiteral("\n[%1] 会话已创建\n").arg(m_currentAgentId.isEmpty() ? QStringLiteral("默认助手") : m_currentAgentId));
        enterRepl();
        return;
    }

    rawPrintLn("可用助手:");
    for (int i = 0; i < agents.size(); ++i) {
        const QString line = QStringLiteral("  [%1] %2")
                                 .arg(i + 1)
                                 .arg(agents[i]->name());
        rawPrintLn(qPrintable(line));
    }

    rawPrint("\n请选择助手 [1-");
    rawPrint(qPrintable(QString::number(agents.size())));
    rawPrint("]: ");

    // 同步阻塞读一行（此时还未进入 REPL）
    const QString choice = readLineFromStdin().trimmed();
    bool ok = false;
    const int idx = choice.toInt(&ok);
    if (!ok || idx < 1 || idx > agents.size()) {
        rawPrintLn("无效选择，使用第一个助手");
        selectAgent(0);
    } else {
        selectAgent(idx - 1);
    }
}

void InteractiveCli::selectAgent(int idx)
{
    IdentityManager* im = IdentityManager::instance();
    const QList<Identity*> agents = im->allAgents();
    if (idx < 0 || idx >= agents.size()) {
        printErr(QStringLiteral("助手索引越界"));
        emit done(ExitError);
        return;
    }

    Identity* agent = agents[idx];
    m_currentAgentId = agent->name();

    const QList<Session*> sessions = m_chatService->sessionsForIdentity(agent->id());
    if (!sessions.isEmpty()) {
        m_currentSessionId = sessions.first()->id();
        print(QStringLiteral("\n[%1] 使用已有会话\n").arg(m_currentAgentId));
    } else {
        Session* session = m_chatService->createSessionForIdentity(agent->id());
        if (!session) {
            printErr(QStringLiteral("无法创建会话"));
            emit done(ExitError);
            return;
        }
        m_currentSessionId = session->id();
        print(QStringLiteral("\n[%1] 会话已创建\n").arg(m_currentAgentId));
    }

    enterRepl();
}

void InteractiveCli::enterRepl()
{
    rawPrintLn("输入消息开始对话。命令: /quit 退出, /switch 切换助手, /jobs 查看子Agent\n");
    promptInput();

    // 启动后台线程阻塞读 stdin
    StdinReaderThread* reader = new StdinReaderThread(this, this);
    connect(reader, &QThread::finished, reader, &QObject::deleteLater);
    reader->start();
}

void InteractiveCli::onStdinLine(const QString& line)
{
    if (line.isNull()) {
        // EOF
        rawPrintLn("\nBye!");
        m_chatService->saveSessionsToDisk();
        emit done(ExitSuccess);
        return;
    }

    if (m_waitingForResponse) {
        // 正在等待 AI 回复，忽略输入
        return;
    }

    processLine(line);
}

void InteractiveCli::processLine(const QString& line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        promptInput();
        return;
    }

    if (trimmed == QStringLiteral("/quit") || trimmed == QStringLiteral("/exit")) {
        rawPrintLn("Bye!");
        m_chatService->saveSessionsToDisk();
        emit done(ExitSuccess);
        return;
    }

    if (trimmed == QStringLiteral("/switch")) {
        m_currentSessionId.clear();
        m_currentAgentId.clear();
        showAgentList();
        return;
    }

    if (trimmed == QStringLiteral("/jobs")) {
        IdentityManager* im = IdentityManager::instance();
        const QList<Identity*> agents = im->allAgents();
        QString identityId;
        for (Identity* a : agents) {
            if (a->name() == m_currentAgentId) {
                identityId = a->id();
                break;
            }
        }
        const QString ctx = DelegateTaskScheduler::instance()->formatActiveJobsContext(identityId);
        if (ctx.isEmpty()) {
            rawPrintLn("(无活跃子Agent任务)");
        } else {
            rawPrintLn(qPrintable(ctx));
        }
        promptInput();
        return;
    }

    if (trimmed == QStringLiteral("/help")) {
        rawPrintLn("命令:");
        rawPrintLn("  /quit    - 退出");
        rawPrintLn("  /switch  - 切换助手");
        rawPrintLn("  /jobs    - 查看活跃子Agent任务");
        rawPrintLn("  /help    - 显示帮助");
        promptInput();
        return;
    }

    // 发送用户消息
    m_waitingForResponse = true;
    m_streamStarted = false;
    m_chatService->sendUserMessage(m_currentSessionId, trimmed);
}

void InteractiveCli::promptInput()
{
    rawPrint("> ");
}

// ─── ChatService 信号处理 ───

void InteractiveCli::onStreamData(const QString& sessionId, const QString& data)
{
    if (sessionId != m_currentSessionId)
        return;

    if (!m_streamStarted) {
        m_streamStarted = true;
        rawPrint("\n");
    }
    rawPrint(qPrintable(data));
}

void InteractiveCli::onFinished(const QString& sessionId, const QString& content)
{
    if (sessionId != m_currentSessionId)
        return;

    if (!m_streamStarted) {
        rawPrint("\n");
        rawPrintLn(qPrintable(content));
    } else {
        rawPrint("\n\n");
    }

    m_waitingForResponse = false;
    m_chatService->saveSessionsToDisk(); // 每一轮结束都保存，方便其他进程同步
    promptInput();
}

void InteractiveCli::onError(const QString& sessionId, const QString& error)
{
    if (sessionId != m_currentSessionId)
        return;

    printErr(QStringLiteral("\n[错误] %1").arg(error));
    m_waitingForResponse = false;
    promptInput();
}

void InteractiveCli::onToolEvent(const QString& sessionId, const ToolExecutionEvent& event)
{
    if (sessionId != m_currentSessionId)
        return;

    QMutexLocker locker(&g_consoleMutex);
    if (event.status == QStringLiteral("started")) {
        const QString msg = QStringLiteral("[tool] %1 started").arg(event.toolName);
        std::fprintf(stderr, "%s\n", qPrintable(msg));
    } else if (event.status == QStringLiteral("completed")) {
        const QString msg = QStringLiteral("[tool] %1 %2")
                                .arg(event.toolName, event.success ? QStringLiteral("OK") : QStringLiteral("FAIL"));
        std::fprintf(stderr, "%s\n", qPrintable(msg));
    }
}

void InteractiveCli::print(const QString& msg)
{
    rawPrint(qPrintable(msg));
}

void InteractiveCli::printErr(const QString& msg)
{
    QMutexLocker locker(&g_consoleMutex);
    std::fprintf(stderr, "%s\n", qPrintable(msg));
}
