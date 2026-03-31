#ifndef TMAGENT_IDELEGATEBACKEND_H
#define TMAGENT_IDELEGATEBACKEND_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <functional>
#include <memory>
#include "../types/CommonTypes.h"

namespace TmAgent {

class IToolExecutor;
class IModelFactory;
struct ToolExecutionEvent;

/**
 * @brief 委托请求结构
 * 
 * 包含创建委托会话所需的所有信息。
 */
struct DelegateRequest {
    QString task;                           ///< 任务描述
    QString executionPrompt;                ///< 执行提示词
    AgentConfig childConfig;                ///< 子 Agent 配置
    IToolExecutor* toolExecutor;            ///< 工具执行器接口（用于工具调用）
    IModelFactory* modelFactory;            ///< 模型工厂接口（用于创建 LLM Provider）
    int expectedTimeoutMs;                  ///< 预期超时时间（毫秒）
    int maxResponseChars;                   ///< 最大响应字符数
    bool restrictDelegation;                ///< 是否限制递归委托
    QStringList inheritedAllowedTools;      ///< 继承的工具权限列表
    
    DelegateRequest()
        : toolExecutor(nullptr)
        , modelFactory(nullptr)
        , expectedTimeoutMs(300000)
        , maxResponseChars(100000)
        , restrictDelegation(false)
    {}
};

/**
 * @brief 委托回调函数集合
 * 
 * 用于监听委托会话的执行过程。
 */
struct DelegateCallbacks {
    std::function<void()> onActivity;                           ///< 活动事件回调
    std::function<void(const QString&)> onSummary;              ///< 摘要信息回调
    std::function<void(const ToolExecutionEvent&)> onToolEvent; ///< 工具执行事件回调
    std::function<void(const QString&)> onStreamDelta;          ///< 流式输出增量回调
    std::function<void(const QString&)> onSuccess;              ///< 成功完成回调
    std::function<void(const QString&)> onFailure;              ///< 失败回调
};

/**
 * @brief 委托会话接口
 * 
 * 表示一个正在进行的委托任务会话。
 */
class IDelegateSession {
public:
    virtual ~IDelegateSession() = default;
    
    /**
     * @brief 返回后端标识
     * @return QString 后端 ID
     */
    virtual QString backendId() const = 0;
    
    /**
     * @brief 启动会话执行
     */
    virtual void start() = 0;
    
    /**
     * @brief 取消会话执行
     */
    virtual void cancel() = 0;
};

/**
 * @brief 委托后端接口
 * 
 * 用于子任务委托场景，支持创建临时的子 Agent 会话。
 */
class IDelegateBackend {
public:
    virtual ~IDelegateBackend() = default;
    
    /**
     * @brief 返回后端标识
     * @return QString 后端 ID
     */
    virtual QString backendId() const = 0;
    
    /**
     * @brief 创建委托会话
     * @param request 委托请求信息
     * @param callbacks 回调函数集合
     * @param error 错误信息输出参数（可选）
     * @return std::unique_ptr<IDelegateSession> 会话实例，失败返回 nullptr
     */
    virtual std::unique_ptr<IDelegateSession> createSession(
        const DelegateRequest& request,
        const DelegateCallbacks& callbacks,
        QString* error = nullptr) = 0;
};

} // namespace TmAgent

#endif // TMAGENT_IDELEGATEBACKEND_H
