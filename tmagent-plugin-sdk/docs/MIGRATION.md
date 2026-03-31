# TmAgent 插件迁移指南

本指南帮助您将现有的 TmAgent 插件从旧接口迁移到新的 SDK 架构。

## 目录

- [迁移概述](#迁移概述)
- [为什么要迁移](#为什么要迁移)
- [迁移前准备](#迁移前准备)
- [接口对比](#接口对比)
- [迁移步骤](#迁移步骤)
- [数据结构迁移](#数据结构迁移)
- [构建配置迁移](#构建配置迁移)
- [常见迁移问题](#常见迁移问题)
- [验证和测试](#验证和测试)

---

## 迁移概述

### 架构变化

**旧架构**：
- 插件直接依赖 `src/core/` 内部实现
- 紧耦合，无法独立编译
- 需要完整的 TmAgent 源码

**新架构**：
- 插件仅依赖轻量级 SDK（< 100KB）
- 松耦合，可独立编译和分发
- 仅需 SDK 头文件和 Qt

### 迁移时间线

- **阶段 1（当前）**：主应用同时支持新旧接口
- **阶段 2（v1.1.0）**：旧接口标记为 deprecated
- **阶段 3（v2.0.0）**：移除旧接口支持

---

## 为什么要迁移

### 优势

1. **独立编译**：无需 TmAgent 完整源码
2. **ABI 稳定**：SDK 次版本更新无需重新编译插件
3. **更快开发**：SDK 体积小，编译快速
4. **更好隔离**：插件错误不影响主应用
5. **标准化**：统一的接口和数据格式
6. **跨平台**：更好的跨平台兼容性

### 兼容性保证

- 主应用在 v2.0.0 前继续支持旧接口
- 迁移后的插件功能完全一致
- 可以逐步迁移，不影响其他插件


---

## 迁移前准备

### 1. 备份现有代码

```bash
git checkout -b migration/sdk-upgrade
git commit -am "Backup before SDK migration"
```

### 2. 安装 SDK

下载并解压 TmAgent Plugin SDK 到开发目录：

```bash
cd ~/development
tar -xzf tmagent-plugin-sdk-1.0.0.tar.gz
export TMAGENT_SDK_PATH=~/development/tmagent-plugin-sdk
```

### 3. 检查依赖

确保您的插件：
- 使用 Qt 5.15+ 或 Qt 6.2+
- 不依赖 TmAgent 内部私有 API
- 所有第三方依赖已明确声明

---

## 接口对比

### 工具插件接口

#### 旧接口（src/core/agent/IToolPlugin.h）

```cpp
// 旧接口
class IToolPlugin {
public:
    virtual ~IToolPlugin() = default;
    virtual QString pluginId() const = 0;
    virtual QString displayName() const = 0;
    virtual QStringList toolNames() const = 0;
    virtual QList<Tool> listTools() const = 0;
    virtual ToolResult execute(const ToolCall& call) = 0;
};

#define TOOL_PLUGIN_IID "org.tmagent.IToolPlugin"
Q_DECLARE_INTERFACE(IToolPlugin, TOOL_PLUGIN_IID)
```

#### 新接口（SDK）

```cpp
// 新接口
namespace TmAgent {

class IToolPlugin {
public:
    virtual ~IToolPlugin() = default;
    
    // 返回插件元数据（替代多个单独方法）
    virtual ToolPluginDescriptor descriptor() const = 0;
    
    // 创建提供者实例（新增）
    virtual IToolProvider* createProvider(IToolPluginHost* host, 
                                         QObject* parent) = 0;
    
    // 配置提供者（可选，新增）
    virtual bool configureProvider(IToolProvider* provider,
                                  const QJsonObject& config,
                                  QString* error) { return true; }
    
    // 健康检查（可选，新增）
    virtual ToolPluginHealth health(const IToolProvider* provider) const;
};

} // namespace TmAgent

#define TMAGENT_TOOL_PLUGIN_IID "org.tmagent.ToolPlugin/1.0"
Q_DECLARE_INTERFACE(TmAgent::IToolPlugin, TMAGENT_TOOL_PLUGIN_IID)
```

### 关键变化

| 旧接口 | 新接口 | 说明 |
|--------|--------|------|
| `pluginId()` | `descriptor().pluginId` | 元数据统一到 descriptor |
| `displayName()` | `descriptor().displayName` | 元数据统一到 descriptor |
| `toolNames()` | `descriptor().toolNames` | 元数据统一到 descriptor |
| `listTools()` | `IToolProvider::listTools()` | 移到 Provider 接口 |
| `execute()` | `IToolProvider::execute()` | 移到 Provider 接口 |
| 无 | `createProvider()` | 新增：创建 Provider 实例 |
| 无 | `configureProvider()` | 新增：配置支持 |
| 无 | `health()` | 新增：健康检查 |


### 后端插件接口

#### 旧接口

```cpp
// 旧接口（简化示例）
class IBackendPlugin {
public:
    virtual QString backendId() const = 0;
    virtual DelegateBackend* createDelegateBackend() = 0;
    virtual TeammateBackend* createTeammateBackend() = 0;
};
```

#### 新接口

```cpp
// 新接口
namespace TmAgent {

class IBackendPlugin {
public:
    virtual ~IBackendPlugin() = default;
    
    // 返回后端元数据
    virtual BackendDescriptor descriptor() const = 0;
    
    // 创建委托后端
    virtual IDelegateBackend* createDelegateBackend(QObject* parent) = 0;
    
    // 创建队友后端
    virtual ITeammateBackend* createTeammateBackend(QObject* parent) = 0;
};

} // namespace TmAgent
```

### 宿主回调接口

#### 旧方式

```cpp
// 旧方式：直接访问内部类
#include "core/agent/ToolDispatcher.h"
#include "core/logging/LogCatalog.h"

// 在插件中直接调用
ToolDispatcher::instance()->execute(call);
LogCatalog::instance()->log("message");
```

#### 新方式

```cpp
// 新方式：通过 IToolPluginHost 回调
class MyProvider : public IToolProvider {
public:
    MyProvider(IToolPluginHost* host) : m_host(host) {}
    
    ToolResult execute(const ToolCall& call) override {
        // 调用其他工具
        ToolResult result = m_host->executeHostTool(otherCall);
        
        // 记录日志
        m_host->logInfo("my_plugin", "Processing...");
        
        // 读取配置
        QJsonObject config = m_host->getPluginConfig("my_plugin");
        
        return result;
    }
    
private:
    IToolPluginHost* m_host;
};
```

---

## 迁移步骤

### 步骤 1：更新项目结构

#### 旧结构

```
my-plugin/
├── MyPlugin.h
├── MyPlugin.cpp
├── MyPlugin.pro
└── (依赖 TmAgent 源码)
```

#### 新结构

```
my-plugin/
├── MyPlugin.h          # 插件入口类
├── MyPlugin.cpp
├── MyProvider.h        # 新增：Provider 类
├── MyProvider.cpp
├── my_plugin.json      # 新增：元数据文件
├── MyPlugin.pro        # 更新：使用 SDK
└── CMakeLists.txt      # 可选：CMake 支持
```

### 步骤 2：拆分插件和提供者

**旧代码**：

```cpp
// 旧代码：插件类直接实现工具逻辑
class MyPlugin : public QObject, public IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TOOL_PLUGIN_IID)
    Q_INTERFACES(IToolPlugin)

public:
    QString pluginId() const override { return "my_plugin"; }
    QString displayName() const override { return "My Plugin"; }
    QStringList toolNames() const override { return {"tool1", "tool2"}; }
    
    QList<Tool> listTools() const override {
        // 工具定义
    }
    
    ToolResult execute(const ToolCall& call) override {
        // 工具执行逻辑
    }
};
```


**新代码**：

```cpp
// 新代码：拆分为插件类和提供者类

// MyProvider.h - 提供者类
#include <tmagent/plugin/IToolProvider.h>

class MyProvider : public QObject, public TmAgent::IToolProvider {
    Q_OBJECT
public:
    explicit MyProvider(TmAgent::IToolPluginHost* host, QObject* parent = nullptr);
    
    QList<TmAgent::Tool> listTools() const override;
    TmAgent::ToolResult execute(const TmAgent::ToolCall& call) override;

private:
    TmAgent::IToolPluginHost* m_host;
};

// MyPlugin.h - 插件入口类
#include <tmagent/plugin/IToolPlugin.h>

class MyPlugin : public QObject, public TmAgent::IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "my_plugin.json")
    Q_INTERFACES(TmAgent::IToolPlugin)

public:
    TmAgent::ToolPluginDescriptor descriptor() const override;
    TmAgent::IToolProvider* createProvider(TmAgent::IToolPluginHost* host, 
                                          QObject* parent) override;
};

// MyPlugin.cpp
#include "MyPlugin.h"
#include "MyProvider.h"
#include <tmagent/version.h>

TmAgent::ToolPluginDescriptor MyPlugin::descriptor() const
{
    TmAgent::ToolPluginDescriptor desc;
    desc.pluginId = "my_plugin";
    desc.displayName = "My Plugin";
    desc.version = "1.0.0";
    desc.description = "My plugin description";
    desc.category = "tools";
    desc.toolNames = QStringList{"tool1", "tool2"};
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    
    return desc;
}

TmAgent::IToolProvider* MyPlugin::createProvider(
    TmAgent::IToolPluginHost* host, 
    QObject* parent)
{
    return new MyProvider(host, parent);
}
```

### 步骤 3：更新头文件引用

**旧引用**：

```cpp
#include "core/agent/IToolPlugin.h"
#include "core/agent/ToolTypes.h"
#include "core/agent/ToolDispatcher.h"
#include "core/logging/LogCatalog.h"
#include "core/parser/TreeSitterParser.h"
```

**新引用**：

```cpp
#include <tmagent/plugin/IToolPlugin.h>
#include <tmagent/plugin/IToolProvider.h>
#include <tmagent/plugin/IToolPluginHost.h>
#include <tmagent/types/ToolTypes.h>
#include <tmagent/support/ToolSchemaBuilder.h>
#include <tmagent/version.h>
```

### 步骤 4：更新命名空间

**旧代码**：

```cpp
// 全局命名空间
Tool tool;
ToolCall call;
ToolResult result;
```

**新代码**：

```cpp
// TmAgent 命名空间
using namespace TmAgent;

Tool tool;
ToolCall call;
ToolResult result;

// 或者使用完全限定名
TmAgent::Tool tool;
TmAgent::ToolCall call;
TmAgent::ToolResult result;
```

### 步骤 5：替换直接依赖

#### 替换 ToolDispatcher

**旧代码**：

```cpp
#include "core/agent/ToolDispatcher.h"

ToolResult MyPlugin::execute(const ToolCall& call) {
    // 直接调用 ToolDispatcher
    ToolCall otherCall;
    otherCall.name = "other_tool";
    ToolResult result = ToolDispatcher::instance()->execute(otherCall);
    return result;
}
```

**新代码**：

```cpp
// 通过 IToolPluginHost 回调
ToolResult MyProvider::execute(const ToolCall& call) {
    ToolCall otherCall;
    otherCall.name = "other_tool";
    ToolResult result = m_host->executeHostTool(otherCall);
    return result;
}
```

#### 替换日志系统

**旧代码**：

```cpp
#include "core/logging/LogCatalog.h"

void MyPlugin::someMethod() {
    LogCatalog::instance()->log(LogLevel::Info, "my_plugin", "Message");
}
```

**新代码**：

```cpp
void MyProvider::someMethod() {
    m_host->logInfo("my_plugin", "Message");
    m_host->logDebug("my_plugin", "Debug message");
    m_host->logWarning("my_plugin", "Warning message");
    m_host->logError("my_plugin", "Error message");
}
```

#### 替换 TreeSitterParser

**旧代码**：

```cpp
#include "core/parser/TreeSitterParser.h"

ToolResult MyPlugin::parseCode(const QString& code) {
    TreeSitterParser parser;
    QJsonObject ast = parser.parse("cpp", code);
    return ToolResult(ast);
}
```

**新代码**：

```cpp
ToolResult MyProvider::parseCode(const QString& code) {
    QString error;
    QJsonObject ast = m_host->parseCode("cpp", code, &error);
    
    if (ast.isEmpty() && !error.isEmpty()) {
        return ToolResult(
            QString("Parse error: %1").arg(error),
            "Parse failed",
            false
        );
    }
    
    return ToolResult(
        QString::fromUtf8(QJsonDocument(ast).toJson()),
        "Parse successful",
        true,
        ast
    );
}
```


---

## 数据结构迁移

### LLMConfig → AgentConfig

**旧代码**：

```cpp
#include "llm/LLMTypes.h"

void MyBackend::delegate(const LLMConfig& config) {
    QString modelId = config.selectedModelId;
    QString systemPrompt = config.systemPrompt;
    // ...
}
```

**新代码**：

```cpp
#include <tmagent/types/CommonTypes.h>

void MyBackend::delegate(const TmAgent::AgentConfig& config) {
    QString modelId = config.selectedModelId;
    QString systemPrompt = config.systemPrompt;
    
    // 验证配置
    if (!config.isValid()) {
        // 处理无效配置
    }
    
    // 检查是否允许委托
    if (!config.canDelegate()) {
        // 递归深度已达上限
    }
}
```

### Teammate → TeammateConfig/TeammateState

**旧代码**：

```cpp
#include "core/model/Teammate.h"

void MyBackend::createSession(Teammate* teammate) {
    QString id = teammate->id();
    QString name = teammate->name();
    QString status = teammate->status();
    // ...
}
```

**新代码**：

```cpp
#include <tmagent/types/CommonTypes.h>

// 创建会话时使用 TeammateConfig
ITeammateBackend::CreateResult MyBackend::createSession(
    const QString& teammateId,
    const TmAgent::TeammateConfig& config)
{
    QString name = config.name;
    QString role = config.role;
    QString backend = config.backend;
    // ...
}

// 查询状态时使用 TeammateState
TmAgent::TeammateState MyBackend::getState(const QString& teammateId) {
    TmAgent::TeammateState state;
    state.id = teammateId;
    state.status = "idle";
    state.turnCount = 0;
    // ...
    return state;
}
```

### DelegateRequest 结构变化

**旧代码**：

```cpp
struct DelegateRequest {
    QString task;
    LLMConfig childConfig;
    ToolDispatcher* dispatcher;  // 直接指针
    ModelFactory* modelFactory;  // 直接指针
};
```

**新代码**：

```cpp
#include <tmagent/plugin/IDelegateBackend.h>

// 使用 SDK 定义的结构
TmAgent::DelegateRequest request;
request.task = "Task description";
request.executionPrompt = "Execution prompt";
request.childConfig = agentConfig;
request.toolExecutor = toolExecutorInterface;  // 接口指针
request.modelFactory = modelFactoryInterface;  // 接口指针
request.expectedTimeoutMs = 30000;
request.maxResponseChars = 10000;
request.restrictDelegation = false;
```

---

## 构建配置迁移

### qmake 配置

**旧配置（MyPlugin.pro）**：

```qmake
QT += core
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = MyPlugin

# 依赖 TmAgent 源码
TMAGENT_ROOT = ../../TmAgent
INCLUDEPATH += $TMAGENT_ROOT/src/core
INCLUDEPATH += $TMAGENT_ROOT/src/llm

SOURCES += MyPlugin.cpp
HEADERS += MyPlugin.h
```

**新配置（MyPlugin.pro）**：

```qmake
QT += core
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = MyPlugin

# 仅依赖 SDK
TMAGENT_SDK_PATH = /path/to/tmagent-plugin-sdk
include($TMAGENT_SDK_PATH/tmagent-plugin-sdk.pri)

SOURCES += \
    MyPlugin.cpp \
    MyProvider.cpp

HEADERS += \
    MyPlugin.h \
    MyProvider.h

OTHER_FILES += my_plugin.json

# 输出目录
CONFIG(debug, debug|release) {
    DESTDIR = $$OUT_PWD/debug
} else {
    DESTDIR = $$OUT_PWD/release
}
```

### CMake 配置

**旧配置**：

```cmake
# 依赖 TmAgent 源码
set(TMAGENT_ROOT ${CMAKE_SOURCE_DIR}/../TmAgent)
include_directories(${TMAGENT_ROOT}/src/core)
include_directories(${TMAGENT_ROOT}/src/llm)

add_library(MyPlugin MODULE MyPlugin.cpp)
target_link_libraries(MyPlugin PRIVATE TmAgentCore)
```

**新配置**：

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyPlugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 COMPONENTS Core REQUIRED)

# 使用 SDK
set(TMAGENT_SDK_PATH "/path/to/tmagent-plugin-sdk" CACHE PATH "Path to SDK")
include_directories(${TMAGENT_SDK_PATH}/include)

add_library(MyPlugin MODULE
    MyPlugin.cpp
    MyProvider.cpp
)

target_link_libraries(MyPlugin PRIVATE Qt5::Core)

# 安装规则
install(TARGETS MyPlugin
    LIBRARY DESTINATION lib/tmagent/plugins/tools
)
```

### 元数据文件

创建 `my_plugin.json`：

```json
{
  "pluginId": "my_plugin",
  "displayName": "My Plugin",
  "version": "1.0.0",
  "sdkVersion": "1.0.0",
  "category": "tools",
  "description": "Plugin description",
  "author": "Your Name",
  "license": "MIT",
  "dependencies": {
    "qt": ">=5.15.0",
    "sdk": "^1.0.0"
  }
}
```


---

## 完整迁移示例

### 示例：Shell 工具插件迁移

#### 旧实现

```cpp
// ShellPlugin.h (旧版本)
#include "core/agent/IToolPlugin.h"
#include <QProcess>

class ShellPlugin : public QObject, public IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TOOL_PLUGIN_IID)
    Q_INTERFACES(IToolPlugin)

public:
    QString pluginId() const override { return "shell"; }
    QString displayName() const override { return "Shell Tools"; }
    QStringList toolNames() const override { return {"execute_shell"}; }
    
    QList<Tool> listTools() const override {
        Tool tool;
        tool.name = "execute_shell";
        tool.description = "Execute shell command";
        tool.inputSchema = QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"command", QJsonObject{
                    {"type", "string"},
                    {"description", "Command to execute"}
                }}
            }},
            {"required", QJsonArray{"command"}}
        };
        return {tool};
    }
    
    ToolResult execute(const ToolCall& call) override {
        if (call.name != "execute_shell") {
            return ToolResult("Unknown tool", "Error", false);
        }
        
        QString command = call.input["command"].toString();
        
        QProcess process;
        process.start(command);
        process.waitForFinished();
        
        QString output = process.readAllStandardOutput();
        return ToolResult(output, "Command executed", true);
    }
};
```


#### 新实现

```cpp
// ShellProvider.h (新版本)
#ifndef SHELLPROVIDER_H
#define SHELLPROVIDER_H

#include <tmagent/plugin/IToolProvider.h>
#include <tmagent/plugin/IToolPluginHost.h>
#include <QObject>
#include <QProcess>

using namespace TmAgent;

class ShellProvider : public QObject, public IToolProvider {
    Q_OBJECT
public:
    explicit ShellProvider(IToolPluginHost* host, QObject* parent = nullptr);
    
    QList<Tool> listTools() const override;
    ToolResult execute(const ToolCall& call) override;

private:
    IToolPluginHost* m_host;
};

#endif // SHELLPROVIDER_H
```

```cpp
// ShellProvider.cpp
#include "ShellProvider.h"
#include <tmagent/support/ToolSchemaBuilder.h>

ShellProvider::ShellProvider(IToolPluginHost* host, QObject* parent)
    : QObject(parent), m_host(host)
{
}

QList<Tool> ShellProvider::listTools() const
{
    QList<Tool> tools;
    
    Tool tool;
    tool.name = "execute_shell";
    tool.description = "Execute shell command";
    tool.inputSchema = makeToolSchema(
        "execute_shell",
        "Execute shell command",
        QJsonObject{
            {"command", makePropertySchema("string", "Command to execute")}
        },
        QStringList{"command"}
    )["function"].toObject()["parameters"].toObject();
    
    tools.append(tool);
    return tools;
}

ToolResult ShellProvider::execute(const ToolCall& call)
{
    if (call.name != "execute_shell") {
        return ToolResult(
            QString("Unknown tool: %1").arg(call.name),
            "Error",
            false,
            QJsonObject{{"errorCode", "unknown_tool"}}
        );
    }
    
    QString command = call.input["command"].toString();
    
    // 记录日志
    if (m_host) {
        m_host->logInfo("shell", QString("Executing: %1").arg(command));
    }
    
    QProcess process;
    process.start(command);
    
    if (!process.waitForFinished(30000)) {
        if (m_host) {
            m_host->logError("shell", "Command timeout");
        }
        return ToolResult(
            "Command execution timeout",
            "Timeout",
            false,
            QJsonObject{{"errorCode", "timeout"}}
        );
    }
    
    if (process.exitCode() != 0) {
        QString stderr = process.readAllStandardError();
        if (m_host) {
            m_host->logWarning("shell", 
                QString("Command failed with exit code %1").arg(process.exitCode()));
        }
        return ToolResult(
            QString("Command failed: %1").arg(stderr),
            "Execution failed",
            false,
            QJsonObject{
                {"errorCode", "execution_failed"},
                {"exitCode", process.exitCode()}
            }
        );
    }
    
    QString output = process.readAllStandardOutput();
    return ToolResult(output, "Command executed successfully", true);
}
```

```cpp
// ShellPlugin.h
#ifndef SHELLPLUGIN_H
#define SHELLPLUGIN_H

#include <tmagent/plugin/IToolPlugin.h>
#include <QObject>

using namespace TmAgent;

class ShellPlugin : public QObject, public IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "shell.json")
    Q_INTERFACES(TmAgent::IToolPlugin)

public:
    ToolPluginDescriptor descriptor() const override;
    IToolProvider* createProvider(IToolPluginHost* host, QObject* parent) override;
};

