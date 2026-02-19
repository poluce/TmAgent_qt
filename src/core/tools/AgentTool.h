#ifndef AGENTTOOL_H
#define AGENTTOOL_H

#include "core/agent/LLMAgent.h"
#include "core/agent/ToolTypes.h"
#include <QObject>

class ToolDispatcher;

/**
 * @brief 子智能体工具 wrapper
 *
 * 将一个完整的 LLMAgent 封装为 ITool 接口。
 * 允许主 Agent 通过调用此工具将任务委派给子 Agent。
 *
 * 特性：
 * 1. 自动管理递归深度 (Recursion Depth)。
 * 2. 独立上下文 (不继承父 Agent 历史，只处理 Task)。
 * 3. 异步转同步执行 (使用 QEventLoop 等待子 Agent 完成)。
 */
class AgentTool : public QObject, public ITool {
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param parentConfig 父 Agent 的配置 (用于继承 BaseURL, Key 等)
     * @param toolName 工具名称 (如 "delegate_task")
     * @param toolDesc 工具描述
     */
    AgentTool(const LLMConfig& parentConfig, ToolDispatcher* toolDispatcher, const QString& toolName, const QString& toolDesc, QObject* parent = nullptr);

    /**
     * @brief 设置强制覆盖的配置 (用于异构模型，如 DeepSeek 调用 OpenAI)
     * @param config 子 Agent 的独立配置
     */
    void setOverrideConfig(const LLMConfig& config);

    ~AgentTool() override;

    // ITool 接口实现
    Tool getSchema() const override;
    ToolResult execute(const QJsonObject& args) override;

private:
    static constexpr int kDefaultDelegateTimeoutMs = 60000;
    static constexpr int kMinDelegateTimeoutMs = 2000;
    static constexpr int kMaxDelegateTimeoutMs = 120000;
    static constexpr int kDefaultMaxResponseChars = 4000;
    static constexpr int kMaxTaskChars = 20000;

    LLMConfig m_parentConfig;
    ToolDispatcher* m_toolDispatcher = nullptr;
    Tool m_schema;

    // 内部持有的子 Agent (按需创建或复用)
    LLMAgent* m_childAgent = nullptr;

    // 异构配置 (可选)
    bool m_useOverrideConfig = false;
    LLMConfig m_overrideConfig;
    QString m_progressAccumulator; // 用于积累进度输出
};

#endif // AGENTTOOL_H
