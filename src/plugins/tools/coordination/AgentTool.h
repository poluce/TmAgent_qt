#ifndef AGENTTOOL_H
#define AGENTTOOL_H

#include <tmagent/types/ToolTypes.h>
#include "llm/LLMTypes.h"
#include <QObject>

namespace TmAgent {
class IToolPluginHost;
}

class ToolDispatcher;

/**
 * @brief Team 协作工具 wrapper
 *
 * 对外统一暴露 teammate 团队协作工具族：
 * - create_teammate
 * - message_teammate
 * - list_teammates
 * - cancel_teammate_turn
 * - remove_teammate
 * - rename_teammate
 * - get_teammate_status
 * - message_between_teammates
 */
class AgentTool : public QObject, public ITool {
    Q_OBJECT
public:
    static TmAgent::Tool buildSchema(const QString& toolName, const QString& toolDesc = QString());
    static QList<TmAgent::Tool> toolSchemas();

    /**
     * @brief 构造函数
     * @param host 宿主回调接口（用于查询可用后端）
     * @param parentConfig 父 Agent 的配置
     * @param toolDispatcher 工具调度器
     * @param toolName 工具名称（如 "create_teammate"）
     * @param toolDesc 工具描述
     */
    AgentTool(TmAgent::IToolPluginHost* host, const LLMConfig& parentConfig, ToolDispatcher* toolDispatcher, const QString& toolName, const QString& toolDesc, QObject* parent = nullptr);

    /**
     * @brief 设置强制覆盖的配置 (用于异构模型，如 DeepSeek 调用 OpenAI)
     * @param config 子 Agent 的独立配置
     */
    void setOverrideConfig(const LLMConfig& config);

    ~AgentTool() override;

    // ITool 接口实现
    TmAgent::Tool getSchema() const override;
    TmAgent::ToolResult execute(const QJsonObject& args) override;

private:
    static constexpr int kDefaultWaitTimeoutMs = 120000;
    static constexpr int kMinWaitTimeoutMs = 1000;
    static constexpr int kMaxWaitTimeoutMs = 600000;

    TmAgent::IToolPluginHost* m_host = nullptr;
    LLMConfig m_parentConfig;
    ToolDispatcher* m_toolDispatcher = nullptr;
    TmAgent::Tool m_schema;

    // 异构配置 (可选)
    bool m_useOverrideConfig = false;
    LLMConfig m_overrideConfig;
};

#endif // AGENTTOOL_H