#endif // SHELLPLUGIN_H
```

```cpp
// ShellPlugin.cpp
#include "ShellPlugin.h"
#include "ShellProvider.h"
#include <tmagent/version.h>

ToolPluginDescriptor ShellPlugin::descriptor() const
{
    ToolPluginDescriptor desc;
    desc.pluginId = "shell";
    desc.displayName = "Shell Tools";
    desc.version = "1.0.0";
    desc.description = "Execute shell commands";
    desc.category = "system";
    desc.toolNames = QStringList{"execute_shell"};
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    
    return desc;
}

IToolProvider* ShellPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new ShellProvider(host, parent);
}
```

```json
// shell.json
{
  "pluginId": "shell",
  "displayName": "Shell Tools",
  "version": "1.0.0",
  "sdkVersion": "1.0.0",
  "category": "system",
  "description": "Execute shell commands",
  "author": "TmAgent Team",
  "license": "MIT"
}
```

---

## 常见迁移问题

### Q1: 如何处理插件内部状态？

**问题**：旧插件在插件类中保存状态，新架构如何处理？

**解决方案**：将状态移到 Provider 类中

```cpp
class MyProvider : public QObject, public IToolProvider {
public:
    MyProvider(IToolPluginHost* host, QObject* parent)
        : QObject(parent), m_host(host), m_counter(0) {}
    
