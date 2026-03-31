# TmAgent 插件开发教程

本教程将指导您从零开始创建 TmAgent 插件，包括工具插件和后端插件的完整开发流程。

## 目录

- [前置要求](#前置要求)
- [第一部分：创建工具插件](#第一部分创建工具插件)
- [第二部分：创建后端插件](#第二部分创建后端插件)
- [第三部分：高级主题](#第三部分高级主题)
- [常见问题](#常见问题)
- [最佳实践](#最佳实践)

## 前置要求

在开始之前，请确保您的开发环境满足以下要求：

### 必需软件

- **Qt 5.15+** 或 **Qt 6.2+**
- **C++ 编译器**：
  - Windows: MSVC 2019+ 或 MinGW
  - Linux: GCC 9+ 或 Clang 12+
  - macOS: Xcode 12+ (Clang)
- **构建工具**：qmake 或 CMake 3.16+

### SDK 安装

1. 下载 TmAgent Plugin SDK
2. 解压到您的开发目录，例如 `~/tmagent-plugin-sdk`
3. 记录 SDK 路径，后续构建配置需要使用

### 验证安装

检查 SDK 目录结构：

```
tmagent-plugin-sdk/
├── include/
│   └── tmagent/
│       ├── plugin/
│       ├── types/
│       ├── support/
│       └── version.h
├── examples/
├── docs/
└── tmagent-plugin-sdk.pri
```

---

## 第一部分：创建工具插件

工具插件为 AI Agent 提供可调用的工具功能。本节将创建一个简单的计算器插件。



### 步骤 1：创建项目结构

创建插件项目目录：

```bash
mkdir calculator-plugin
cd calculator-plugin
```

创建以下文件：

```
calculator-plugin/
├── CalculatorPlugin.h
├── CalculatorPlugin.cpp
├── calculator.json
├── CalculatorPlugin.pro    # qmake 配置
└── CMakeLists.txt          # CMake 配置（可选）
```

### 步骤 2：定义插件头文件

创建 `CalculatorPlugin.h`：

```cpp
#ifndef CALCULATORPLUGIN_H
#define CALCULATORPLUGIN_H

#include <tmagent/plugin/IToolPlugin.h>
#include <tmagent/plugin/IToolProvider.h>
#include <QObject>

using namespace TmAgent;

// 工具提供者类
class CalculatorProvider : public QObject, public IToolProvider {
    Q_OBJECT
public:
    explicit CalculatorProvider(IToolPluginHost* host, QObject* parent = nullptr);
    
    // 实现 IToolProvider 接口
    QList<Tool> listTools() const override;
    ToolResult execute(const ToolCall& call) override;

private:
    IToolPluginHost* m_host;
    
    // 工具实现方法
    ToolResult add(const QJsonObject& input);
    ToolResult subtract(const QJsonObject& input);
    ToolResult multiply(const QJsonObject& input);
    ToolResult divide(const QJsonObject& input);
};

// 插件入口类
class CalculatorPlugin : public QObject, public IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "calculator.json")
    Q_INTERFACES(TmAgent::IToolPlugin)

public:
    ToolPluginDescriptor descriptor() const override;
    IToolProvider* createProvider(IToolPluginHost* host, QObject* parent) override;
};

#endif // CALCULATORPLUGIN_H
```



### 步骤 3：实现插件逻辑

创建 `CalculatorPlugin.cpp`：

```cpp
#include "CalculatorPlugin.h"
#include <tmagent/support/ToolSchemaBuilder.h>
#include <tmagent/version.h>

// CalculatorProvider 实现
CalculatorProvider::CalculatorProvider(IToolPluginHost* host, QObject* parent)
    : QObject(parent), m_host(host)
{
}

QList<Tool> CalculatorProvider::listTools() const
{
    QList<Tool> tools;
    
    // 定义 add 工具
    Tool addTool;
    addTool.name = "add";
    addTool.description = "Add two numbers";
    addTool.inputSchema = makeToolSchema(
        "add",
        "Add two numbers",
        QJsonObject{
            {"a", makePropertySchema("number", "First number")},
            {"b", makePropertySchema("number", "Second number")}
        },
        QStringList{"a", "b"}
    )["function"].toObject()["parameters"].toObject();
    tools.append(addTool);
    
    // 定义 subtract 工具
    Tool subtractTool;
    subtractTool.name = "subtract";
    subtractTool.description = "Subtract second number from first";
    subtractTool.inputSchema = makeToolSchema(
        "subtract",
        "Subtract second number from first",
        QJsonObject{
            {"a", makePropertySchema("number", "First number")},
            {"b", makePropertySchema("number", "Second number")}
        },
        QStringList{"a", "b"}
    )["function"].toObject()["parameters"].toObject();
    tools.append(subtractTool);
    
    // 定义 multiply 工具
    Tool multiplyTool;
    multiplyTool.name = "multiply";
    multiplyTool.description = "Multiply two numbers";
    multiplyTool.inputSchema = makeToolSchema(
        "multiply",
        "Multiply two numbers",
        QJsonObject{
            {"a", makePropertySchema("number", "First number")},
            {"b", makePropertySchema("number", "Second number")}
        },
        QStringList{"a", "b"}
    )["function"].toObject()["parameters"].toObject();
    tools.append(multiplyTool);
    
    // 定义 divide 工具
    Tool divideTool;
    divideTool.name = "divide";
    divideTool.description = "Divide first number by second";
    divideTool.inputSchema = makeToolSchema(
        "divide",
        "Divide first number by second",
        QJsonObject{
            {"a", makePropertySchema("number", "Dividend")},
            {"b", makePropertySchema("number", "Divisor")}
        },
        QStringList{"a", "b"}
    )["function"].toObject()["parameters"].toObject();
    tools.append(divideTool);
    
    return tools;
}

ToolResult CalculatorProvider::execute(const ToolCall& call)
{
    // 记录工具调用
    if (m_host) {
        m_host->logInfo("calculator", 
            QString("Executing tool: %1").arg(call.name));
    }
    
    // 路由到对应的工具实现
    if (call.name == "add") {
        return add(call.input);
    } else if (call.name == "subtract") {
        return subtract(call.input);
    } else if (call.name == "multiply") {
        return multiply(call.input);
    } else if (call.name == "divide") {
        return divide(call.input);
    }
    
    // 未知工具
    return ToolResult(
        QString("Error: unknown tool '%1'").arg(call.name),
        "Unknown tool",
        false,
        QJsonObject{{"errorCode", "unknown_tool"}}
    );
}

ToolResult CalculatorProvider::add(const QJsonObject& input)
{
    double a = input.value("a").toDouble();
    double b = input.value("b").toDouble();
    double result = a + b;
    
    return ToolResult(
        QString::number(result),
        QString("%1 + %2 = %3").arg(a).arg(b).arg(result),
        true,
        QJsonObject{{"result", result}}
    );
}

ToolResult CalculatorProvider::subtract(const QJsonObject& input)
{
    double a = input.value("a").toDouble();
    double b = input.value("b").toDouble();
    double result = a - b;
    
    return ToolResult(
        QString::number(result),
        QString("%1 - %2 = %3").arg(a).arg(b).arg(result),
        true,
        QJsonObject{{"result", result}}
    );
}

ToolResult CalculatorProvider::multiply(const QJsonObject& input)
{
    double a = input.value("a").toDouble();
    double b = input.value("b").toDouble();
    double result = a * b;
    
    return ToolResult(
        QString::number(result),
        QString("%1 × %2 = %3").arg(a).arg(b).arg(result),
        true,
        QJsonObject{{"result", result}}
    );
}

ToolResult CalculatorProvider::divide(const QJsonObject& input)
{
    double a = input.value("a").toDouble();
    double b = input.value("b").toDouble();
    
    // 检查除零错误
    if (qFuzzyIsNull(b)) {
        return ToolResult(
            "Error: division by zero",
            "Cannot divide by zero",
            false,
            QJsonObject{{"errorCode", "invalid_parameter"}}
        );
    }
    
    double result = a / b;
    
    return ToolResult(
        QString::number(result),
        QString("%1 ÷ %2 = %3").arg(a).arg(b).arg(result),
        true,
        QJsonObject{{"result", result}}
    );
}

// CalculatorPlugin 实现
ToolPluginDescriptor CalculatorPlugin::descriptor() const
{
    ToolPluginDescriptor desc;
    desc.pluginId = "calculator";
    desc.displayName = "Calculator Plugin";
    desc.version = "1.0.0";
    desc.description = "Basic arithmetic operations";
    desc.category = "math";
    desc.toolNames = QStringList{"add", "subtract", "multiply", "divide"};
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    
    return desc;
}

IToolProvider* CalculatorPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new CalculatorProvider(host, parent);
}
```



### 步骤 4：创建元数据文件

创建 `calculator.json`：

```json
{
  "pluginId": "calculator",
  "displayName": "Calculator Plugin",
  "version": "1.0.0",
  "sdkVersion": "1.0.0",
  "category": "math",
  "description": "Basic arithmetic operations (add, subtract, multiply, divide)",
  "author": "Your Name",
  "license": "MIT",
  "dependencies": {
    "qt": ">=5.15.0",
    "sdk": "^1.0.0"
  }
}
```

### 步骤 5：配置构建系统

#### 使用 qmake

创建 `CalculatorPlugin.pro`：

```qmake
QT += core
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = CalculatorPlugin

# 设置 SDK 路径（根据实际情况修改）
TMAGENT_SDK_PATH = /path/to/tmagent-plugin-sdk
include($TMAGENT_SDK_PATH/tmagent-plugin-sdk.pri)

SOURCES += CalculatorPlugin.cpp
HEADERS += CalculatorPlugin.h

# 元数据文件
OTHER_FILES += calculator.json

# 输出目录
CONFIG(debug, debug|release) {
    DESTDIR = $$OUT_PWD/debug
} else {
    DESTDIR = $$OUT_PWD/release
}

# 安装规则
target.path = /usr/local/lib/tmagent/plugins/tools
INSTALLS += target
```

#### 使用 CMake（可选）

创建 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
project(CalculatorPlugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 COMPONENTS Core REQUIRED)

# 设置 SDK 路径
set(TMAGENT_SDK_PATH "/path/to/tmagent-plugin-sdk" CACHE PATH "Path to TmAgent SDK")
include_directories(${TMAGENT_SDK_PATH}/include)

add_library(CalculatorPlugin MODULE
    CalculatorPlugin.cpp
    CalculatorPlugin.h
)

target_link_libraries(CalculatorPlugin PRIVATE Qt5::Core)

# 安装规则
install(TARGETS CalculatorPlugin
    LIBRARY DESTINATION lib/tmagent/plugins/tools
)
```

### 步骤 6：编译插件

#### 使用 qmake

```bash
# 生成 Makefile
qmake CalculatorPlugin.pro

# 编译
make

# 编译产物：CalculatorPlugin.dll (Windows) 或 libCalculatorPlugin.so (Linux)
```

#### 使用 CMake

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### 步骤 7：测试插件

将编译好的插件文件和元数据文件复制到 TmAgent 插件目录：

```bash
# Linux/macOS
cp libCalculatorPlugin.so ~/.local/share/TmAgent/plugins/tools/
cp calculator.json ~/.local/share/TmAgent/plugins/tools/

# Windows
copy CalculatorPlugin.dll %APPDATA%\TmAgent\plugins\tools\
copy calculator.json %APPDATA%\TmAgent\plugins\tools\
```

启动 TmAgent，插件应该会自动加载。您可以在日志中看到加载信息。



---

## 第二部分：创建后端插件

后端插件为 TmAgent 提供 AI 模型接口。本节将创建一个简单的模拟后端插件。

### 步骤 1：创建项目结构

```bash
mkdir mock-backend-plugin
cd mock-backend-plugin
```

创建以下文件：

```
mock-backend-plugin/
├── MockBackendPlugin.h
├── MockBackendPlugin.cpp
├── mock_backend.json
├── MockBackendPlugin.pro
└── CMakeLists.txt
```

### 步骤 2：定义插件头文件

创建 `MockBackendPlugin.h`：

```cpp
#ifndef MOCKBACKENDPLUGIN_H
#define MOCKBACKENDPLUGIN_H

#include <tmagent/plugin/IBackendPlugin.h>
#include <tmagent/plugin/IDelegateBackend.h>
#include <tmagent/plugin/ITeammateBackend.h>
#include <QObject>
#include <QTimer>

using namespace TmAgent;

// 委托会话实现
class MockDelegateSession : public QObject, public IDelegateSession {
    Q_OBJECT
public:
    MockDelegateSession(const DelegateRequest& request,
                       const DelegateCallbacks& callbacks,
                       QObject* parent = nullptr);
    
    QString backendId() const override { return "mock_backend"; }
    void start() override;
    void cancel() override;

private:
    DelegateRequest m_request;
    DelegateCallbacks m_callbacks;
    bool m_cancelled = false;
    QTimer* m_timer;
};

// 委托后端实现
class MockDelegateBackend : public QObject, public IDelegateBackend {
    Q_OBJECT
public:
    explicit MockDelegateBackend(QObject* parent = nullptr);
    
    QString backendId() const override { return "mock_backend"; }
    std::unique_ptr<IDelegateSession> createSession(
        const DelegateRequest& request,
        const DelegateCallbacks& callbacks,
        QString* error) override;
};

// 队友后端实现
class MockTeammateBackend : public QObject, public ITeammateBackend {
    Q_OBJECT
public:
    explicit MockTeammateBackend(QObject* parent = nullptr);
    
    QString backendId() const override { return "mock_backend"; }
    bool ensureReady(QString* error = nullptr) override;
    bool isReady() const override { return m_ready; }
    
    CreateResult createSession(const QString& teammateId,
                              const TeammateConfig& config) override;
    SendResult sendMessage(const QString& teammateId,
                          const QString& text) override;
    bool cancelTurn(const QString& teammateId,
                   QString* error = nullptr) override;
    void destroySession(const QString& teammateId) override;
    void shutdown() override;

private:
    bool m_ready = false;
    QMap<QString, TeammateState> m_sessions;
};

// 插件入口类
class MockBackendPlugin : public QObject, public IBackendPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_BACKEND_PLUGIN_IID FILE "mock_backend.json")
    Q_INTERFACES(TmAgent::IBackendPlugin)

public:
    BackendDescriptor descriptor() const override;
    IDelegateBackend* createDelegateBackend(QObject* parent) override;
    ITeammateBackend* createTeammateBackend(QObject* parent) override;
};

#endif // MOCKBACKENDPLUGIN_H
```



### 步骤 3：实现委托后端

创建 `MockBackendPlugin.cpp`（第一部分 - 委托后端）：

```cpp
#include "MockBackendPlugin.h"
#include <tmagent/version.h>
#include <QUuid>

// MockDelegateSession 实现
MockDelegateSession::MockDelegateSession(const DelegateRequest& request,
                                       const DelegateCallbacks& callbacks,
                                       QObject* parent)
    : QObject(parent), m_request(request), m_callbacks(callbacks)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
}

void MockDelegateSession::start()
{
    if (m_cancelled) return;
    
    // 通知开始活动
    if (m_callbacks.onActivity) {
        m_callbacks.onActivity();
    }
    
    // 模拟流式输出
    if (m_callbacks.onStreamDelta) {
        m_callbacks.onStreamDelta("Processing task: ");
        m_callbacks.onStreamDelta(m_request.task);
        m_callbacks.onStreamDelta("\n");
    }
    
    // 模拟工具调用事件
    if (m_callbacks.onToolEvent) {
        ToolExecutionEvent event;
        event.toolName = "mock_tool";
        event.status = "started";
        m_callbacks.onToolEvent(event);
        
        event.status = "completed";
        m_callbacks.onToolEvent(event);
    }
    
    // 延迟 1 秒后完成
    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (m_cancelled) return;
        
        QString response = QString("Mock response for task: %1\n\n"
                                  "Execution prompt: %2\n"
                                  "Agent: %3")
                          .arg(m_request.task)
                          .arg(m_request.executionPrompt)
                          .arg(m_request.childConfig.uuid);
        
        // 发送摘要
        if (m_callbacks.onSummary) {
            m_callbacks.onSummary("Task completed successfully");
        }
        
        // 通知成功
        if (m_callbacks.onSuccess) {
            m_callbacks.onSuccess(response);
        }
    });
    
    m_timer->start(1000);
}

void MockDelegateSession::cancel()
{
    m_cancelled = true;
    m_timer->stop();
    
    if (m_callbacks.onFailure) {
        m_callbacks.onFailure("Task cancelled by user");
    }
}

// MockDelegateBackend 实现
MockDelegateBackend::MockDelegateBackend(QObject* parent)
    : QObject(parent)
{
}

std::unique_ptr<IDelegateSession> MockDelegateBackend::createSession(
    const DelegateRequest& request,
    const DelegateCallbacks& callbacks,
    QString* error)
{
    // 验证请求
    if (request.task.isEmpty()) {
        if (error) {
            *error = "Task description is empty";
        }
        return nullptr;
    }
    
    if (!request.childConfig.isValid()) {
        if (error) {
            *error = "Invalid child agent configuration";
        }
        return nullptr;
    }
    
    return std::make_unique<MockDelegateSession>(request, callbacks, this);
}
```



### 步骤 4：实现队友后端

继续 `MockBackendPlugin.cpp`（第二部分 - 队友后端）：

```cpp
// MockTeammateBackend 实现
MockTeammateBackend::MockTeammateBackend(QObject* parent)
    : QObject(parent)
{
}

bool MockTeammateBackend::ensureReady(QString* error)
{
    // 模拟初始化过程
    if (!m_ready) {
        // 在实际实现中，这里会进行模型加载、连接建立等操作
        m_ready = true;
    }
    return true;
}

ITeammateBackend::CreateResult MockTeammateBackend::createSession(
    const QString& teammateId,
    const TeammateConfig& config)
{
    CreateResult result;
    
    // 检查会话是否已存在
    if (m_sessions.contains(teammateId)) {
        result.success = false;
        result.error = "Session already exists";
        return result;
    }
    
    // 创建新会话
    QString threadId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    TeammateState state;
    state.id = teammateId;
    state.threadId = threadId;
    state.status = "idle";
    state.turnCount = 0;
    state.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    state.lastActiveAtMs = state.createdAtMs;
    
    m_sessions[teammateId] = state;
    
    result.success = true;
    result.threadId = threadId;
    return result;
}

ITeammateBackend::SendResult MockTeammateBackend::sendMessage(
    const QString& teammateId,
    const QString& text)
{
    SendResult result;
    
    // 检查会话是否存在
    if (!m_sessions.contains(teammateId)) {
        result.success = false;
        result.error = "Session not found";
        return result;
    }
    
    // 更新状态
    TeammateState& state = m_sessions[teammateId];
    state.status = "busy";
    state.lastActiveAtMs = QDateTime::currentMSecsSinceEpoch();
    
    // 生成回合 ID
    QString turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    state.activeTurnId = turnId;
    state.turnCount++;
    
    // 模拟处理（实际实现中会调用 LLM API）
    // 这里简单返回一个模拟响应
    
    // 完成后更新状态
    state.status = "idle";
    state.activeTurnId.clear();
    
    result.success = true;
    result.turnId = turnId;
    return result;
}

bool MockTeammateBackend::cancelTurn(const QString& teammateId, QString* error)
{
    if (!m_sessions.contains(teammateId)) {
        if (error) {
            *error = "Session not found";
        }
        return false;
    }
    
    TeammateState& state = m_sessions[teammateId];
    state.status = "idle";
    state.activeTurnId.clear();
    
    return true;
}

void MockTeammateBackend::destroySession(const QString& teammateId)
{
    m_sessions.remove(teammateId);
}

void MockTeammateBackend::shutdown()
{
    m_sessions.clear();
    m_ready = false;
}

// MockBackendPlugin 实现
BackendDescriptor MockBackendPlugin::descriptor() const
{
    BackendDescriptor desc;
    desc.backendId = "mock_backend";
    desc.displayName = "Mock Backend";
    desc.version = "1.0.0";
    desc.supportsDelegate = true;
    desc.supportsTeammate = true;
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    
    return desc;
}

IDelegateBackend* MockBackendPlugin::createDelegateBackend(QObject* parent)
{
    return new MockDelegateBackend(parent);
}

ITeammateBackend* MockBackendPlugin::createTeammateBackend(QObject* parent)
{
    return new MockTeammateBackend(parent);
}
```



### 步骤 5：创建元数据和构建配置

创建 `mock_backend.json`：

```json
{
  "pluginId": "mock_backend",
  "displayName": "Mock Backend Plugin",
  "version": "1.0.0",
  "sdkVersion": "1.0.0",
  "category": "backend",
  "description": "A mock backend for testing and development",
  "author": "Your Name",
  "license": "MIT",
  "dependencies": {
    "qt": ">=5.15.0",
    "sdk": "^1.0.0"
  }
}
```

创建 `MockBackendPlugin.pro`：

```qmake
QT += core
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = MockBackendPlugin

# 设置 SDK 路径
TMAGENT_SDK_PATH = /path/to/tmagent-plugin-sdk
include($TMAGENT_SDK_PATH/tmagent-plugin-sdk.pri)

SOURCES += MockBackendPlugin.cpp
HEADERS += MockBackendPlugin.h

OTHER_FILES += mock_backend.json

CONFIG(debug, debug|release) {
    DESTDIR = $$OUT_PWD/debug
} else {
    DESTDIR = $$OUT_PWD/release
}

target.path = /usr/local/lib/tmagent/plugins/backends
INSTALLS += target
```

### 步骤 6：编译和测试

```bash
# 编译
qmake MockBackendPlugin.pro
make

# 安装到插件目录
cp libMockBackendPlugin.so ~/.local/share/TmAgent/plugins/backends/
cp mock_backend.json ~/.local/share/TmAgent/plugins/backends/
```

---

## 第三部分：高级主题

### 异步工具实现

某些工具需要长时间执行（如网络请求、文件处理）。SDK 支持异步工具模式：

```cpp
ToolResult LongRunningProvider::execute(const ToolCall& call)
{
    if (call.name == "download_file") {
        // 立即返回延迟标记
        QString url = call.input.value("url").toString();
        
        // 启动后台任务
        QNetworkAccessManager* manager = new QNetworkAccessManager(this);
        QNetworkReply* reply = manager->get(QNetworkRequest(QUrl(url)));
        
        connect(reply, &QNetworkReply::finished, this, [this, call, reply]() {
            ToolResult result;
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray data = reply->readAll();
                result = ToolResult(
                    QString("Downloaded %1 bytes").arg(data.size()),
                    "Download completed",
                    true
                );
            } else {
                result = ToolResult(
                    QString("Error: %1").arg(reply->errorString()),
                    "Download failed",
                    false
                );
            }
            
            // 发出完成信号
            emit toolCompleted(call.id, result);
            reply->deleteLater();
        });
        
        // 返回延迟标记
        return ToolResult(
            "__DEFERRED__Downloading file...",
            "Download in progress",
            true
        );
    }
    
    return ToolResult("Unknown tool", "Error", false);
}
```

### 使用宿主服务

插件可以通过 `IToolPluginHost` 访问主应用提供的服务：

```cpp
ToolResult MyProvider::execute(const ToolCall& call)
{
    // 记录日志
    m_host->logInfo("my_plugin", "Executing tool: " + call.name);
    
    // 调用其他工具
    ToolCall otherCall;
    otherCall.name = "read_file";
    otherCall.input = QJsonObject{{"path", "/tmp/data.txt"}};
    ToolResult fileContent = m_host->executeHostTool(otherCall);
    
    // 读取配置
    QJsonObject config = m_host->getPluginConfig("my_plugin");
    QString apiKey = config.value("api_key").toString();
    
    // 获取数据目录
    QString dataDir = m_host->getPluginDataDir("my_plugin");
    
    // 查询可用后端
    QStringList backends = m_host->availableTeammateBackendIds();
    
    // ... 使用这些服务实现工具逻辑
}
```

### 工具 Schema 高级用法

使用 `ToolSchemaBuilder` 创建复杂的参数 Schema：

```cpp
// 枚举类型
QJsonObject modeProperty = makePropertySchema("string", "Operation mode");
modeProperty["enum"] = QJsonArray{"fast", "normal", "accurate"};

