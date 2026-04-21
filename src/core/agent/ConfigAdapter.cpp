#include "ConfigAdapter.h"

TmAgent::AgentConfig ConfigAdapter::toSdkConfig(const LLMConfig& llmConfig)
{
    TmAgent::AgentConfig sdkConfig;
    
    // === Agent 标识 ===
    sdkConfig.uuid = llmConfig.uuid;
    sdkConfig.userName = llmConfig.userName;
    
    // === 接入点和模型配置 ===
    sdkConfig.providerInstanceId = llmConfig.providerInstanceId;
    sdkConfig.selectedModelId = llmConfig.selectedModelId;
    
    // === 旧路径兼容 ===
    sdkConfig.configId = llmConfig.configId;
    
    // === 提示词和执行模式 ===
    sdkConfig.systemPrompt = llmConfig.systemPrompt;
    sdkConfig.executionMode = llmConfig.executionMode;
    
    // === 工作目录 ===
    sdkConfig.workspaceDir = llmConfig.workspaceDir;
    
    // === 递归控制 ===
    sdkConfig.recursionDepth = llmConfig.recursionDepth;
    
    return sdkConfig;
}

LLMConfig ConfigAdapter::fromSdkConfig(const TmAgent::AgentConfig& sdkConfig)
{
    LLMConfig llmConfig;
    
    // === Agent 标识 ===
    llmConfig.uuid = sdkConfig.uuid;
    llmConfig.userName = sdkConfig.userName;
    
    // === 接入点和模型配置 ===
    llmConfig.providerInstanceId = sdkConfig.providerInstanceId;
    llmConfig.selectedModelId = sdkConfig.selectedModelId;
    
    // === 旧路径兼容 ===
    llmConfig.configId = sdkConfig.configId;
    
    // === 提示词和执行模式 ===
    llmConfig.systemPrompt = sdkConfig.systemPrompt;
    llmConfig.executionMode = sdkConfig.executionMode;
    
    // === 工作目录 ===
    llmConfig.workspaceDir = sdkConfig.workspaceDir;
    
    // === 递归控制 ===
    llmConfig.recursionDepth = sdkConfig.recursionDepth;
    
    return llmConfig;
}