    ToolResult execute(const ToolCall& call) override {
        m_counter++;  // Provider 实例保存状态
        m_host->logInfo("my_plugin", 
            QString("Execution count: %1").arg(m_counter));
        // ...
    }

private:
    IToolPluginHost* m_host;
    int m_counter;  // 状态保存在 Provider 中
};
```

### Q2: 如何访问应用配置？

**旧方式**：

```cpp
#include "core/config/AppConfig.h"
QString workspaceDir = AppConfig::instance()->workspaceDir();
```

**新方式**：

```cpp
// 通过 IToolPluginHost 访问
QJsonObject appConfig = m_host->getAppConfig("workspace");
QString workspaceDir = appConfig["directory"].toString();
```

### Q3: 如何处理插件数据存储？

**新方式**：

```cpp
ToolResult MyProvider::saveData(const QJsonObject& data) {
    // 获取插件专用数据目录
    QString dataDir = m_host->getPluginDataDir("my_plugin");
    
    // 保存数据到文件
    QFile file(dataDir + "/data.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(data).toJson());
        file.close();
        return ToolResult("Data saved", "Success", true);
    }
    
    return ToolResult("Failed to save data", "Error", false);
}
```

### Q4: 编译错误：找不到头文件

**错误信息**：

```
fatal error: core/agent/IToolPlugin.h: No such file or directory
```

**解决方案**：

1. 更新所有 `#include` 语句使用 SDK 路径
2. 确保 `.pro` 或 `CMakeLists.txt` 正确引入 SDK
3. 检查 `TMAGENT_SDK_PATH` 环境变量

