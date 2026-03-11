#ifndef CLIRUNNER_H
#define CLIRUNNER_H

#include "core/agent/ToolTypes.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

class LLMAgent;
class ToolDispatcher;
class ModelFactory;
class QTimer;

/**
 * @brief 无头 CLI 执行器
 *
 * 封装 LLMAgent 的初始化与执行流程，
 * 收集工具事件和最终回复，输出 JSON 到 stdout。
 */
class CliRunner : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString modelConfigPath; // 为空时使用默认用户配置 ~/.tmagent/config/models.yaml
        QString configId;        // 为空时使用 YAML 中的 default
        QString modelId;         // schema v2 下的真实模型 ID；为空时尝试从默认值推断
        QString systemPrompt;
        QString workspaceDir;    // 为空时使用当前目录
        QString task;            // 任务文本
        int timeoutMs = 300000;
        QStringList allowedTools;
        bool noTools = false;
        bool verbose = false;
        bool readStdin = false;
    };

    explicit CliRunner(const Options& opts, QObject* parent = nullptr);

    /// 启动执行（异步，完成后发 done 信号）
    void run();

    /// 退出码常量
    static constexpr int ExitSuccess = 0;
    static constexpr int ExitError = 1;
    static constexpr int ExitTimeout = 2;
    static constexpr int ExitBadArgs = 3;

signals:
    void done(int exitCode);

private slots:
    void onFinished(const QString& fullContent);
    void onError(const QString& errorMsg);
    void onToolEvent(const ToolExecutionEvent& event);
    void onTimeout();

private:
    bool initModelFactory();
    bool initToolDispatcher();
    bool initAgent();
    void outputJson(bool success, const QString& response, int exitCode);
    void log(const QString& msg) const;

    Options m_opts;
    LLMAgent* m_agent = nullptr;
    ToolDispatcher* m_dispatcher = nullptr;
    ModelFactory* m_factory = nullptr;
    QTimer* m_timer = nullptr;

    QJsonArray m_toolCalls;       // 收集的工具事件
    qint64 m_startTimeMs = 0;
};

#endif // CLIRUNNER_H
