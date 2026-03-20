#ifndef RUNTIMEMANAGER_H
#define RUNTIMEMANAGER_H

#include "core/agent/ToolTypes.h"
#include "llm/LLMTypes.h"
#include <QHash>
#include <QObject>
#include <QString>

class AgentRuntime;
class Identity;
class ModelFactory;
class ToolDispatcher;
class SessionManager;
class ChatPersistenceService;

/**
 * @brief AgentRuntime 生命周期管理器
 *
 * 负责 AgentRuntime 的创建、缓存、配置组装和释放。
 * 从 ApplicationServices 提取，使 ApplicationServices 专注于消息编排。
 */
class RuntimeManager : public QObject {
    Q_OBJECT
public:
    explicit RuntimeManager(QObject* parent = nullptr);
    ~RuntimeManager() override;

    // ---- 依赖注入 ----
    void setModelFactory(ModelFactory* factory);
    void setToolDispatcher(ToolDispatcher* dispatcher);
    void setSessionManager(SessionManager* manager);
    void setPersistence(ChatPersistenceService* persistence);

    // ---- Runtime 获取/创建 ----
    AgentRuntime* runtimeForAgent(const QString& agentIdentityId) const;
    AgentRuntime* ensureRuntimeForAgent(Identity* agentIdentity);
    void releaseRuntimeIfUnused(const QString& agentIdentityId);

    // ---- 配置 ----
    void setDefaultAgentConfig(const LLMConfig& config);
    LLMConfig defaultAgentConfig() const;
    LLMConfig composeConfigForIdentity(Identity* identity) const;
    void applyConfigToAllRuntimes();
    void applyToolDispatcherToAllRuntimes();

    // ---- 遍历 ----
    QHash<QString, AgentRuntime*>& runtimes();
    const QHash<QString, AgentRuntime*>& runtimes() const;

signals:
    /**
     * @brief 新 Runtime 创建后发射，供 ApplicationServices 连接信号
     */
    void runtimeCreated(AgentRuntime* runtime);

private:
    ModelFactory* m_modelFactory = nullptr;
    ToolDispatcher* m_toolDispatcher = nullptr;
    SessionManager* m_sessionManager = nullptr;
    ChatPersistenceService* m_persistence = nullptr;

    QHash<QString, AgentRuntime*> m_runtimes; // agentIdentityId -> AgentRuntime*
    LLMConfig m_defaultAgentConfig;
};

#endif // RUNTIMEMANAGER_H
