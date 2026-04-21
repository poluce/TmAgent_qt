#include "TmagentDelegateBackendAdapter.h"
#include "core/agent/delegate/TmagentDelegateBackend.h"
#include "core/agent/delegate/DelegateBackendSupport.h"
#include "core/agent/LLMAgent.h"
#include "core/agent/ToolDispatcher.h"
#include "llm/ModelFactory.h"
#include <tmagent/plugin/IToolExecutor.h>
#include <tmagent/plugin/IModelFactory.h>
#include <QCoreApplication>
#include <QPointer>

namespace {

/**
 * @brief 将 SDK 的 AgentConfig 转换为内部的 LLMConfig
 */
LLMConfig convertToLLMConfig(const TmAgent::AgentConfig& sdkConfig)
{
    LLMConfig config;
    config.uuid = sdkConfig.uuid;
    config.userName = sdkConfig.userName;
    config.providerInstanceId = sdkConfig.providerInstanceId;
    config.selectedModelId = sdkConfig.selectedModelId;
    config.configId = sdkConfig.configId;
    config.systemPrompt = sdkConfig.systemPrompt;
    config.executionMode = sdkConfig.executionMode;
    config.workspaceDir = sdkConfig.workspaceDir;
    config.recursionDepth = sdkConfig.recursionDepth;
    return config;
}

/**
 * @brief 工具执行器适配器，将 IToolExecutor 桥接到 ToolDispatcher
 */
class ToolExecutorAdapter {
public:
    explicit ToolExecutorAdapter(TmAgent::IToolExecutor* executor)
        : m_executor(executor)
    {}
    
    ToolDispatcher* getDispatcher() const {
        return ToolDispatcher::instance();
    }
    
private:
    TmAgent::IToolExecutor* m_executor;
};

/**
 * @brief 模型工厂适配器，将 IModelFactory 桥接到 ModelFactory
 */
class ModelFactoryAdapter {
public:
    explicit ModelFactoryAdapter(TmAgent::IModelFactory* factory)
        : m_factory(factory)
    {}
    
    ModelFactory* getFactory() const {
        return ModelFactory::instance();
    }
    
private:
    TmAgent::IModelFactory* m_factory;
};

/**
 * @brief 委托会话适配器，将内部会话包装为 SDK 接口
 */
class TmagentDelegateSessionAdapter : public TmAgent::IDelegateSession {
public:
    explicit TmagentDelegateSessionAdapter(
        std::unique_ptr<DelegateBackendInternal::IDelegateBackendSession> internalSession)
        : m_internalSession(std::move(internalSession))
    {}
    
    QString backendId() const override {
        return m_internalSession ? m_internalSession->backendId() : QString();
    }
    
    void start() override {
        if (m_internalSession)
            m_internalSession->start();
    }
    
    void cancel() override {
        if (m_internalSession)
            m_internalSession->cancel();
    }
    
private:
    std::unique_ptr<DelegateBackendInternal::IDelegateBackendSession> m_internalSession;
};

} // anonymous namespace

TmagentDelegateBackendAdapter::TmagentDelegateBackendAdapter(QObject* parent)
    : QObject(parent)
{
}

QString TmagentDelegateBackendAdapter::backendId() const
{
    return QStringLiteral("tmagent");
}

std::unique_ptr<TmAgent::IDelegateSession> TmagentDelegateBackendAdapter::createSession(
    const TmAgent::DelegateRequest& request,
    const TmAgent::DelegateCallbacks& callbacks,
    QString* error)
{
    // 转换 SDK 请求为内部请求
    DelegateBackendInternal::DelegateBackendStartRequest internalRequest;
    internalRequest.task = request.task;
    internalRequest.executionPrompt = request.executionPrompt;
    internalRequest.childConfig = convertToLLMConfig(request.childConfig);
    internalRequest.expectedTimeoutMs = request.expectedTimeoutMs;
    internalRequest.maxResponseChars = request.maxResponseChars;
    internalRequest.restrictDelegation = request.restrictDelegation;
    internalRequest.inheritedAllowedTools = request.inheritedAllowedTools;
    
    // 使用全局单例而不是接口指针
    internalRequest.toolDispatcher = ToolDispatcher::instance();
    internalRequest.modelFactory = ModelFactory::instance();
    
    // 转换回调函数
    DelegateBackendInternal::DelegateBackendCallbacks internalCallbacks;
    internalCallbacks.onActivity = callbacks.onActivity;
    internalCallbacks.onSummary = callbacks.onSummary;
    internalCallbacks.onToolEvent = callbacks.onToolEvent;
    internalCallbacks.onStreamDelta = callbacks.onStreamDelta;
    internalCallbacks.onSuccess = callbacks.onSuccess;
    internalCallbacks.onFailure = callbacks.onFailure;
    
    // 创建内部会话
    DelegateBackendInternal::TmagentDelegateBackend backend;
    auto internalSession = backend.createSession(internalRequest, internalCallbacks, error);
    
    if (!internalSession)
        return nullptr;
    
    // 包装为 SDK 会话
    return std::make_unique<TmagentDelegateSessionAdapter>(std::move(internalSession));
}