### Q5: 链接错误：未定义的引用

**错误信息**：

```
undefined reference to `ToolDispatcher::instance()'
```

**解决方案**：

这表示代码仍在使用旧的直接依赖。需要：

1. 移除对 `ToolDispatcher` 的直接调用
2. 使用 `IToolPluginHost::executeHostTool()` 替代
3. 确保不链接 TmAgent 内部库


---

## 验证和测试

### 迁移检查清单

完成迁移后，使用此清单验证：

- [ ] **编译检查**
  - [ ] 插件可以独立编译（不依赖 TmAgent 源码）
  - [ ] 无编译警告
  - [ ] 无链接错误

- [ ] **接口检查**
  - [ ] 实现了 `IToolPlugin` 或 `IBackendPlugin`
  - [ ] 实现了 `IToolProvider` 或后端接口
  - [ ] 使用了正确的 IID 宏
  - [ ] 元数据文件存在且格式正确

- [ ] **依赖检查**
  - [ ] 移除了所有 `src/core/` 引用
  - [ ] 移除了所有 `llm/` 引用
  - [ ] 仅依赖 SDK 头文件
  - [ ] 第三方依赖已明确声明

- [ ] **功能检查**
  - [ ] 所有工具功能正常
  - [ ] 错误处理正确
  - [ ] 日志记录工作
  - [ ] 配置读写正常

- [ ] **版本检查**
  - [ ] `sdkVersionMajor` 和 `sdkVersionMinor` 已设置
  - [ ] 插件版本号已更新
  - [ ] 元数据文件版本匹配

### 编译测试

```bash
# 清理旧构建
rm -rf build/
mkdir build
cd build