// 数字范围
QJsonObject countProperty = makePropertySchema("number", "Item count");
countProperty["minimum"] = 1;
countProperty["maximum"] = 100;

// 数组类型
QJsonObject tagsProperty = makePropertySchema("array", "Tags");
tagsProperty["items"] = QJsonObject{{"type", "string"}};

// 嵌套对象
QJsonObject configProperty = makePropertySchema("object", "Configuration");
configProperty["properties"] = QJsonObject{
    {"timeout", makePropertySchema("number", "Timeout in seconds")},
    {"retries", makePropertySchema("number", "Retry count")}
};

// 组合成完整 Schema
Tool complexTool;
complexTool.name = "complex_operation";
complexTool.description = "A complex operation with advanced parameters";
complexTool.inputSchema = makeToolSchema(
    "complex_operation",
    "A complex operation",
    QJsonObject{
        {"mode", modeProperty},
        {"count", countProperty},
        {"tags", tagsProperty},
        {"config", configProperty}
    },
    QStringList{"mode", "count"}  // 必需参数
)["function"].toObject()["parameters"].toObject();
```



### 错误处理最佳实践

始终提供清晰的错误信息和错误码：

```cpp
ToolResult MyProvider::execute(const ToolCall& call)
{
    // 参数验证
    if (!call.input.contains("required_param")) {
        return ToolResult(
            "Error: missing required parameter 'required_param'",
            "Parameter error",
            false,
            QJsonObject{
                {"errorCode", "missing_parameter"},
                {"parameter", "required_param"}
            }
        );
    }
    
    // 权限检查
    if (!hasPermission(call.input)) {
        return ToolResult(
            "Error: insufficient permissions",
            "Permission denied",
            false,
            QJsonObject{
                {"errorCode", "permission_denied"},
                {"requiredPermission", "filesystem.write"}
            }
        );
    }
    
    // 执行操作
    try {
        QString result = performOperation(call.input);
        return ToolResult(result, "Success", true);
    } catch (const std::exception& e) {
        // 捕获异常并转换为 ToolResult
        m_host->logError("my_plugin", 
            QString("Exception: %1").arg(e.what()));
        
        return ToolResult(
            QString("Internal error: %1").arg(e.what()),
            "Execution failed",
            false,
            QJsonObject{
                {"errorCode", "execution_failed"},
                {"exception", e.what()}
            }
        );
    }
}
```

### 配置管理

支持用户配置的插件示例：

```cpp
// 在插件描述符中声明配置 Schema
ToolPluginDescriptor MyPlugin::descriptor() const
{
    ToolPluginDescriptor desc;
    desc.pluginId = "my_plugin";
    // ... 其他字段
    
    // 定义配置 Schema
    desc.configSchema = QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"api_key", QJsonObject{
                {"type", "string"},
                {"description", "API key for external service"}
            }},
            {"timeout", QJsonObject{
                {"type", "number"},
                {"description", "Request timeout in seconds"},
                {"default", 30}
            }},
            {"enable_cache", QJsonObject{
                {"type", "boolean"},
                {"description", "Enable response caching"},
                {"default", true}
            }}
        }},
        {"required", QJsonArray{"api_key"}}
    };
    
    return desc;
}

