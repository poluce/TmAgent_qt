#ifndef TMAGENT_TYPES_COMMONTYPES_H
#define TMAGENT_TYPES_COMMONTYPES_H

#include <QString>
#include <QJsonObject>

namespace TmAgent {

/**
 * @brief AgentConfig 结构 - Agent 配置
 * 
 * 包含 Agent 运行所需的配置信息，用于在插件和主应用之间传递配置。
 */
struct AgentConfig {
    QString uuid;                    // Agent 唯一标识
    QString userName;                // 用户名
    QString providerInstanceId;      // 接入点实例 ID
    QString selectedModelId;         // 选中的模型 ID
    QString configId;                // 配置 ID（兼容旧路径）
    QString systemPrompt;            // 系统提示词
    QString executionMode;           // 执行模式（如 "autopilot", "supervised"）
    QString workspaceDir;            // 工作目录
    int recursionDepth;              // 递归深度（用于委托控制）
    
    // 默认构造函数
    AgentConfig() : recursionDepth(0) {}
    
    /**
     * @brief 验证配置有效性
     * @return true 如果配置有效
     */
    bool isValid() const {
        return !uuid.trimmed().isEmpty() 
            && !providerInstanceId.trimmed().isEmpty()
            && !selectedModelId.trimmed().isEmpty();
    }
    
    /**
     * @brief 检查是否允许委托
     * @return true 如果递归深度允许委托
     */
    bool canDelegate() const {
        // 通常限制递归深度，例如最大深度为 3
        return recursionDepth < 3;
    }
};

/**
 * @brief TeammateConfig 结构 - 队友配置
 * 
 * 包含创建队友会话所需的配置信息。
 */
struct TeammateConfig {
    QString name;                    // 队友名称
    QString role;                    // 队友角色描述
    QString backend;                 // 后端类型（如 "codex", "tmagent"）
    QString persistence;             // 持久化策略："persistent" 或 "temporary"
    QString workingDirectory;        // 工作目录
    QString ownerAgentId;            // 所有者 Agent ID
    int turnIdleTimeoutMs;           // 回合空闲超时时间（毫秒）
    bool autoCleanup;                // 是否自动清理
    QString ephemeralOwnerTurnId;    // 临时队友的所有者回合 ID
    QJsonObject backendOverrides;    // 后端特定配置覆盖
    
    // 默认构造函数
    TeammateConfig()
        : persistence("temporary")
        , turnIdleTimeoutMs(300000)  // 默认 5 分钟
        , autoCleanup(true)
    {}
};

/**
 * @brief TeammateState 结构 - 队友状态
 * 
 * 表示队友的当前运行状态。
 */
struct TeammateState {
    QString id;                      // 队友 ID
    QString threadId;                // 会话线程 ID
    QString activeTurnId;            // 当前活动回合 ID
    QString status;                  // 状态："idle", "busy", "error", "shutdown"
    QString lastError;               // 最后错误信息
    int turnCount;                   // 回合计数
    qint64 createdAtMs;              // 创建时间戳（毫秒）
    qint64 lastActiveAtMs;           // 最后活动时间戳（毫秒）
    
    // 默认构造函数
    TeammateState()
        : status("idle")
        , turnCount(0)
        , createdAtMs(0)
        , lastActiveAtMs(0)
    {}
    
    /**
     * @brief 检查是否处于空闲状态
     * @return true 如果状态为 "idle"
     */
    bool isIdle() const {
        return status == "idle";
    }
    
    /**
     * @brief 检查是否处于忙碌状态
     * @return true 如果状态为 "busy"
     */
    bool isBusy() const {
        return status == "busy";
    }
    
    /**
     * @brief 检查是否有错误
     * @return true 如果状态为 "error"
     */
    bool hasError() const {
        return status == "error";
    }
};

} // namespace TmAgent

#endif // TMAGENT_TYPES_COMMONTYPES_H