# 使用 qmake
qmake ../MyPlugin.pro
make

# 或使用 CMake
cmake ..
cmake --build .

# 检查编译产物
ls -lh libMyPlugin.so  # Linux
ls -lh MyPlugin.dll    # Windows
```

### 加载测试

```bash
# 复制插件到测试目录
cp libMyPlugin.so ~/.local/share/TmAgent/plugins/tools/
cp my_plugin.json ~/.local/share/TmAgent/plugins/tools/

# 启动 TmAgent 并检查日志
tmagent --log-level=debug

# 查看插件加载日志
grep "my_plugin" ~/.local/share/TmAgent/logs/tmagent.log
```

### 功能测试

创建测试脚本验证工具功能：

```cpp
// test_plugin.cpp
#include <QtTest>
#include "MyProvider.h"

class MockHost : public IToolPluginHost {
public:
    QStringList availableTeammateBackendIds() const override { return {}; }
    QStringList availableTools() const override { return {}; }
    ToolResult executeHostTool(const ToolCall&) override { return ToolResult(); }
    void logDebug(const QString&, const QString&) override {}
    void logInfo(const QString&, const QString&) override {}
    void logWarning(const QString&, const QString&) override {}
    void logError(const QString&, const QString&) override {}
    QJsonObject getPluginConfig(const QString&) const override { return {}; }
    bool setPluginConfig(const QString&, const QJsonObject&, QString*) override { return true; }
    QString getPluginDataDir(const QString&) const override { return "/tmp"; }
    QString getAppDataDir() const override { return "/tmp"; }
    QJsonObject parseCode(const QString&, const QString&, QString*) const override { return {}; }
};