// 实现配置验证
bool MyPlugin::configureProvider(IToolProvider* provider,
                                const QJsonObject& config,
                                QString* error)
{
    // 验证必需字段
    if (!config.contains("api_key") || 
        config.value("api_key").toString().isEmpty()) {
        if (error) {
            *error = "Missing required configuration: api_key";
        }
        return false;
    }
    
    // 应用配置到 Provider
    MyProvider* myProvider = qobject_cast<MyProvider*>(provider);
    if (myProvider) {
        myProvider->setApiKey(config.value("api_key").toString());
        myProvider->setTimeout(config.value("timeout").toInt(30));
        myProvider->setEnableCache(config.value("enable_cache").toBool(true));
    }
    
    return true;
}
```

### 健康检查实现

实现插件健康检查以便主应用监控插件状态：

```cpp
ToolPluginHealth MyPlugin::health(const IToolProvider* provider) const
{
    ToolPluginHealth health;
    
    const MyProvider* myProvider = 
        qobject_cast<const MyProvider*>(provider);
    
    if (!myProvider) {
        health.status = "unhealthy";
        health.diagnostics = "Provider instance is invalid";
        return health;
    }
    
    // 检查外部服务连接
    if (!myProvider->isConnected()) {
        health.status = "degraded";
        health.diagnostics = "External service connection lost";
        return health;
    }
    
    // 检查缓存状态
    if (myProvider->getCacheSize() > 1000000) {
        health.status = "degraded";
        health.diagnostics = "Cache size exceeds threshold";
        return health;
    }
    
    health.status = "healthy";
    health.diagnostics = "All systems operational";
    return health;
}
```

---

## 常见问题

### Q1: 插件加载失败，如何调试？

**A:** 检查以下几点：

1. **查看日志**：TmAgent 会记录插件加载错误到日志文件
2. **验证 SDK 版本**：确保插件使用的 SDK 版本与主应用兼容
3. **检查依赖**：确保所有依赖库（Qt、第三方库）都已安装
4. **验证元数据**：确保 JSON 元数据文件格式正确
5. **检查符号导出**：确保插件正确导出了 Qt 插件接口

调试命令：

```bash
# Linux: 检查动态库依赖
ldd libMyPlugin.so

