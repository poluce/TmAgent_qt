#ifndef TMAGENT_ITOOLEXECUTOR_H
#define TMAGENT_ITOOLEXECUTOR_H

#include <QtCore/QString>
#include <functional>
#include "../types/ToolTypes.h"

namespace TmAgent {

/**
 * @brief 工具执行器接口
 * 
 * 提供给后端插件用于执行工具调用的回调接口。
 * 后端插件通过此接口调用主应用的工具，而无需直接依赖 ToolDispatcher。
 */
class IToolExecutor {
public:
    virtual ~IToolExecutor() = default;
    
    /**
     * @brief 同步执行工具调用
     * 
     * 阻塞直到工具执行完成。
     * 
     * @param call 工具调用请求
     * @return ToolResult 工具执行结果
     */
    virtual ToolResult executeToolSync(const ToolCall& call) = 0;
    
    /**
     * @brief 异步执行工具调用
     * 
     * 立即返回，工具执行完成后通过回调函数通知。
     * 
     * @param call 工具调用请求
     * @param callback 完成回调函数，接收 ToolResult 参数
     */
    virtual void executeToolAsync(const ToolCall& call,
                                  std::function<void(const ToolResult&)> callback) = 0;
};

} // namespace TmAgent

#endif // TMAGENT_ITOOLEXECUTOR_H
