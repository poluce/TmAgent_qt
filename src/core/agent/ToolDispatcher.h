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
 *   - 向首方插件暴露必要的宿主查询能力
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
     * @brief 预留的团队协作工具刷新钩子
     * @param config 当前 Agent 的配置
     *
     * 当前 teammate 工具 schema 由工具插件直接提供，因此此处保持为 no-op。
     */
    void registerAgentTools(const LLMConfig& config);
    void clearProviders();
    void setDefaultAgentConfig(const LLMConfig& config);

    QStringList availableTeammateBackendIds() const override;

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
    LLMConfig m_defaultAgentConfig;
};

#endif // TOOLDISPATCHER_H