# macOS: 检查动态库依赖
otool -L libMyPlugin.dylib

# Windows: 使用 Dependency Walker 检查 DLL 依赖
```

### Q2: 如何在插件中使用第三方库？

**A:** 在构建配置中添加第三方库：

```qmake
# qmake 示例
LIBS += -L/path/to/lib -lmylib
INCLUDEPATH += /path/to/include
```

```cmake
# CMake 示例
find_package(MyLib REQUIRED)
target_link_libraries(MyPlugin PRIVATE MyLib::MyLib)
```

注意：确保第三方库在运行时可被找到（设置 LD_LIBRARY_PATH 或将库放在系统路径）。

### Q3: 插件可以访问主应用的内部 API 吗？

**A:** 不可以。插件只能通过 SDK 定义的接口与主应用交互。这是设计上的隔离，确保：
- ABI 稳定性
- 插件独立性
- 安全性

如果需要访问主应用功能，应该通过 `IToolPluginHost` 回调接口请求。

### Q4: 如何处理插件崩溃？

**A:** 最佳实践：

1. **异常边界**：在所有公开接口方法中使用 try-catch
2. **资源管理**：使用 RAII 和智能指针管理资源
3. **防御性编程**：验证所有输入参数
4. **日志记录**：记录详细的错误信息

```cpp
ToolResult MyProvider::execute(const ToolCall& call)
{
    try {
        // 验证输入
        if (!validateInput(call.input)) {
            return ToolResult("Invalid input", "Error", false);
        }
        
        // 执行操作
        return performOperation(call);
        
    } catch (const std::exception& e) {
        // 记录异常
        if (m_host) {
            m_host->logError("my_plugin", 
                QString("Exception in execute: %1").arg(e.what()));
        }
        
        // 返回错误结果而不是崩溃
        return ToolResult(
            QString("Internal error: %1").arg(e.what()),
            "Execution failed",
            false,
            QJsonObject{{"errorCode", "plugin_exception"}}
        );
    } catch (...) {
        // 捕获所有异常
        if (m_host) {
            m_host->logError("my_plugin", "Unknown exception in execute");
        }
        return ToolResult("Unknown error", "Error", false);
    }
}
```

### Q5: 插件可以创建线程吗？

**A:** 可以，但需要注意：

1. **线程安全**：确保跨线程访问的数据有适当的同步
2. **Qt 信号槽**：使用 Qt::QueuedConnection 跨线程通信
3. **资源清理**：在插件卸载前确保所有线程已停止
4. **回调线程**：调用 IToolPluginHost 方法时确保在主线程

```cpp
class MyProvider : public QObject, public IToolProvider {
    Q_OBJECT
public:
    ToolResult execute(const ToolCall& call) override {
        // 启动后台线程
        QThread* thread = QThread::create([this, call]() {
            // 后台处理
            QString result = heavyComputation(call.input);
            
            // 使用信号返回结果（自动跨线程）
            emit computationFinished(call.id, result);
        });
        
        connect(thread, &QThread::finished, thread, &QThread::deleteLater);
        thread->start();
        
        // 返回延迟标记
        return ToolResult("__DEFERRED__Processing...", "In progress", true);
    }

signals:
    void computationFinished(const QString& callId, const QString& result);
};
```

### Q6: 如何测试插件？

**A:** 推荐的测试策略：

1. **单元测试**：使用 Qt Test 框架测试 Provider 逻辑
2. **集成测试**：在 TmAgent 中加载插件并测试端到端流程
3. **模拟测试**：创建 IToolPluginHost 的模拟实现

单元测试示例：

```cpp
#include <QtTest>
#include "MyProvider.h"

