#ifndef TOOLDISPATCHER_H
#define TOOLDISPATCHER_H

#include "IToolProvider.h"
#include "ToolTypes.h"
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <memory>

class LocalToolProvider;

/**
 * @brief 工具调度器 - 负责分发和执行工具调用
 *
 * 职责:
 *   - 管理工具注册表
 *   - 分发工具调用到对应的执行函数
 *   - 提供所有已注册工具的 Schema
 *
 * 使用方式:
 *   ToolDispatcher* dispatcher = ToolDispatcher::instance();
 *   dispatcher->registerTool(FileTool::getCreateFileSchema(), "创建文件", FileTool::executeCreateFile);
 *   dispatcher->dispatch(call);
 */
class ToolDispatcher : public QObject {
    Q_OBJECT
public:
    static ToolDispatcher* instance();

    /**
     * @brief 注册工具
     * @param schema 工具 Schema 定义
     * @param description 中文描述（用于 UI 显示）
     * @param executor 执行函数
     */
    void registerTool(ITool* tool, const QString& description);

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
     * @brief 注册默认工具集（FileTool、ShellTool）
     */
    void registerDefaultTools();

    /**
     * @brief 获取所有已注册工具的 Schema 定义
     * @return 工具列表，用于注册到 LLMAgent
     */
    QList<Tool> getAllToolSchemas() const;

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

    std::unique_ptr<LocalToolProvider> m_localProvider;
    bool m_defaultToolsRegistered = false;
};

#endif // TOOLDISPATCHER_H