class MyProviderTest : public QObject {
    Q_OBJECT
private slots:
    void testExecute() {
        MockHost host;
        MyProvider provider(&host);
        
        ToolCall call;
        call.name = "my_tool";
        call.input = QJsonObject{{"param", "value"}};
        
        ToolResult result = provider.execute(call);
        
        QVERIFY(result.success);
        QVERIFY(!result.rawContent.isEmpty());
    }
};

QTEST_MAIN(MyProviderTest)
#include "test_plugin.moc"
```

### 性能测试

验证迁移后性能没有退化：

```cpp
void benchmarkToolExecution() {
    MyProvider provider(&host);
    
    ToolCall call;
    call.name = "my_tool";
    call.input = QJsonObject{{"param", "value"}};
    
    QElapsedTimer timer;
    timer.start();
    
    for (int i = 0; i < 1000; i++) {
        provider.execute(call);
    }
    
    qint64 elapsed = timer.elapsed();
    qDebug() << "1000 executions took" << elapsed << "ms";
    qDebug() << "Average:" << (elapsed / 1000.0) << "ms per execution";
}
```

---

## 迁移后优化建议

### 1. 利用新特性

迁移完成后，可以利用 SDK 的新特性：

```cpp
// 实现配置支持
bool MyPlugin::configureProvider(IToolProvider* provider,
                                const QJsonObject& config,
                                QString* error)
{
    MyProvider* myProvider = qobject_cast<MyProvider*>(provider);
    if (!myProvider) {
        if (error) *error = "Invalid provider";
        return false;
    }
    
    // 应用配置
    myProvider->setApiKey(config["api_key"].toString());
    myProvider->setTimeout(config["timeout"].toInt(30));
    
    return true;
}