class MyProviderTest : public QObject {
    Q_OBJECT
private slots:
    void testExecute() {
        MyProvider provider(nullptr);
        
        ToolCall call;
        call.name = "my_tool";
        call.input = QJsonObject{{"param", "value"}};
        
        ToolResult result = provider.execute(call);
        
        QVERIFY(result.success);
        QCOMPARE(result.rawContent, "expected output");
    }
};

QTEST_MAIN(MyProviderTest)
#include "MyProviderTest.moc"
```



---

## 最佳实践

### 1. 接口设计原则

**保持接口简单**：
- 每个工具只做一件事
- 参数数量控制在 5 个以内
- 使用清晰的命名

**提供详细的描述**：
```cpp
Tool tool;
tool.name = "search_files";
tool.description = "Search for files in a directory matching a pattern. "
                  "Supports glob patterns like *.txt or file?.doc. "
                  "Returns a list of matching file paths.";
```

**使用类型安全的 Schema**：
```cpp
// 明确指定类型和约束
QJsonObject schema = makeToolSchema(
    "create_user",
    "Create a new user account",
    QJsonObject{
        {"username", makePropertySchema("string", "Username (3-20 chars)")},
        {"email", makePropertySchema("string", "Email address")},
        {"age", makePropertySchema("number", "Age (must be 18+)")}
    },
    QStringList{"username", "email"}
);

