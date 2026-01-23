---
description: 为 TmAgent 快速添加一个新的 AI 工具能力
---

# /add-tool 工作流

此工作流指导你如何按照 TmAgent 的最新架构方案，为 Agent 添加一个新的功能工具。

## 1. 准备工作
确定工具的名称、职责以及所需的入参（JSON Schema）。

## 2. 实现 ITool 接口
在 `src/core/tools/` 目录下创建一个新的头文件（或在现有文件中添加），继承 `ITool` 接口。

```cpp
class MyNewTool : public ITool {
public:
    // 1. 定义工具 Schema
    Tool getSchema() const override {
        Tool tool;
        tool.name = "my_tool_name";
        tool.description = "描述工具能做什么";
        tool.inputSchema = QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"param1", QJsonObject{{"type", "string"}, {"description", "参数描述"}}}
            }},
            {"required", QJsonArray{"param1"}}
        };
        return tool;
    }

    // 2. 实现执行逻辑
    ToolResult execute(const QJsonObject& args) override {
        QString val = args["param1"].toString();
        // 执行逻辑...
        return ToolResult("原始结果数据", "给用户看的摘要", true);
    }
};
```

## 3. 注册工具
使用 `REGISTER_TOOL_INSTANCE` 宏实现零配置注册。**必须**在工具类定义的下方调用。

```cpp
// 在 .h 或 .cpp 的文件作用域内
REGISTER_TOOL_INSTANCE(MyNewTool, "my_tool_name")
```

## 4. 验证集成
1. 编译并启动 TmAgent。
2. 观察控制台日志：`[ToolRegistry] 注册工厂成功: my_tool_name`。
3. 在对话框中要求 Agent 使用该工具，例如：“请使用 my_tool_name 执行 XXX”。

## 注意事项
- 确保包含必要头文件：`#include "agent/ToolRegistry.h"`。
- 工具名应全局唯一且具有描述性。
- `ToolResult` 的 `userSummary` 应当简洁明了，以便在 UI 上展示。