// 实现健康检查
ToolPluginHealth MyPlugin::health(const IToolProvider* provider) const
{
    const MyProvider* myProvider = qobject_cast<const MyProvider*>(provider);
    
    ToolPluginHealth health;
    if (myProvider && myProvider->isHealthy()) {
        health.status = "healthy";
        health.diagnostics = "All systems operational";
    } else {
        health.status = "unhealthy";
        health.diagnostics = "Service unavailable";
    }
    
    return health;
}
```

### 2. 改进错误处理

使用标准化错误码：

```cpp
ToolResult MyProvider::execute(const ToolCall& call) {
    // 参数验证
    if (!call.input.contains("required_param")) {
        return ToolResult(
            "Missing required parameter: required_param",
            "Parameter error",
            false,
            QJsonObject{
                {"errorCode", "missing_parameter"},
                {"parameter", "required_param"}
            }
        );
    }
    
    // 执行操作
    try {
        return performOperation(call);
    } catch (const std::exception& e) {
        m_host->logError("my_plugin", QString("Exception: %1").arg(e.what()));
        return ToolResult(
            QString("Internal error: %1").arg(e.what()),
            "Execution failed",
            false,
            QJsonObject{{"errorCode", "plugin_exception"}}
        );
    }
}
```

### 3. 添加详细日志

利用分级日志系统：

```cpp
ToolResult MyProvider::execute(const ToolCall& call) {
    m_host->logDebug("my_plugin", 
        QString("Executing %1 with input: %2")
            .arg(call.name)
            .arg(QString::fromUtf8(QJsonDocument(call.input).toJson())));
    
    QElapsedTimer timer;
    timer.start();
    
    ToolResult result = performOperation(call);
    
    qint64 elapsed = timer.elapsed();
    m_host->logInfo("my_plugin", 
        QString("Tool %1 completed in %2 ms").arg(call.name).arg(elapsed));
    
    if (elapsed > 5000) {
        m_host->logWarning("my_plugin", "Slow execution detected");
    }
    
    return result;
}
```

---

## 获取帮助

如果在迁移过程中遇到问题：

1. **查看文档**：
   - [API 参考](API.md)
   - [开发教程](TUTORIAL.md)

2. **参考示例**：
   - `examples/minimal-tool-plugin/`
   - `examples/minimal-backend-plugin/`

3. **社区支持**：
   - [GitHub Issues](https://github.com/tmagent/plugin-sdk/issues)
   - [论坛讨论](https://forum.tmagent.org)

4. **提交问题**：
   - 提供完整的错误信息
   - 包含最小可复现示例
   - 说明您的环境（OS、Qt 版本、编译器）

---

## 总结

迁移到 SDK 架构的关键步骤：

1. ✅ 拆分插件类和提供者类
2. ✅ 更新头文件引用到 SDK
3. ✅ 替换直接依赖为回调接口
4. ✅ 更新构建配置使用 SDK
5. ✅ 创建元数据文件
6. ✅ 测试和验证功能

迁移后的优势：

- 🚀 独立编译，开发更快
- 🔒 ABI 稳定，兼容性更好
- 📦 体积更小，依赖更少
- 🛡️ 更好隔离，更加安全
- 📚 标准化接口，易于维护

祝您迁移顺利！