// 添加验证规则
schema["properties"].toObject()["username"].toObject()["pattern"] = "^[a-zA-Z0-9_]{3,20}$";
schema["properties"].toObject()["age"].toObject()["minimum"] = 18;
```

### 2. 错误处理策略

**使用标准错误码**：
```cpp
// 定义错误码常量
namespace ErrorCodes {
    const QString UNKNOWN_TOOL = "unknown_tool";
    const QString MISSING_PARAMETER = "missing_parameter";
    const QString INVALID_PARAMETER = "invalid_parameter";
    const QString PERMISSION_DENIED = "permission_denied";
    const QString TIMEOUT = "timeout";
    const QString EXECUTION_FAILED = "execution_failed";
}

// 使用错误码
return ToolResult(
    "Error: file not found",
    "File error",
    false,
    QJsonObject{
        {"errorCode", ErrorCodes::EXECUTION_FAILED},
        {"path", filePath}
    }
);
```

**提供可操作的错误信息**：
```cpp
// ✗ 不好：模糊的错误信息
return ToolResult("Error", "Failed", false);

// ✓ 好：清晰的错误信息和恢复建议
return ToolResult(
    "Error: Cannot write to file '/tmp/data.txt'. "
    "Permission denied. Please check file permissions or "
    "specify a different output path.",
    "Permission denied",
    false,
    QJsonObject{
        {"errorCode", "permission_denied"},
        {"path", "/tmp/data.txt"},
        {"suggestion", "Check file permissions or use a different path"}
    }
);
```

### 3. 性能优化

**延迟初始化**：
```cpp
class MyProvider : public QObject, public IToolProvider {
public:
    ToolResult execute(const ToolCall& call) override {
        // 延迟初始化重量级资源
        if (!m_heavyResource) {
            m_heavyResource = initializeHeavyResource();
        }
        
        return m_heavyResource->process(call);
    }

private:
    std::unique_ptr<HeavyResource> m_heavyResource;
};
```

**缓存结果**：
```cpp
class CachingProvider : public QObject, public IToolProvider {
public:
    ToolResult execute(const ToolCall& call) override {
        // 生成缓存键
        QString cacheKey = generateCacheKey(call);
        
        // 检查缓存
        if (m_cache.contains(cacheKey)) {
            m_host->logDebug("my_plugin", "Cache hit for " + call.name);
            return m_cache[cacheKey];
        }
        
        // 执行并缓存
        ToolResult result = performOperation(call);
        m_cache[cacheKey] = result;
        
        return result;
    }

private:
    QMap<QString, ToolResult> m_cache;
};
```

**批量操作**：
```cpp
// 提供批量版本的工具
Tool batchTool;
batchTool.name = "process_files_batch";
batchTool.description = "Process multiple files in one call";
batchTool.inputSchema = makeToolSchema(
    "process_files_batch",
    "Process multiple files",
    QJsonObject{
        {"files", QJsonObject{
            {"type", "array"},
            {"items", QJsonObject{{"type", "string"}}},
            {"description", "List of file paths"}
        }}
    },
    QStringList{"files"}
)["function"].toObject()["parameters"].toObject();
```

### 4. 安全性考虑

**输入验证**：
```cpp
ToolResult MyProvider::execute(const ToolCall& call)
{
    // 验证工具名称
    if (!isValidToolName(call.name)) {
        return ToolResult("Invalid tool name", "Error", false);
    }
    
    // 验证参数类型
    if (!call.input.contains("path") || 
        !call.input["path"].isString()) {
        return ToolResult("Invalid parameter type", "Error", false);
    }
    
    // 验证路径安全性（防止路径遍历攻击）
    QString path = call.input["path"].toString();
    if (path.contains("..") || path.startsWith("/etc/")) {
        return ToolResult("Invalid path", "Security error", false);
    }
    
    // 限制字符串长度
    if (path.length() > 4096) {
        return ToolResult("Path too long", "Error", false);
    }
    
    // 执行操作
    return performOperation(path);
}
```

**资源限制**：
```cpp
class ResourceLimitedProvider : public QObject, public IToolProvider {
public:
    ToolResult execute(const ToolCall& call) override {
        // 限制并发执行数
        if (m_activeExecutions >= MAX_CONCURRENT) {
            return ToolResult(
                "Too many concurrent executions",
                "Rate limited",
                false,
                QJsonObject{{"errorCode", "rate_limited"}}
            );
        }
        
        m_activeExecutions++;
        
        // 设置超时
        QTimer::singleShot(30000, this, [this, call]() {
            // 超时处理
            m_activeExecutions--;
            emit toolCompleted(call.id, ToolResult(
                "Execution timeout",
                "Timeout",
                false
            ));
        });
        
        // 执行操作
        ToolResult result = performOperation(call);
        m_activeExecutions--;
        
        return result;
    }

private:
    static const int MAX_CONCURRENT = 5;
    int m_activeExecutions = 0;
};
```

### 5. 日志记录

**使用分级日志**：
```cpp
void MyProvider::execute(const ToolCall& call)
{
    // Debug: 详细的调试信息
    m_host->logDebug("my_plugin", 
        QString("Executing %1 with params: %2")
            .arg(call.name)
            .arg(QString::fromUtf8(
                QJsonDocument(call.input).toJson())));
    
    // Info: 重要的操作信息
    m_host->logInfo("my_plugin", 
        QString("Processing file: %1")
            .arg(call.input["path"].toString()));
    
    // Warning: 可恢复的问题
    if (fileSize > 10000000) {
        m_host->logWarning("my_plugin", 
            "Large file detected, processing may be slow");
    }
    
    // Error: 错误情况
    if (!fileExists) {
        m_host->logError("my_plugin", 
            QString("File not found: %1")
                .arg(call.input["path"].toString()));
    }
}
```

**记录性能指标**：
```cpp
ToolResult MyProvider::execute(const ToolCall& call)
{
    QElapsedTimer timer;
    timer.start();
    
    ToolResult result = performOperation(call);
    
    qint64 elapsed = timer.elapsed();
    m_host->logInfo("my_plugin", 
        QString("Tool %1 completed in %2 ms")
            .arg(call.name)
            .arg(elapsed));
    
    // 性能警告
    if (elapsed > 5000) {
        m_host->logWarning("my_plugin", 
            QString("Slow execution detected: %1 ms").arg(elapsed));
    }
    
    return result;
}
```

### 6. 文档和注释

**编写清晰的代码注释**：
```cpp
/**
 * @brief 执行文件搜索操作
 * 
 * 在指定目录中搜索匹配模式的文件。支持递归搜索和多种模式匹配。
 * 
 * @param call 工具调用请求，必须包含以下参数：
 *   - directory (string): 搜索的起始目录
 *   - pattern (string): 文件名匹配模式（支持 glob）
 *   - recursive (boolean, optional): 是否递归搜索子目录，默认 false
 *   - max_results (number, optional): 最大结果数，默认 100
 * 
 * @return ToolResult 包含匹配文件路径列表的结果
 *   成功时 rawContent 为 JSON 数组格式的文件路径列表
 *   失败时包含错误信息和错误码
 * 
 * @note 搜索深度限制为 10 层，防止无限递归
 * @note 大型目录搜索可能耗时较长，建议使用异步模式
 */
