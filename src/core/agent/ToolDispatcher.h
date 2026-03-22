#ifndef TOOLDISPATCHER_H
#define TOOLDISPATCHER_H

#include "IToolPluginHost.h"
#include "IToolProvider.h"
#include "ToolTypes.h"
#include "llm/LLMTypes.h"
#include <QList>
#include <QMap>
#include <QObject>

/**
 * @brief 工具调度器 - 聚合工具 provider 并分发工具调用
 *
 * 职责:
 *   - 管理已注册的工具 provider（工具插件 / MCP）
 *   - 聚合所有可见工具 schema
 *   - 将工具调用分发到对应 provider
 *   - 作为宿主回调，为首方工具插件执行内建工具逻辑
 */
class ToolDispatcher : public QObject, public IToolPluginHost {
    Q_OBJECT
public:
    static ToolDispatcher* instance();

    /**
     * @brief 注册工具 Provider
     * @param provider 工具提供者实例
     * @param name Provider 名称（用于冲突提示）
     */
    void registerProvider(IToolProvider* provider, const QString& name);

    /**
     * @brief 重新索引指定 Provider 的工具
     * @param name Provider 名称
     */
    void refreshProvider(const QString& name);

    /**
     * @brief 预加载工具 schema 缓存
     */
    void registerDefaultTools();

    /**
     * @brief 获取所有已注册工具的 Schema 定义
     * @return 工具列表，用于注册到 LLMAgent
     */
    QList<Tool> getAllToolSchemas() const;
    QStringList toolNamesForProvider(const QString& providerName) const;

    /**
     * @brief 查询指定工具名是否已在 dispatcher 中注册
     */
    bool hasToolSchema(const QString& name) const;

    /**
     * @param call 工具调用请求
     * @return 执行结果字符串
     */
    ToolResult dispatch(const ToolCall& call);

    /**
     * @brief 注册 Agent 委派工具 (如 delegate_to_coder)
     * @param config 当前 Agent 的配置 (用于判断深度和传递给子 Agent)
     */
    void registerAgentTools(const LLMConfig& config);
    void clearProviders();
    void setDefaultAgentConfig(const LLMConfig& config);

    Tool resolveToolSchema(const QString& toolName,
                           const QString& fallbackDescription) const override;
    ToolResult executeHostedTool(const QString& toolName,
                                 const QString& fallbackDescription,
                                 const QJsonObject& args) override;

signals:
    /// 工具开始执行 (description: 操作描述, params: 参数JSON)
    void toolStarted(const QString& description, const QString& params);

private:
    explicit ToolDispatcher(QObject* parent = nullptr);
    ~ToolDispatcher() override;
    ToolDispatcher(const ToolDispatcher&) = delete;
    ToolDispatcher& operator=(const ToolDispatcher&) = delete;

    void indexProviderTools(IToolProvider* provider, const QString& providerName);
    void indexToolSchema(const Tool& tool, IToolProvider* provider, const QString& providerName);

    QMap<QString, IToolProvider*> m_providers; // provider 名称 -> provider
    QMap<QString, IToolProvider*> m_toolIndex; // 工具名 -> provider
    QMap<QString, Tool> m_toolSchemas;         // 工具名 -> schema
    QMap<QString, QString> m_toolOwners;       // 工具名 -> provider 名称
    bool m_defaultToolsRegistered = false;
    LLMConfig m_defaultAgentConfig;
};

#endif // TOOLDISPATCHER_H
