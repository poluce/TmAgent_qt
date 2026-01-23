#ifndef TOOLDISPATCHER_H
#define TOOLDISPATCHER_H

#include <QObject>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <functional>
#include "ToolTypes.h"

/**
 * @brief 工具注册条目
 */
struct ToolEntry {
    ITool* toolImpl;            // 工具实现接口（生命周期由外部或 Dispatcher 管理）
    QString description;        // 中文描述
};

/**
 * @brief 工具调度器 - 负责分发和执行工具调用
 * 
 * 职责:
 *   - 管理工具注册表
 *   - 分发工具调用到对应的执行函数
 *   - 提供所有已注册工具的 Schema
 * 
 * 使用方式:
 *   ToolDispatcher dispatcher;
 *   dispatcher.registerTool(FileTool::getCreateFileSchema(), "创建文件", FileTool::executeCreateFile);
 *   dispatcher.dispatch(call);
 */
class ToolDispatcher : public QObject {
    Q_OBJECT
public:
    explicit ToolDispatcher(QObject *parent = nullptr);
    
    /**
     * @brief 注册工具
     * @param schema 工具 Schema 定义
     * @param description 中文描述（用于 UI 显示）
     * @param executor 执行函数
     */
    void registerTool(ITool* tool, const QString& description);
    
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
     * @brief 分发工具调用
     * @param call 工具调用请求
     * @return 执行结果字符串
     */
    ToolResult dispatch(const ToolCall& call);

signals:
    /// 工具开始执行 (description: 操作描述, params: 参数JSON)
    void toolStarted(const QString& description, const QString& params);

private:
    QMap<QString, ToolEntry> m_registry;  // 工具名 -> 注册条目
};

#endif // TOOLDISPATCHER_H