ToolResult FileSearchProvider::execute(const ToolCall& call)
{
    // 实现...
}
```

**提供示例代码**：
在 README.md 中包含使用示例：

```markdown
## 使用示例

### 搜索文件

```json
{
  "name": "search_files",
  "input": {
    "directory": "/home/user/projects",
    "pattern": "*.cpp",
    "recursive": true,
    "max_results": 50
  }
}
```

响应：

```json
{
  "success": true,
  "rawContent": "['/home/user/projects/main.cpp', '/home/user/projects/src/utils.cpp']",
  "data": {
    "count": 2,
    "truncated": false
  }
}
```
```

### 7. 版本管理

**遵循语义化版本**：
- 主版本号：不兼容的 API 变更
- 次版本号：向后兼容的功能新增
- 补丁版本号：向后兼容的 Bug 修复

**维护变更日志**：
```markdown
# Changelog

## [1.1.0] - 2024-01-15
### Added
- New tool: batch_process for processing multiple files
- Support for custom timeout configuration

### Changed
- Improved error messages for file operations
- Updated to SDK 1.1.0

### Fixed
- Fixed memory leak in cache implementation
- Fixed crash when processing empty files

## [1.0.0] - 2024-01-01
### Added
- Initial release
- Basic file operations: read, write, search
```

---

## 下一步

恭喜！您已经学会了如何创建 TmAgent 插件。接下来可以：

1. **查看示例代码**：研究 `examples/` 目录中的完整示例
2. **阅读 API 文档**：查看 `docs/API.md` 了解所有接口细节
3. **参考官方插件**：学习 TmAgent 官方插件的实现
4. **加入社区**：在 GitHub 上分享您的插件

## 相关资源

- [API 参考文档](API.md)
- [迁移指南](MIGRATION.md)
- [SDK GitHub 仓库](https://github.com/tmagent/plugin-sdk)
- [官方插件示例](https://github.com/tmagent/plugins)

## 获取帮助

如果遇到问题：

1. 查看[常见问题](#常见问题)部分
2. 搜索 [GitHub Issues](https://github.com/tmagent/plugin-sdk/issues)
3. 在社区论坛提问
4. 提交新的 Issue

祝您开发愉快！
