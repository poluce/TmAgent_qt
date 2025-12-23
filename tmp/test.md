sequenceDiagram
participant User as 用户 (UI)
participant Widget as LLMConfigWidget
participant Agent as LLMAgent
participant Network as QNetworkAccessManager
participant API as DeepSeek API
participant Dispatcher as ToolDispatcher
participant Tools as 工具实现 (FileTool/ShellTool)

    User->>Widget: 输入消息，点击发送
    Widget->>Agent: sendMessage(prompt)
    Agent->>Agent: sendPromptInternal(prompt, true)
    Agent->>Agent: buildRequestJson() 构造请求体
    Agent->>Network: POST /chat/completions
    Network->>API: 发送 SSE 流式请求

    loop 流式响应
        API-->>Agent: data: {...} (SSE chunk)
        Agent->>Agent: processStreamChunk()
        Agent-->>Widget: chunkReceived(chunk)
        Widget->>Widget: 实时显示文本
    end

    alt 普通回复 (finish_reason: "stop")
        Agent->>Agent: handleStreamFinished()
        Agent-->>Widget: finished(fullContent)
        Widget->>Widget: Markdown 渲染并显示
    else 工具调用 (finish_reason: "tool_calls")
        Agent->>Agent: handleStreamFinished()
        Agent->>Agent: assembleToolCalls() 组装工具调用
        Agent->>Agent: handleToolUseResponse()
        Agent-->>Widget: toolCallRequested(id, name, input)
        Widget->>Dispatcher: dispatch(toolName, input)
        Dispatcher->>Tools: 调用具体工具函数
        Tools-->>Dispatcher: 返回执行结果
        Dispatcher-->>Widget: 返回 result
        Widget->>Agent: submitToolResult(toolId, result)
        Agent->>Agent: continueConversationWithToolResults()
        Agent->>Network: POST /chat/completions (带工具结果)
        Note right of Agent: 循环直到 LLM 返回最终回复
    end
