#ifndef CONFIGADAPTER_H
#define CONFIGADAPTER_H

#include "CommonTypes.h"
#include "llm/LLMTypes.h"

/**
 * @brief ConfigAdapter - 配置转换适配器
 * 
 * 提供 LLMConfig 到 SDK AgentConfig 的转换功能。
 * 用于将主应用的内部配置格式转换为插件可以使用的 SDK 格式。
 */
class ConfigAdapter {
public:
    /**
     * @brief 将 LLMConfig 转换为 SDK 的 AgentConfig
     * 
     * 映射所有必需字段，确保插件能够获取完整的 Agent 配置信息。
     * 
     * @param llmConfig 主应用的 LLMConfig 配置
     * @return TmAgent::AgentConfig SDK 格式的配置
     */
    static TmAgent::AgentConfig toSdkConfig(const LLMConfig& llmConfig);
    
    /**
     * @brief 将 SDK 的 AgentConfig 转换回 LLMConfig
     * 
     * 用于从插件接收配置并转换回主应用格式（如果需要）。
     * 
     * @param sdkConfig SDK 格式的配置
     * @return LLMConfig 主应用的配置格式
     */
    static LLMConfig fromSdkConfig(const TmAgent::AgentConfig& sdkConfig);
};

#endif // CONFIGADAPTER_H
