#include "ToolExecutorAdapter.h"
#include "ToolDispatcher.h"
#include <QTimer>

ToolExecutorAdapter::ToolExecutorAdapter(ToolDispatcher* dispatcher, QObject* parent)
    : QObject(parent)
    , m_dispatcher(dispatcher)
{
    Q_ASSERT(dispatcher != nullptr);
}

ToolExecutorAdapter::~ToolExecutorAdapter() = default;

TmAgent::ToolResult ToolExecutorAdapter::executeToolSync(const TmAgent::ToolCall& call)
{
    if (!m_dispatcher) {
        TmAgent::ToolResult error;
        error.rawContent = "错误：ToolDispatcher 未初始化";
        error.userSummary = "工具调度器不可用";
        error.success = false;
        error.data = QJsonObject{{"errorCode", "dispatcher_unavailable"}};
        return error;
    }
    
    // 直接调用 ToolDispatcher 的 dispatch 方法
    // ToolDispatcher::dispatch 返回 ToolResult，与 SDK 的 ToolResult 兼容
    return m_dispatcher->dispatch(call);
}

void ToolExecutorAdapter::executeToolAsync(const TmAgent::ToolCall& call,
                                          std::function<void(const TmAgent::ToolResult&)> callback)
{
    if (!m_dispatcher) {
        TmAgent::ToolResult error;
        error.rawContent = "错误：ToolDispatcher 未初始化";
        error.userSummary = "工具调度器不可用";
        error.success = false;
        error.data = QJsonObject{{"errorCode", "dispatcher_unavailable"}};
        
        // 异步回调错误结果
        QTimer::singleShot(0, this, [callback, error]() {
            callback(error);
        });
        return;
    }
    
    // 在单独的线程中执行工具调用，避免阻塞
    // 使用 QTimer::singleShot 将执行推迟到事件循环
    QTimer::singleShot(0, this, [this, call, callback]() {
        TmAgent::ToolResult result = m_dispatcher->dispatch(call);
        callback(result);
    });
}
