#ifndef TMAGENT_ITOOLPROVIDER_H
#define TMAGENT_ITOOLPROVIDER_H

#include <tmagent/types/ToolTypes.h>
#include <QList>

namespace TmAgent {

/**
 * @brief 工具提供者接口
 * 
 * 工具提供者负责实现具体的工具逻辑。每个工具插件通过 createProvider()
 * 创建一个提供者实例，该实例包含一组相关的工具。
 * 
 * 提供者必须继承 QObject 以支持 Qt 的信号槽机制（用于异步工具）。
 * 
 * 使用示例：
 * @code
 * class MyToolProvider : public QObject, public TmAgent::IToolProvider {
 *     Q_OBJECT
 * public:
 *     QList<Tool> listTools() const override {
 *         return m_tools;
 *     }
 *     
 *     ToolResult execute(const ToolCall& call) override {
 *         if (call.name == "my_tool") {
 *             // 执行工具逻辑
 *             return ToolResult("result", "summary", true);
 *         }
 *         return ToolResult("Unknown tool", "Error", false);
 *     }
 * };
 * @endcode
 */
class IToolProvider {
public:
    virtual ~IToolProvider() = default;
    
    /**
     * @brief 列出所有可用工具
     * 
     * 返回此提供者支持的所有工具定义。主应用会调用此方法来获取
     * 工具列表，并将其注册到工具调度器中。
     * 
     * @return QList<Tool> 工具定义列表，每个工具包含名称、描述和参数 Schema
     * 
     * @note 工具名称必须全局唯一
     * @note 工具的 inputSchema 应使用 JSON Schema Draft 7 格式
     * @note 此方法可能被多次调用，应返回一致的结果
     * 
     * @see Tool
     * @see ToolSchemaBuilder
     */
    virtual QList<Tool> listTools() const = 0;
    
    /**
     * @brief 执行工具调用
     * 
     * 当 AI Agent 需要调用工具时，主应用会调用此方法。
     * 实现应该：
     * 1. 验证工具名称和参数
     * 2. 执行工具逻辑
     * 3. 返回执行结果
     * 
     * 对于同步工具，直接返回最终结果。
     * 对于异步工具，返回延迟标记（rawContent 以 "__DEFERRED__" 开头），
     * 并在完成后发出 toolCompleted 信号。
     * 
     * @param call 工具调用请求，包含工具名称、调用 ID 和输入参数
     * @return ToolResult 工具执行结果
     * 
     * @note 此方法不应抛出异常，所有错误应通过 ToolResult{success=false} 返回
     * @note 对于异步工具，应在后台线程执行耗时操作，避免阻塞主线程
     * @note 工具执行时间应尽可能短，长时间操作应使用异步模式
     * 
     * @see ToolCall
     * @see ToolResult
     * @see isDeferredToolResult()
     */
    virtual ToolResult execute(const ToolCall& call) = 0;
    
    // 注意：异步工具完成信号应在派生类中定义：
    // signals:
    //     void toolCompleted(const QString& callId, const ToolResult& result);
};

} // namespace TmAgent

#endif // TMAGENT_ITOOLPROVIDER_H
