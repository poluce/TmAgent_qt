#ifndef LLMTYPES_H
#define LLMTYPES_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

/**
 * @file LLMTypes.h
 * @brief 多模型支持体系中的请求/响应、能力描述、路由等数据类型
 *
 * 与《Agent 多模型支持设计》中的 LLMRequest、LLMResponse、CapabilityDescriptor、
 * 以及 ModelRouter 的输入输出保持一致。
 */

// -----------------------------------------------------------------------------
// 能力标签（设计文档 5.1）
// -----------------------------------------------------------------------------
namespace Capability {
inline const QString TextGeneration = QStringLiteral("text_generation");
inline const QString StructuredOutput = QStringLiteral("structured_output");
inline const QString CodeGeneration = QStringLiteral("code_generation");
inline const QString ToolCalling = QStringLiteral("tool_calling");
inline const QString Multimodal = QStringLiteral("multimodal");
} // namespace Capability

// -----------------------------------------------------------------------------
// 错误码（设计文档 9.6.5 Error Taxonomy）
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
// CapabilityDescriptor：运行时描述模型能力（设计文档 6.6）
// -----------------------------------------------------------------------------
struct CapabilityDescriptor {
    QString modelId;
    QStringList capabilities;  // 能力标签：text_generation, tool_calling 等
    int contextLength = 0;
    bool toolCalling = false;
    QString codeQuality;   // 如 "high" / "medium" / "low" 或空
    QString costLevel;     // 如 "low" / "medium" / "high"
    QString latency;       // 如 "low" / "medium" / "high"
    bool isDefault = false;

    bool supports(const QString& capability) const {
        return capabilities.contains(capability);
    }
};

// -----------------------------------------------------------------------------
// ModelConfig：模型配置信息（集中管理）
// -----------------------------------------------------------------------------
/**
 * @brief 模型配置结构，由 ModelFactory 统一管理
 * 
 * 包含模型的所有配置信息：认证、端点、参数等。
 * Agent 不再需要知道这些配置细节，只需指定 modelId 即可。
 */
struct ModelConfig {
    // 基本信息
    QString modelId;           // 模型 ID（如 "deepseek-chat", "gpt-4o"）
    QString displayName;       // 显示名称（如 "DeepSeek Chat"）
    QString provider;          // 提供商（如 "openai", "deepseek", "anthropic"）
    
    // 认证信息
    QString apiKey;            // API 密钥
    QString baseUrl;           // API 基础 URL（如 "https://api.deepseek.com"）
    QString authType;          // 认证类型（"Bearer", "X-API-Key" 等）
    
    // 模型参数（默认值）
    double temperature = 0.7;
    int maxTokens = 4096;
    int timeoutMs = 180000;    // 默认 3 分钟
    
    // 能力描述
    QStringList capabilities;  // 支持的能力标签
    bool toolCalling = false;
    int contextLength = 0;
    
    // 系统提示词
    QString systemPrompt;      // 每个模型可以有独立的系统提示词
    
    // 扩展配置
    QJsonObject extraConfig;   // 提供商特定配置（如自定义 header）
    
    /**
     * @brief 验证配置是否有效
     */
    bool isValid() const {
        return !modelId.isEmpty() && !apiKey.isEmpty() && !baseUrl.isEmpty();
    }
    
    /**
     * @brief 转换为 JSON 对象（用于序列化）
     */
    QJsonObject toJson() const {
        QJsonObject obj;
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
        for (const QString& cap : capabilities) {
            caps.append(cap);
        }
        obj["capabilities"] = caps;
        
        if (!extraConfig.isEmpty()) {
            obj["extraConfig"] = extraConfig;
        }
        
        return obj;
    }
    
    /**
     * @brief 从 JSON 对象加载配置
     */
    static ModelConfig fromJson(const QJsonObject& obj) {
        ModelConfig config;
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
        
        QJsonArray caps = obj["capabilities"].toArray();
        for (const QJsonValue& cap : caps) {
            config.capabilities.append(cap.toString());
        }
        
        if (obj.contains("extraConfig")) {
            config.extraConfig = obj["extraConfig"].toObject();
        }
        
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
    QJsonObject diagnostics;  // Adapter 提供的诊断信息（HTTP 状态、SSE 等）
};

// -----------------------------------------------------------------------------
// LLMRequest：Provider 接收的已组装请求（设计文档 6.3）
// -----------------------------------------------------------------------------
struct LLMRequest {
    QString requestId;
    QString traceId;
    QString modelId;
    QStringList capabilities;  // 本任务需要的能力标签
    bool stream = false;
    QJsonArray messages;       // 由上层/HistoryManager 完成拼接
    QJsonArray tools;          // 本次请求可用工具（OpenAI 兼容 function 数组）
    int timeoutMs = 180000;
    double temperature = 0.7;
    int maxTokens = 4096;
};

// -----------------------------------------------------------------------------
// LLMResponse：非流式调用的标准化结果（设计文档 6.3）
// -----------------------------------------------------------------------------
struct LLMResponse {
    QString result;            // 完整文本结果
    LLMUsage usage;
    LLMError error;            // 若失败则填充，result 可为空
    QJsonArray toolCalls;      // 若存在工具调用则填充
    QString finishReason;      // "stop" / "tool_calls" / "length" 等

    bool hasError() const { return !error.errorCode.isEmpty(); }
};

// -----------------------------------------------------------------------------
// ModelRouter 输入（设计文档 6.2）
// -----------------------------------------------------------------------------
struct RouterRequest {
    QString taskType;          // 任务类型，供策略使用
    QStringList requiredCapabilities;
    QString costPreference;    // "minimize" / "balance" / "quality"
    int maxLatencyMs = -1;     // -1 表示不限制
    QString preferredModelId;  // 可选偏好
};

// -----------------------------------------------------------------------------
// ModelRouter 输出（设计文档 6.2）
// -----------------------------------------------------------------------------
struct RouterResult {
    QString modelId;
    QString decisionReason;
    QStringList fallbackChain; // 主选失败时的备用 model_id 顺序
    bool success = false;
};

#endif // LLMTYPES_H
