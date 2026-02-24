#ifndef LLMTYPES_H
#define LLMTYPES_H

#include "core/utils/DefaultPrompts.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// -----------------------------------------------------------------------------
// 能力标签
// -----------------------------------------------------------------------------
namespace Capability {
inline const QString TextGeneration = QStringLiteral("text_generation");
inline const QString StructuredOutput = QStringLiteral("structured_output");
inline const QString CodeGeneration = QStringLiteral("code_generation");
inline const QString ToolCalling = QStringLiteral("tool_calling");
inline const QString Multimodal = QStringLiteral("multimodal");
} // namespace Capability

// -----------------------------------------------------------------------------
// 错误码
// -----------------------------------------------------------------------------
namespace LLMErrorCode {
inline const QString Timeout = QStringLiteral("timeout");
inline const QString RateLimited = QStringLiteral("rate_limited");
inline const QString AuthFailed = QStringLiteral("auth_failed");
inline const QString ProtocolError = QStringLiteral("protocol_error");
inline const QString ModelRefusal = QStringLiteral("model_refusal");
inline const QString ToolFailed = QStringLiteral("tool_failed");
inline const QString Unknown = QStringLiteral("unknown");
} // namespace LLMErrorCode

// -----------------------------------------------------------------------------
// CapabilityDescriptor：运行时描述模型能力
// -----------------------------------------------------------------------------
struct CapabilityDescriptor {
    QString modelId;
    QStringList capabilities; // 能力标签：text_generation, tool_calling 等
    int contextLength = 0;
    bool toolCalling = false;
    QString codeQuality; // 如 "high" / "medium" / "low" 或空
    QString costLevel;   // 如 "low" / "medium" / "high"
    QString latency;     // 如 "low" / "medium" / "high"
    bool isDefault = false;

    bool supports(const QString& capability) const
    {
        return capabilities.contains(capability);
    }
};

// -----------------------------------------------------------------------------
// ModelConfig：模型配置信息
// -----------------------------------------------------------------------------
struct ModelConfig {
    // 配置标识
    QString configId;       // 配置条目唯一标识（如 "anthropic-claude-relay"）
    bool enabled = true;    // 是否启用

    // 基本信息
    QString modelId;
    QString displayName;
    QString provider;

    // 认证信息
    QString apiKey;
    QString baseUrl;
    QString authType;

    // 模型参数
    double temperature = 0.7;
    int maxTokens = 4096;
    int timeoutMs = 180000;

    // 能力描述
    QStringList capabilities;
    bool toolCalling = false;
    int contextLength = 0;

    // 系统提示词
    QString systemPrompt;

    // 扩展配置
    QJsonObject extraConfig;

    bool isValid() const
    {
        return !configId.isEmpty() && !modelId.isEmpty() && !apiKey.isEmpty() && !baseUrl.isEmpty();
    }

    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj["configId"] = configId;
        obj["enabled"] = enabled;
        obj["modelId"] = modelId;
        obj["displayName"] = displayName;
        obj["provider"] = provider;
        obj["apiKey"] = apiKey;
        obj["baseUrl"] = baseUrl;
        obj["authType"] = authType;
        obj["temperature"] = temperature;
        obj["maxTokens"] = maxTokens;
        obj["timeoutMs"] = timeoutMs;
        obj["toolCalling"] = toolCalling;
        obj["contextLength"] = contextLength;
        obj["systemPrompt"] = systemPrompt;
        QJsonArray caps;
        for (const QString& cap : capabilities)
            caps.append(cap);
        obj["capabilities"] = caps;
        if (!extraConfig.isEmpty())
            obj["extraConfig"] = extraConfig;
        return obj;
    }

    static ModelConfig fromJson(const QJsonObject& obj)
    {
        ModelConfig config;
        config.configId = obj["configId"].toString();
        config.enabled = obj["enabled"].toBool(true);
        config.modelId = obj["modelId"].toString();
        config.displayName = obj["displayName"].toString();
        config.provider = obj["provider"].toString();
        config.apiKey = obj["apiKey"].toString();
        config.baseUrl = obj["baseUrl"].toString();
        config.authType = obj["authType"].toString("Bearer");
        config.temperature = obj["temperature"].toDouble(0.7);
        config.maxTokens = obj["maxTokens"].toInt(4096);
        config.timeoutMs = obj["timeoutMs"].toInt(180000);
        config.toolCalling = obj["toolCalling"].toBool(false);
        config.contextLength = obj["contextLength"].toInt(0);
        config.systemPrompt = obj["systemPrompt"].toString();
        for (const QJsonValue& cap : obj["capabilities"].toArray())
            config.capabilities.append(cap.toString());
        if (obj.contains("extraConfig"))
            config.extraConfig = obj["extraConfig"].toObject();
        return config;
    }
};

// -----------------------------------------------------------------------------
// LLM 用量信息
// -----------------------------------------------------------------------------
struct LLMUsage {
    int promptTokens = 0;
    int completionTokens = 0;
    int totalTokens = 0;
};

// -----------------------------------------------------------------------------
// LLM 错误信息（Provider 统一产出 error_code + user_message）
// -----------------------------------------------------------------------------
struct LLMError {
    QString errorCode;
    QString userMessage;
    QJsonObject diagnostics;
};

// -----------------------------------------------------------------------------
// LLMRequest：Provider 接收的已组装请求
// -----------------------------------------------------------------------------
struct LLMRequest {
    QString requestId;
    QString traceId;
    QString modelId;
    QStringList capabilities;
    bool stream = false;
    QJsonArray messages;
    QJsonArray tools;
    int timeoutMs = 180000;
    double temperature = 0.7;
    int maxTokens = 4096;
};

// -----------------------------------------------------------------------------
// LLMResponse：非流式调用的标准化结果
// -----------------------------------------------------------------------------
struct LLMResponse {
    QString result;
    LLMUsage usage;
    LLMError error;
    QJsonArray toolCalls;
    QString finishReason;

    bool hasError() const { return !error.errorCode.isEmpty(); }
};

// -----------------------------------------------------------------------------
// ModelRouter 输入
// -----------------------------------------------------------------------------
struct RouterRequest {
    QString taskType;
    QStringList requiredCapabilities;
    QString costPreference;
    int maxLatencyMs = -1;
    QString preferredModelId;
};

// -----------------------------------------------------------------------------
// ModelRouter 输出
// -----------------------------------------------------------------------------
struct RouterResult {
    QString modelId;
    QString decisionReason;
    QStringList fallbackChain;
    bool success = false;
};

// -----------------------------------------------------------------------------
// LLMConfig：Agent 角色配置（从 ToolTypes.h 迁移至此）
// -----------------------------------------------------------------------------
struct LLMConfig {
    // === Agent 标识 ===
    QString uuid;     // 唯一代号 (UUID)
    QString userName; // 显示名称 (如 "代码专家")

    // === 模型与角色 ===
    QString configId; // 统一使用 configId 查找 ModelFactory
    QString systemPrompt = DefaultPrompts::codingAssistantSystemPrompt();
    QString workspaceDir; // Agent 独立工作空间（默认由 ChatService 注入）

    // === 递归控制 ===
    // 3 = 主 Agent (可以委派给 Depth 2)
    // 2 = 子 Agent (可以委派给 Depth 1)
    // ...
    // 0 = 叶子 Agent (禁止委派)
    int recursionDepth = 3;

    // === 辅助方法 ===
    bool isValid() const
    {
        return !configId.trimmed().isEmpty();
    }
    bool canDelegate() const { return recursionDepth > 0; } // 深度大于0才允许委派
};

#endif // LLMTYPES_H
