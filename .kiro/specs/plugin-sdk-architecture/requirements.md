# 需求文档：TmAgent 插件 SDK 架构

## 引言

本文档定义了 TmAgent 插件 SDK 架构重构的功能和非功能需求。该重构旨在将紧耦合的插件系统转变为独立的 SDK 架构，使第三方开发者能够在不依赖主应用源码的情况下开发和分发插件。

## 术语表

- **SDK (Software Development Kit)**: 软件开发工具包，提供插件开发所需的接口定义和辅助工具
- **Plugin**: 插件，实现 SDK 接口的动态库，可在运行时被主应用加载
- **Tool_Plugin**: 工具插件，提供可被 AI Agent 调用的工具功能
- **Backend_Plugin**: 后端插件，提供 AI 模型接口实现
- **Provider**: 提供者，实现具体工具或后端逻辑的类
- **Host**: 宿主应用，即 TmAgent 主应用，负责加载和管理插件
- **ABI (Application Binary Interface)**: 应用程序二进制接口，定义编译后代码的兼容性
- **POD (Plain Old Data)**: 简单数据结构，不包含虚函数和复杂构造函数
- **Descriptor**: 描述符，包含插件元数据的数据结构
- **ToolCall**: 工具调用请求，包含工具名称和输入参数
- **ToolResult**: 工具执行结果，包含输出内容和执行状态
- **Delegate_Backend**: 委托后端，用于子任务委托的 AI 后端
- **Teammate_Backend**: 队友后端，用于持久化协作的 AI 后端

## 需求

### 需求 1: SDK 项目独立性

**用户故事**: 作为第三方插件开发者，我希望能够仅依赖轻量级 SDK 开发插件，而无需克隆整个 TmAgent 项目，以便快速开始开发。

#### 验收标准

1. THE SDK SHALL 作为独立项目存在，与主应用代码库分离
2. THE SDK SHALL 仅依赖 Qt Core 模块
3. THE SDK SHALL 包含大小小于 100KB
4. WHEN 开发者下载 SDK THEN THE SDK SHALL 包含所有必需的头文件、构建配置和示例代码
5. THE SDK SHALL 提供 qmake 和 CMake 两种构建系统支持


### 需求 2: 插件接口定义

**用户故事**: 作为插件开发者，我希望有清晰的接口定义，以便知道如何实现插件功能。

#### 验收标准

1. THE SDK SHALL 定义 IToolPlugin 接口用于工具插件
2. THE SDK SHALL 定义 IBackendPlugin 接口用于后端插件
3. THE SDK SHALL 定义 IToolProvider 接口用于工具提供者
4. THE SDK SHALL 定义 IDelegateBackend 接口用于委托后端
5. THE SDK SHALL 定义 ITeammateBackend 接口用于队友后端
6. WHEN 插件实现接口 THEN THE SDK SHALL 使用纯虚函数定义，不包含数据成员
7. THE SDK SHALL 为每个接口提供 Qt 插件系统所需的 IID 宏定义

### 需求 3: 数据结构定义

**用户故事**: 作为插件开发者，我希望使用标准化的数据结构与主应用交换数据，以确保兼容性。

#### 验收标准

1. THE SDK SHALL 定义 Tool 结构用于描述工具定义
2. THE SDK SHALL 定义 ToolCall 结构用于传递工具调用请求
3. THE SDK SHALL 定义 ToolResult 结构用于返回工具执行结果
4. THE SDK SHALL 定义 ToolPluginDescriptor 结构用于描述插件元数据
5. THE SDK SHALL 定义 BackendDescriptor 结构用于描述后端元数据
6. THE SDK SHALL 定义 AgentConfig 结构用于传递 Agent 配置
7. THE SDK SHALL 定义 TeammateConfig 结构用于传递队友配置
8. THE SDK SHALL 定义 TeammateState 结构用于传递队友状态
9. WHEN 定义数据结构 THEN THE SDK SHALL 使用 POD 类型，避免虚函数表
10. WHEN 数据结构包含 JSON 字段 THEN THE SDK SHALL 提供 toJson 和 fromJson 方法

### 需求 4: 宿主回调服务

**用户故事**: 作为插件开发者，我希望能够调用主应用提供的服务，以便实现复杂的插件功能。

#### 验收标准

1. THE SDK SHALL 定义 IToolPluginHost 接口用于宿主回调
2. WHEN 插件需要查询可用后端 THEN THE IToolPluginHost SHALL 提供 availableTeammateBackendIds 方法
3. WHEN 插件需要调用其他工具 THEN THE IToolPluginHost SHALL 提供 executeHostTool 方法
4. WHEN 插件需要记录日志 THEN THE IToolPluginHost SHALL 提供 logDebug、logInfo、logWarning、logError 方法
5. WHEN 插件需要读取配置 THEN THE IToolPluginHost SHALL 提供 getPluginConfig 方法
6. WHEN 插件需要保存配置 THEN THE IToolPluginHost SHALL 提供 setPluginConfig 方法
7. WHEN 插件需要访问数据目录 THEN THE IToolPluginHost SHALL 提供 getPluginDataDir 方法
8. WHERE 插件需要代码解析服务 THEN THE IToolPluginHost SHALL 提供 parseCode 方法


### 需求 5: 版本兼容性管理

**用户故事**: 作为插件开发者，我希望了解 SDK 版本兼容性规则，以便确保我的插件能在不同版本的主应用中运行。

#### 验收标准

1. THE SDK SHALL 使用语义化版本控制（MAJOR.MINOR.PATCH）
2. WHEN SDK 主版本号变更 THEN THE SDK SHALL 表示 ABI 不兼容变更
3. WHEN SDK 次版本号变更 THEN THE SDK SHALL 表示 ABI 兼容的功能新增
4. WHEN SDK 补丁版本号变更 THEN THE SDK SHALL 表示 Bug 修复或文档更新
5. THE SDK SHALL 在 version.h 中定义版本号宏
6. WHEN 插件声明 SDK 版本 THEN THE ToolPluginDescriptor SHALL 包含 sdkVersionMajor 和 sdkVersionMinor 字段
7. WHEN 插件声明 SDK 版本 THEN THE BackendDescriptor SHALL 包含 sdkVersionMajor 和 sdkVersionMinor 字段

### 需求 6: 插件加载机制

**用户故事**: 作为主应用开发者，我希望主应用能够自动发现和加载插件，以便用户可以方便地扩展功能。

#### 验收标准

1. WHEN 主应用启动 THEN THE PluginManager SHALL 扫描预定义的插件目录
2. THE PluginManager SHALL 按优先级顺序搜索：应用内置目录、用户插件目录、系统插件目录
3. WHEN 发现动态库文件 THEN THE PluginManager SHALL 使用 QPluginLoader 加载
4. WHEN 加载插件 THEN THE PluginManager SHALL 验证插件实现了 IToolPlugin 或 IBackendPlugin 接口
5. WHEN 加载插件 THEN THE PluginManager SHALL 调用 descriptor 方法获取元数据
6. WHEN 获取插件元数据 THEN THE PluginManager SHALL 执行版本兼容性检查
7. IF 插件主版本号与 SDK 主版本号不匹配 THEN THE PluginManager SHALL 拒绝加载该插件
8. IF 插件次版本号大于 SDK 次版本号 THEN THE PluginManager SHALL 拒绝加载该插件
9. WHEN 插件兼容 THEN THE PluginManager SHALL 调用 createProvider 方法创建提供者实例
10. WHEN 插件加载失败 THEN THE PluginManager SHALL 记录错误日志并继续加载其他插件

### 需求 7: 工具调用执行

**用户故事**: 作为 AI Agent，我希望能够调用插件提供的工具，以便完成用户任务。

#### 验收标准

1. WHEN Agent 收到 LLM 的工具调用请求 THEN THE ToolDispatcher SHALL 将请求路由到对应的插件
2. WHEN 路由工具调用 THEN THE ToolDispatcher SHALL 根据工具名称查找对应的 Provider
3. WHEN 找到 Provider THEN THE ToolDispatcher SHALL 调用 Provider 的 execute 方法
4. WHEN 工具执行完成 THEN THE Provider SHALL 返回 ToolResult 结构
5. WHEN 工具执行成功 THEN THE ToolResult SHALL 包含 success=true 和结果内容
6. WHEN 工具执行失败 THEN THE ToolResult SHALL 包含 success=false 和错误信息
7. WHEN 工具支持异步执行 THEN THE Provider SHALL 返回包含 "__DEFERRED__" 前缀的 ToolResult
8. WHEN 异步工具完成 THEN THE Provider SHALL 发出 toolCompleted 信号通知主应用


### 需求 8: 错误处理和隔离

**用户故事**: 作为主应用开发者，我希望插件错误不会影响主应用和其他插件，以确保系统稳定性。

#### 验收标准

1. WHEN 插件加载失败 THEN THE PluginManager SHALL 记录错误并继续加载其他插件
2. WHEN 插件抛出 C++ 异常 THEN THE ToolDispatcher SHALL 在插件边界捕获异常
3. WHEN 捕获插件异常 THEN THE ToolDispatcher SHALL 将异常转换为 ToolResult{success=false}
4. WHEN 插件异常被捕获 THEN THE ToolDispatcher SHALL 记录详细堆栈信息到日志
5. WHEN 一个插件发生错误 THEN THE Host SHALL 确保其他插件继续正常工作
6. WHEN 工具调用参数缺失 THEN THE Provider SHALL 返回包含 "missing_parameter" 错误码的 ToolResult
7. WHEN 工具调用参数格式错误 THEN THE Provider SHALL 返回包含 "invalid_parameter" 错误码的 ToolResult
8. WHEN 工具执行超时 THEN THE Provider SHALL 返回包含 "timeout" 错误码的 ToolResult
9. WHEN 工具执行失败 THEN THE ToolResult SHALL 在 data 字段中包含错误码和诊断信息

### 需求 9: 依赖解耦

**用户故事**: 作为 SDK 架构师，我希望插件不直接依赖主应用的内部实现，以确保插件可以独立编译。

#### 验收标准

1. THE SDK SHALL 不依赖 src/core/ 目录下的任何头文件
2. THE SDK SHALL 不依赖 llm/ 目录下的任何头文件
3. WHEN 插件需要访问 ToolDispatcher 功能 THEN THE SDK SHALL 通过 IToolExecutor 回调接口提供
4. WHEN 插件需要访问 Teammate 数据 THEN THE SDK SHALL 使用 TeammateConfig 和 TeammateState POD 结构
5. WHEN 插件需要访问 LLMConfig 数据 THEN THE SDK SHALL 使用 AgentConfig 结构
6. WHEN 插件需要访问 ModelFactory 功能 THEN THE SDK SHALL 通过 IModelFactory 回调接口提供
7. WHEN 后端插件需要工具执行能力 THEN THE DelegateRequest SHALL 包含 IToolExecutor 接口指针而非 ToolDispatcher 指针
8. WHEN 后端插件需要模型创建能力 THEN THE DelegateRequest SHALL 包含 IModelFactory 接口指针而非 ModelFactory 指针

### 需求 10: 插件元数据管理

**用户故事**: 作为主应用开发者，我希望能够获取插件的元数据信息，以便在 UI 中展示和管理插件。

#### 验收标准

1. WHEN 插件被加载 THEN THE Plugin SHALL 通过 descriptor 方法返回元数据
2. THE ToolPluginDescriptor SHALL 包含 pluginId、displayName、version、description、category 字段
3. THE ToolPluginDescriptor SHALL 包含 toolNames 列表和 configSchema 对象
4. THE BackendDescriptor SHALL 包含 backendId、displayName、version 字段
5. THE BackendDescriptor SHALL 包含 supportsDelegate 和 supportsTeammate 布尔标志
6. WHEN 验证插件元数据 THEN THE Descriptor SHALL 提供 isValid 方法检查必需字段
7. THE ToolPluginDescriptor SHALL 包含 sdkVersionMajor 和 sdkVersionMinor 字段用于版本检查
8. THE BackendDescriptor SHALL 包含 sdkVersionMajor 和 sdkVersionMinor 字段用于版本检查


### 需求 11: 工具 Schema 构建

**用户故事**: 作为插件开发者，我希望有辅助工具帮助我构建 JSON Schema，以便快速定义工具参数。

#### 验收标准

1. THE SDK SHALL 提供 ToolSchemaBuilder 辅助类
2. THE ToolSchemaBuilder SHALL 提供 makePropertySchema 函数用于创建属性 Schema
3. THE ToolSchemaBuilder SHALL 提供 makeToolSchema 函数用于创建完整工具 Schema
4. WHEN 构建工具 Schema THEN THE makeToolSchema SHALL 接受工具名称、描述、属性对象和必需字段列表
5. WHEN 构建属性 Schema THEN THE makePropertySchema SHALL 接受类型和描述参数
6. THE ToolSchemaBuilder SHALL 支持常见 JSON Schema 类型：string、number、boolean、object、array

### 需求 12: 插件生命周期管理

**用户故事**: 作为主应用开发者，我希望能够管理插件的生命周期，包括加载、配置、卸载。

#### 验收标准

1. WHEN 主应用启动 THEN THE PluginManager SHALL 自动加载所有兼容的插件
2. WHEN 插件加载成功 THEN THE PluginManager SHALL 调用 createProvider 创建提供者实例
3. WHEN 创建提供者 THEN THE PluginManager SHALL 传递 IToolPluginHost 接口指针
4. WHEN 创建提供者 THEN THE PluginManager SHALL 传递父对象指针用于内存管理
5. WHERE 插件支持配置 THEN THE PluginManager SHALL 调用 configureProvider 方法
6. WHERE 插件支持健康检查 THEN THE PluginManager SHALL 调用 health 方法
7. WHEN 主应用关闭 THEN THE PluginManager SHALL 通过 Qt 父子对象机制自动释放提供者实例
8. WHEN 插件被卸载 THEN THE PluginManager SHALL 确保所有相关资源被释放

### 需求 13: 跨平台兼容性

**用户故事**: 作为插件开发者，我希望我的插件能在不同操作系统上运行，以便覆盖更多用户。

#### 验收标准

1. THE SDK SHALL 支持 Windows 10 及以上版本
2. THE SDK SHALL 支持 Linux（Ubuntu 20.04 及以上）
3. THE SDK SHALL 支持 macOS 12 及以上版本
4. THE SDK SHALL 支持 Qt 5.15 及以上版本
5. THE SDK SHALL 支持 Qt 6.2 及以上版本
6. WHEN 在不同平台编译 THEN THE SDK SHALL 生成对应的动态库格式（.dll / .so / .dylib）
7. WHEN 使用不同编译器 THEN THE SDK SHALL 确保 ABI 兼容性（MSVC、GCC、Clang）


### 需求 14: 构建系统支持

**用户故事**: 作为插件开发者，我希望能够使用熟悉的构建系统编译插件，以便快速集成到现有工作流。

#### 验收标准

1. THE SDK SHALL 提供 tmagent-plugin-sdk.pri 文件用于 qmake 集成
2. THE SDK SHALL 提供 CMakeLists.txt 文件用于 CMake 集成
3. WHEN 使用 qmake THEN THE Plugin_Project SHALL 通过 include() 引入 SDK 配置
4. WHEN 使用 CMake THEN THE Plugin_Project SHALL 通过 find_package() 查找 SDK
5. WHEN 引入 SDK THEN THE Build_System SHALL 自动配置头文件搜索路径
6. WHEN 引入 SDK THEN THE Build_System SHALL 自动链接 Qt Core 依赖
7. THE SDK SHALL 提供 CMake 配置文件用于版本兼容性检查

### 需求 15: 委托后端功能

**用户故事**: 作为后端插件开发者，我希望实现委托后端功能，以便支持子任务委托场景。

#### 验收标准

1. THE IDelegateBackend SHALL 定义 backendId 方法返回后端标识
2. THE IDelegateBackend SHALL 定义 createSession 方法创建委托会话
3. WHEN 创建委托会话 THEN THE IDelegateBackend SHALL 接受 DelegateRequest 结构和 DelegateCallbacks 结构
4. THE DelegateRequest SHALL 包含任务描述、执行提示词、子 Agent 配置
5. THE DelegateRequest SHALL 包含 IToolExecutor 接口指针用于工具调用
6. THE DelegateRequest SHALL 包含 IModelFactory 接口指针用于模型创建
7. THE DelegateCallbacks SHALL 包含 onActivity、onSummary、onToolEvent、onStreamDelta、onSuccess、onFailure 回调函数
8. WHEN 委托会话创建成功 THEN THE IDelegateBackend SHALL 返回 IDelegateSession 接口指针
9. THE IDelegateSession SHALL 提供 start 方法启动会话
10. THE IDelegateSession SHALL 提供 cancel 方法取消会话

### 需求 16: 队友后端功能

**用户故事**: 作为后端插件开发者，我希望实现队友后端功能，以便支持持久化协作场景。

#### 验收标准

1. THE ITeammateBackend SHALL 定义 backendId 方法返回后端标识
2. THE ITeammateBackend SHALL 定义 ensureReady 方法确保后端就绪
3. THE ITeammateBackend SHALL 定义 isReady 方法查询后端状态
4. THE ITeammateBackend SHALL 定义 createSession 方法创建队友会话
5. WHEN 创建队友会话 THEN THE ITeammateBackend SHALL 接受队友 ID 和 TeammateConfig 结构
6. WHEN 创建会话成功 THEN THE ITeammateBackend SHALL 返回包含 threadId 的 CreateResult
7. THE ITeammateBackend SHALL 定义 sendMessage 方法发送消息
8. WHEN 发送消息成功 THEN THE ITeammateBackend SHALL 返回包含 turnId 的 SendResult
9. THE ITeammateBackend SHALL 定义 cancelTurn 方法取消当前回合
10. THE ITeammateBackend SHALL 定义 destroySession 方法销毁会话
11. THE ITeammateBackend SHALL 定义 shutdown 方法关闭后端


### 需求 17: 数据验证

**用户故事**: 作为主应用开发者，我希望验证跨插件边界的数据，以防止恶意或错误的数据导致系统问题。

#### 验收标准

1. WHEN 接收工具调用请求 THEN THE Host SHALL 验证工具名称存在
2. WHEN 接收工具调用请求 THEN THE Host SHALL 验证参数符合工具的 JSON Schema
3. WHEN 验证字符串参数 THEN THE Host SHALL 限制字符串长度不超过 1MB
4. WHEN 验证数组参数 THEN THE Host SHALL 限制数组大小在合理范围内
5. WHEN 验证插件元数据 THEN THE Host SHALL 调用 descriptor.isValid() 方法
6. IF 插件 ID 为空 THEN THE isValid SHALL 返回 false
7. IF 后端插件既不支持委托也不支持队友 THEN THE isValid SHALL 返回 false

### 需求 18: 性能要求

**用户故事**: 作为最终用户，我希望插件加载和工具调用快速响应，以获得流畅的使用体验。

#### 验收标准

1. WHEN 加载单个插件 THEN THE PluginManager SHALL 在 50 毫秒内完成
2. WHEN 并行加载 10 个插件 THEN THE PluginManager SHALL 在 200 毫秒内完成
3. WHEN 执行同步工具调用 THEN THE ToolDispatcher SHALL 在 10 毫秒内完成调度（不含业务逻辑时间）
4. WHEN 启动异步工具调用 THEN THE ToolDispatcher SHALL 在 5 毫秒内返回延迟标记
5. WHEN 加载插件 THEN THE Plugin SHALL 占用内存小于 5MB
6. THE SDK SHALL 支持延迟加载策略，仅在需要时加载插件
7. THE SDK SHALL 支持插件元数据缓存，避免重复读取

### 需求 19: 日志和调试支持

**用户故事**: 作为插件开发者，我希望能够记录日志到主应用，以便调试插件问题。

#### 验收标准

1. THE IToolPluginHost SHALL 提供 logDebug 方法用于调试日志
2. THE IToolPluginHost SHALL 提供 logInfo 方法用于信息日志
3. THE IToolPluginHost SHALL 提供 logWarning 方法用于警告日志
4. THE IToolPluginHost SHALL 提供 logError 方法用于错误日志
5. WHEN 插件调用日志方法 THEN THE Host SHALL 在日志中包含插件 ID
6. WHEN 插件调用日志方法 THEN THE Host SHALL 使用 Qt 标准日志系统（qDebug/qWarning/qCritical）
7. WHEN 插件加载失败 THEN THE PluginManager SHALL 记录失败原因到错误日志


### 需求 20: 配置管理

**用户故事**: 作为插件开发者，我希望能够读取和保存插件配置，以便支持用户自定义设置。

#### 验收标准

1. THE IToolPluginHost SHALL 提供 getPluginConfig 方法读取插件配置
2. THE IToolPluginHost SHALL 提供 setPluginConfig 方法保存插件配置
3. WHEN 读取插件配置 THEN THE Host SHALL 返回 QJsonObject 格式的配置数据
4. WHEN 保存插件配置 THEN THE Host SHALL 接受 QJsonObject 格式的配置数据
5. IF 保存配置失败 THEN THE setPluginConfig SHALL 返回 false 并设置错误信息
6. THE IToolPluginHost SHALL 提供 getPluginDataDir 方法返回插件专用数据目录
7. THE IToolPluginHost SHALL 提供 getAppDataDir 方法返回应用数据目录
8. WHERE 插件声明配置 Schema THEN THE ToolPluginDescriptor SHALL 包含 configSchema 字段

### 需求 21: 文档和示例

**用户故事**: 作为第三方开发者，我希望有完整的文档和示例代码，以便快速学习如何开发插件。

#### 验收标准

1. THE SDK SHALL 包含 README.md 文件提供快速开始指南
2. THE SDK SHALL 包含 docs/API.md 文件提供完整 API 参考
3. THE SDK SHALL 包含 docs/TUTORIAL.md 文件提供插件开发教程
4. THE SDK SHALL 包含 docs/MIGRATION.md 文件提供迁移指南
5. THE SDK SHALL 在 examples/ 目录提供最小工具插件示例
6. THE SDK SHALL 在 examples/ 目录提供最小后端插件示例
7. WHEN 示例代码被编译 THEN THE Example SHALL 成功生成可加载的插件
8. THE Tutorial SHALL 包含从零开始创建插件的完整步骤
9. THE API_Documentation SHALL 包含所有公开接口的详细说明
10. THE Migration_Guide SHALL 包含从旧接口迁移到 SDK 的步骤

### 需求 22: 主应用适配器实现

**用户故事**: 作为主应用开发者，我希望主应用能够适配 SDK 接口，以便支持基于 SDK 的插件。

#### 验收标准

1. THE Host SHALL 实现 IToolPluginHost 接口的所有方法
2. THE Host SHALL 实现 ToolExecutorAdapter 类桥接 ToolDispatcher
3. WHEN ToolExecutorAdapter 调用 executeToolSync THEN THE Adapter SHALL 调用 ToolDispatcher 的 execute 方法
4. WHEN ToolExecutorAdapter 调用 executeToolAsync THEN THE Adapter SHALL 调用 ToolDispatcher 的 executeAsync 方法
5. THE Host SHALL 实现 ConfigAdapter 类转换 LLMConfig 到 AgentConfig
6. WHEN 转换配置 THEN THE ConfigAdapter SHALL 映射所有必需字段
7. THE Host SHALL 实现 ModelFactoryAdapter 类桥接 ModelFactory
8. WHEN 后端插件请求创建模型 THEN THE ModelFactoryAdapter SHALL 调用 ModelFactory 的对应方法


### 需求 23: 向后兼容支持

**用户故事**: 作为主应用开发者，我希望在迁移期间同时支持旧接口和 SDK 接口，以确保平滑过渡。

#### 验收标准

1. WHEN 加载插件 THEN THE PluginManager SHALL 首先尝试 SDK 接口（TmAgent::IToolPlugin）
2. IF SDK 接口不可用 THEN THE PluginManager SHALL 回退到旧接口（IToolPlugin）
3. WHEN 使用旧接口 THEN THE PluginManager SHALL 记录警告日志
4. THE Host SHALL 实现 LegacyPluginAdapter 类桥接旧插件到 SDK 接口
5. WHEN 适配旧插件 THEN THE LegacyPluginAdapter SHALL 转换旧格式元数据到新格式
6. WHEN 适配旧插件 THEN THE LegacyPluginAdapter SHALL 标记 sdkVersionMajor=0 表示旧版本
7. WHILE 在双接口支持期 THEN THE Host SHALL 确保新旧插件都能正常工作

### 需求 24: 官方插件迁移

**用户故事**: 作为官方插件维护者，我希望将现有插件迁移到 SDK，以验证 SDK 的可用性。

#### 验收标准

1. THE Migration SHALL 包含所有官方工具插件：workspace、shell、codeintel、web、memory、scheduler、coordination
2. THE Migration SHALL 包含所有官方后端插件：codex、tmagent
3. WHEN 迁移插件 THEN THE Plugin SHALL 移除对 src/core/ 的直接依赖
4. WHEN 迁移插件 THEN THE Plugin SHALL 仅依赖 tmagent-plugin-sdk
5. WHEN 迁移插件 THEN THE Plugin SHALL 更新构建配置使用 SDK
6. WHEN 迁移完成 THEN THE Plugin SHALL 保持与迁移前相同的功能
7. WHEN 迁移完成 THEN THE Plugin SHALL 通过所有单元测试和集成测试
8. WHEN 迁移完成 THEN THE Plugin SHALL 可以独立编译，不依赖主应用源码

### 需求 25: 安全和权限控制

**用户故事**: 作为最终用户，我希望了解插件需要哪些权限，以便决定是否信任该插件。

#### 验收标准

1. WHERE 插件需要特定权限 THEN THE ToolPluginDescriptor SHALL 包含 requiredPermissions 字段
2. THE SDK SHALL 定义标准权限类型：filesystem.read、filesystem.write、network.access、process.execute、system.info
3. WHEN 首次加载插件 THEN THE Host SHALL 向用户显示插件请求的权限
4. WHEN 用户拒绝权限 THEN THE Host SHALL 拒绝加载该插件
5. WHEN 用户批准权限 THEN THE Host SHALL 记录用户的信任决策
6. WHERE 插件使用代码签名 THEN THE Host SHALL 验证签名有效性
7. IF 插件未签名 THEN THE Host SHALL 显示未签名警告


### 需求 26: 异步工具支持

**用户故事**: 作为插件开发者，我希望能够实现异步工具，以便处理长时间运行的操作而不阻塞主线程。

#### 验收标准

1. WHEN 工具需要异步执行 THEN THE Provider SHALL 返回包含 "__DEFERRED__" 前缀的 ToolResult
2. THE SDK SHALL 提供 isDeferredToolResult 函数检测延迟标记
3. THE SDK SHALL 提供 stripDeferredPrefix 函数移除延迟前缀
4. WHEN 返回延迟结果 THEN THE ToolResult.rawContent SHALL 以 "__DEFERRED__" 开头
5. WHEN 异步操作完成 THEN THE Provider SHALL 发出 toolCompleted 信号
6. WHEN 发出 toolCompleted 信号 THEN THE Provider SHALL 传递调用 ID 和最终 ToolResult
7. WHEN 主应用接收 toolCompleted 信号 THEN THE ToolDispatcher SHALL 将结果传递给 Agent

### 需求 27: 工具间调用

**用户故事**: 作为插件开发者，我希望能够调用主应用的其他工具，以便实现复合操作。

#### 验收标准

1. THE IToolPluginHost SHALL 提供 executeHostTool 方法用于工具间调用
2. WHEN 插件调用 executeHostTool THEN THE Host SHALL 接受 ToolCall 结构
3. WHEN 执行宿主工具 THEN THE Host SHALL 通过 ToolDispatcher 路由调用
4. WHEN 宿主工具执行完成 THEN THE Host SHALL 返回 ToolResult 结构
5. WHEN 宿主工具执行失败 THEN THE Host SHALL 返回包含错误信息的 ToolResult
6. THE IToolPluginHost SHALL 提供 availableTools 方法查询可用工具列表

### 需求 28: ABI 稳定性保证

**用户故事**: 作为插件开发者，我希望我的插件在 SDK 次版本更新后仍能工作，而无需重新编译。

#### 验收标准

1. WHEN 定义接口 THEN THE SDK SHALL 使用纯虚函数，不包含数据成员
2. WHEN 定义数据结构 THEN THE SDK SHALL 使用 POD 类型，避免虚函数表
3. WHEN 添加新接口方法 THEN THE SDK SHALL 在接口末尾添加，不改变现有方法顺序
4. WHEN 添加新数据字段 THEN THE SDK SHALL 在结构末尾添加，不改变现有字段顺序
5. WHEN SDK 次版本更新 THEN THE Host SHALL 能够加载使用旧次版本 SDK 编译的插件
6. WHEN SDK 主版本更新 THEN THE Host SHALL 拒绝加载使用旧主版本 SDK 编译的插件
7. THE SDK SHALL 在多个编译器（MSVC、GCC、Clang）上保持 ABI 兼容性


### 需求 29: 插件搜索路径

**用户故事**: 作为最终用户，我希望能够在不同位置安装插件，以便灵活管理插件。

#### 验收标准

1. THE PluginManager SHALL 按优先级顺序搜索插件目录
2. THE PluginManager SHALL 首先搜索应用内置插件目录（{APP_DIR}/plugins/）
3. THE PluginManager SHALL 其次搜索用户插件目录（{USER_DATA}/TmAgent/plugins/）
4. THE PluginManager SHALL 最后搜索系统插件目录（/usr/local/lib/tmagent/plugins/）
5. WHEN 多个目录包含同名插件 THEN THE PluginManager SHALL 加载优先级最高目录中的插件
6. THE PluginManager SHALL 在 plugins/tools/ 子目录搜索工具插件
7. THE PluginManager SHALL 在 plugins/backends/ 子目录搜索后端插件

### 需求 30: 插件元数据文件

**用户故事**: 作为插件开发者，我希望通过 JSON 文件声明插件元数据，以便主应用在加载前了解插件信息。

#### 验收标准

1. THE Plugin SHALL 提供与动态库同名的 JSON 元数据文件
2. THE Metadata_File SHALL 包含 pluginId、displayName、version、sdkVersion 字段
3. THE Metadata_File SHALL 包含 category、description、author、license 字段
4. WHERE 插件有依赖 THEN THE Metadata_File SHALL 包含 dependencies 对象
5. THE Metadata_File SHALL 使用 JSON 格式
6. WHEN 解析元数据文件失败 THEN THE PluginManager SHALL 记录错误并跳过该插件

### 需求 31: 错误码标准化

**用户故事**: 作为 AI Agent，我希望工具错误使用标准化的错误码，以便我能够理解错误原因并采取适当行动。

#### 验收标准

1. WHEN 工具不存在 THEN THE ToolResult SHALL 包含 "unknown_tool" 错误码
2. WHEN 缺少必需参数 THEN THE ToolResult SHALL 包含 "missing_parameter" 错误码
3. WHEN 参数格式错误 THEN THE ToolResult SHALL 包含 "invalid_parameter" 错误码
4. WHEN 权限不足 THEN THE ToolResult SHALL 包含 "permission_denied" 错误码
5. WHEN 执行超时 THEN THE ToolResult SHALL 包含 "timeout" 错误码
6. WHEN 执行失败 THEN THE ToolResult SHALL 包含 "execution_failed" 错误码
7. WHEN 插件内部异常 THEN THE ToolResult SHALL 包含 "plugin_exception" 错误码
8. WHEN 外部服务不可用 THEN THE ToolResult SHALL 包含 "service_unavailable" 错误码
9. WHEN 返回错误 THEN THE ToolResult.data SHALL 包含 errorCode 字段


### 需求 32: 内存管理

**用户故事**: 作为主应用开发者，我希望插件的内存管理清晰明确，以避免内存泄漏。

#### 验收标准

1. WHEN 创建提供者 THEN THE Plugin SHALL 接受父对象指针参数
2. WHEN 提供者被创建 THEN THE Provider SHALL 继承 QObject 并设置父对象
3. WHEN 父对象被销毁 THEN THE Qt_Object_System SHALL 自动释放提供者实例
4. THE Plugin SHALL 不持有主应用对象的强引用
5. WHEN 插件分配资源 THEN THE Plugin SHALL 在析构函数中释放资源
6. THE Host SHALL 管理提供者实例的生命周期

### 需求 33: 委托请求数据完整性

**用户故事**: 作为后端插件开发者，我希望委托请求包含所有必需信息，以便正确执行子任务委托。

#### 验收标准

1. THE DelegateRequest SHALL 包含 task 字段描述任务
2. THE DelegateRequest SHALL 包含 executionPrompt 字段提供执行提示词
3. THE DelegateRequest SHALL 包含 childConfig 字段提供子 Agent 配置
4. THE DelegateRequest SHALL 包含 toolExecutor 接口指针用于工具调用
5. THE DelegateRequest SHALL 包含 modelFactory 接口指针用于模型创建
6. THE DelegateRequest SHALL 包含 expectedTimeoutMs 字段指定超时时间
7. THE DelegateRequest SHALL 包含 maxResponseChars 字段限制响应长度
8. THE DelegateRequest SHALL 包含 restrictDelegation 标志控制递归委托
9. THE DelegateRequest SHALL 包含 inheritedAllowedTools 列表传递工具权限

### 需求 34: 委托回调机制

**用户故事**: 作为主应用开发者，我希望能够监听委托会话的执行过程，以便向用户展示进度。

#### 验收标准

1. THE DelegateCallbacks SHALL 包含 onActivity 回调函数通知活动事件
2. THE DelegateCallbacks SHALL 包含 onSummary 回调函数传递摘要信息
3. THE DelegateCallbacks SHALL 包含 onToolEvent 回调函数传递工具执行事件
4. THE DelegateCallbacks SHALL 包含 onStreamDelta 回调函数传递流式输出增量
5. THE DelegateCallbacks SHALL 包含 onSuccess 回调函数通知成功完成
6. THE DelegateCallbacks SHALL 包含 onFailure 回调函数通知失败
7. WHEN 委托会话执行过程中 THEN THE Backend SHALL 调用相应的回调函数
8. WHEN 调用回调函数 THEN THE Backend SHALL 在主线程或通过信号槽机制确保线程安全


### 需求 35: 队友配置数据完整性

**用户故事**: 作为后端插件开发者，我希望队友配置包含所有必需信息，以便正确创建和管理队友会话。

#### 验收标准

1. THE TeammateConfig SHALL 包含 name 字段指定队友名称
2. THE TeammateConfig SHALL 包含 role 字段指定队友角色
3. THE TeammateConfig SHALL 包含 backend 字段指定后端类型
4. THE TeammateConfig SHALL 包含 persistence 字段指定持久化策略（"persistent" 或 "temporary"）
5. THE TeammateConfig SHALL 包含 workingDirectory 字段指定工作目录
6. THE TeammateConfig SHALL 包含 ownerAgentId 字段指定所有者 Agent
7. THE TeammateConfig SHALL 包含 turnIdleTimeoutMs 字段指定空闲超时时间
8. THE TeammateConfig SHALL 包含 autoCleanup 标志控制自动清理
9. THE TeammateConfig SHALL 包含 ephemeralOwnerTurnId 字段用于临时队友
10. THE TeammateConfig SHALL 包含 backendOverrides 对象用于后端特定配置

### 需求 36: 队友状态查询

**用户故事**: 作为主应用开发者，我希望能够查询队友的当前状态，以便在 UI 中展示和管理队友。

#### 验收标准

1. THE TeammateState SHALL 包含 id 字段标识队友
2. THE TeammateState SHALL 包含 threadId 字段标识会话线程
3. THE TeammateState SHALL 包含 activeTurnId 字段标识当前回合
4. THE TeammateState SHALL 包含 status 字段表示状态（"idle"、"busy"、"error"、"shutdown"）
5. THE TeammateState SHALL 包含 lastError 字段记录最后错误
6. THE TeammateState SHALL 包含 turnCount 字段记录回合数
7. THE TeammateState SHALL 包含 createdAtMs 字段记录创建时间戳
8. THE TeammateState SHALL 包含 lastActiveAtMs 字段记录最后活动时间戳

### 需求 37: Agent 配置转换

**用户故事**: 作为主应用开发者，我希望能够将内部 LLMConfig 转换为 SDK 的 AgentConfig，以便传递给后端插件。

#### 验收标准

1. THE AgentConfig SHALL 包含 uuid 字段标识 Agent
2. THE AgentConfig SHALL 包含 userName 字段指定用户名
3. THE AgentConfig SHALL 包含 providerInstanceId 字段指定接入点实例 ID
4. THE AgentConfig SHALL 包含 selectedModelId 字段指定模型 ID
5. THE AgentConfig SHALL 包含 configId 字段兼容旧路径
6. THE AgentConfig SHALL 包含 systemPrompt 字段提供系统提示词
7. THE AgentConfig SHALL 包含 executionMode 字段指定执行模式
8. THE AgentConfig SHALL 包含 workspaceDir 字段指定工作目录
9. THE AgentConfig SHALL 包含 recursionDepth 字段控制递归深度
10. THE AgentConfig SHALL 提供 isValid 方法验证配置有效性
11. THE AgentConfig SHALL 提供 canDelegate 方法检查是否允许委托


### 需求 38: 工具 Schema 序列化

**用户故事**: 作为插件开发者，我希望工具定义能够序列化为 JSON 格式，以便与 LLM API 集成。

#### 验收标准

1. THE Tool SHALL 提供 toJson 方法序列化为 JSON 对象
2. WHEN 序列化工具 THEN THE toJson SHALL 返回包含 "type": "function" 的对象
3. WHEN 序列化工具 THEN THE toJson SHALL 在 "function" 字段中包含 name、description、parameters
4. THE ToolCall SHALL 提供 fromJson 静态方法从 JSON 反序列化
5. WHEN 反序列化工具调用 THEN THE fromJson SHALL 解析 id、type、function 字段
6. WHEN 反序列化工具调用 THEN THE fromJson SHALL 解析 function.arguments JSON 字符串为 QJsonObject

### 需求 39: 测试支持

**用户故事**: 作为 SDK 维护者，我希望 SDK 包含完整的测试套件，以确保接口正确性。

#### 验收标准

1. THE SDK SHALL 包含单元测试验证数据结构的序列化和反序列化
2. THE SDK SHALL 包含单元测试验证辅助函数的正确性
3. THE SDK SHALL 包含集成测试验证插件加载流程
4. THE SDK SHALL 包含集成测试验证工具调用端到端流程
5. THE SDK SHALL 包含集成测试验证版本兼容性检查
6. THE SDK SHALL 使用 Qt Test 框架
7. WHEN 运行测试 THEN THE Test_Suite SHALL 达到 80% 以上的代码覆盖率

### 需求 40: 迁移路径支持

**用户故事**: 作为项目经理，我希望有清晰的迁移路径，以便团队能够分阶段完成重构。

#### 验收标准

1. THE Migration SHALL 分为 4 个阶段：SDK 基础设施、主应用适配、官方插件迁移、文档和生态
2. WHEN 完成阶段 1 THEN THE SDK SHALL 可以独立编译且大小小于 100KB
3. WHEN 完成阶段 2 THEN THE Host SHALL 能够加载基于 SDK 的插件
4. WHEN 完成阶段 2 THEN THE Host SHALL 继续支持旧接口插件
5. WHEN 完成阶段 3 THEN THE Official_Plugins SHALL 全部基于 SDK 编译
6. WHEN 完成阶段 3 THEN THE Official_Plugins SHALL 可以独立编译
7. WHEN 完成阶段 4 THEN THE SDK SHALL 包含完整文档和至少 2 个示例项目
8. THE Migration SHALL 在 12 周内完成所有阶段


### 需求 41: 插件健康检查

**用户故事**: 作为主应用开发者，我希望能够检查插件的健康状态，以便及时发现和处理问题。

#### 验收标准

1. WHERE 插件支持健康检查 THEN THE IToolPlugin SHALL 提供 health 方法
2. THE ToolPluginHealth SHALL 包含状态字段（"healthy"、"degraded"、"unhealthy"）
3. THE ToolPluginHealth SHALL 包含诊断信息字段
4. WHEN 调用 health 方法 THEN THE Plugin SHALL 检查提供者的运行状态
5. WHEN 提供者正常工作 THEN THE health SHALL 返回 "healthy" 状态
6. WHEN 提供者部分功能不可用 THEN THE health SHALL 返回 "degraded" 状态
7. WHEN 提供者完全不可用 THEN THE health SHALL 返回 "unhealthy" 状态

### 需求 42: 工具列表查询

**用户故事**: 作为 AI Agent，我希望能够查询插件提供的所有工具，以便知道可以使用哪些工具。

#### 验收标准

1. THE IToolProvider SHALL 定义 listTools 方法返回工具列表
2. WHEN 调用 listTools THEN THE Provider SHALL 返回 QList<Tool> 类型
3. WHEN 返回工具列表 THEN THE Provider SHALL 包含所有可用工具的完整定义
4. WHEN 返回工具定义 THEN THE Tool SHALL 包含 name、description、inputSchema 字段
5. THE inputSchema SHALL 使用 JSON Schema 格式定义参数

### 需求 43: 插件配置验证

**用户故事**: 作为主应用开发者，我希望能够验证插件配置的正确性，以便在配置错误时给出明确提示。

#### 验收标准

1. WHERE 插件支持配置 THEN THE IToolPlugin SHALL 提供 configureProvider 方法
2. WHEN 配置提供者 THEN THE configureProvider SHALL 接受 Provider 指针、配置对象和错误指针
3. WHEN 配置成功 THEN THE configureProvider SHALL 返回 true
4. IF 配置失败 THEN THE configureProvider SHALL 返回 false 并设置错误信息
5. WHERE 插件声明配置 Schema THEN THE Host SHALL 在配置前验证配置对象符合 Schema


### 需求 44: 后端就绪状态管理

**用户故事**: 作为主应用开发者，我希望能够检查后端是否就绪，以便在后端未就绪时给出提示。

#### 验收标准

1. THE ITeammateBackend SHALL 定义 ensureReady 方法确保后端就绪
2. THE ITeammateBackend SHALL 定义 isReady 方法查询后端状态
3. WHEN 调用 ensureReady THEN THE Backend SHALL 执行必要的初始化操作
4. WHEN 初始化成功 THEN THE ensureReady SHALL 返回 true
5. IF 初始化失败 THEN THE ensureReady SHALL 返回 false 并设置错误信息
6. WHEN 后端未就绪 THEN THE isReady SHALL 返回 false
7. WHEN 后端已就绪 THEN THE isReady SHALL 返回 true

### 需求 45: 会话管理

**用户故事**: 作为后端插件开发者，我希望能够创建、管理和销毁会话，以便支持多个并发对话。

#### 验收标准

1. WHEN 创建队友会话 THEN THE ITeammateBackend SHALL 接受队友 ID 和配置
2. WHEN 会话创建成功 THEN THE CreateResult SHALL 包含 success=true 和 threadId
3. IF 会话创建失败 THEN THE CreateResult SHALL 包含 success=false 和错误信息
4. WHEN 发送消息 THEN THE ITeammateBackend SHALL 接受队友 ID 和消息文本
5. WHEN 消息发送成功 THEN THE SendResult SHALL 包含 success=true 和 turnId
6. IF 消息发送失败 THEN THE SendResult SHALL 包含 success=false 和错误信息
7. WHEN 取消回合 THEN THE ITeammateBackend SHALL 接受队友 ID 和错误指针
8. WHEN 取消成功 THEN THE cancelTurn SHALL 返回 true
9. WHEN 销毁会话 THEN THE ITeammateBackend SHALL 释放与该队友相关的所有资源
10. WHEN 关闭后端 THEN THE ITeammateBackend SHALL 销毁所有会话并释放资源

### 需求 46: 委托会话控制

**用户故事**: 作为主应用开发者，我希望能够控制委托会话的执行，包括启动和取消。

#### 验收标准

1. THE IDelegateSession SHALL 定义 backendId 方法返回后端标识
2. THE IDelegateSession SHALL 定义 start 方法启动会话
3. THE IDelegateSession SHALL 定义 cancel 方法取消会话
4. WHEN 调用 start THEN THE Session SHALL 开始执行委托任务
5. WHEN 调用 cancel THEN THE Session SHALL 中止任务执行并调用 onFailure 回调
6. WHEN 会话完成 THEN THE Session SHALL 调用 onSuccess 或 onFailure 回调
7. WHEN 会话对象被销毁 THEN THE Session SHALL 自动取消未完成的任务


### 需求 47: 工具执行回调接口

**用户故事**: 作为后端插件开发者，我希望能够在委托会话中调用工具，以便子 Agent 能够使用工具完成任务。

#### 验收标准

1. THE IToolExecutor SHALL 定义 executeToolSync 方法用于同步工具调用
2. THE IToolExecutor SHALL 定义 executeToolAsync 方法用于异步工具调用
3. WHEN 调用 executeToolSync THEN THE IToolExecutor SHALL 接受 ToolCall 参数并返回 ToolResult
4. WHEN 调用 executeToolAsync THEN THE IToolExecutor SHALL 接受 ToolCall 参数和回调函数
5. WHEN 异步工具完成 THEN THE IToolExecutor SHALL 调用回调函数传递 ToolResult
6. THE Host SHALL 实现 ToolExecutorAdapter 类桥接到 ToolDispatcher

### 需求 48: 模型工厂回调接口

**用户故事**: 作为后端插件开发者，我希望能够创建 LLM Provider，以便在委托会话中使用不同的模型。

#### 验收标准

1. THE IModelFactory SHALL 定义 createProvider 方法创建 LLM Provider
2. WHEN 创建 Provider THEN THE IModelFactory SHALL 接受 AgentConfig 参数和错误指针
3. WHEN 创建成功 THEN THE createProvider SHALL 返回 QObject 指针
4. IF 创建失败 THEN THE createProvider SHALL 返回 nullptr 并设置错误信息
5. THE IModelFactory SHALL 定义 getModelCapabilities 方法查询模型能力
6. WHEN 查询模型能力 THEN THE getModelCapabilities SHALL 返回能力列表
7. THE Host SHALL 实现 ModelFactoryAdapter 类桥接到 ModelFactory

### 需求 49: 插件目录结构规范

**用户故事**: 作为插件开发者，我希望了解插件的目录结构规范，以便正确组织插件文件。

#### 验收标准

1. THE Plugin SHALL 将工具插件放置在 plugins/tools/ 子目录
2. THE Plugin SHALL 将后端插件放置在 plugins/backends/ 子目录
3. WHEN 插件包含多个文件 THEN THE Plugin SHALL 创建以插件 ID 命名的子目录
4. THE Plugin SHALL 在插件目录中包含动态库文件和元数据 JSON 文件
5. WHERE 插件包含资源文件 THEN THE Plugin SHALL 将资源文件放置在插件子目录中


### 需求 50: 辅助宏定义

**用户故事**: 作为插件开发者，我希望有便捷的宏定义，以便快速声明插件接口。

#### 验收标准

1. THE SDK SHALL 在 PluginMacros.h 中定义插件宏
2. THE SDK SHALL 定义 TMAGENT_TOOL_PLUGIN_IID 宏用于工具插件 IID
3. THE SDK SHALL 定义 TMAGENT_BACKEND_PLUGIN_IID 宏用于后端插件 IID
4. THE SDK SHALL 提供 Q_DECLARE_INTERFACE 宏声明用于 Qt 插件系统集成
5. WHEN 插件使用宏 THEN THE Plugin SHALL 能够被 Qt 插件系统识别

### 需求 51: 代码解析服务

**用户故事**: 作为插件开发者，我希望能够使用主应用的代码解析服务，以便实现代码智能功能。

#### 验收标准

1. WHERE 插件需要代码解析 THEN THE IToolPluginHost SHALL 提供 parseCode 方法
2. WHEN 调用 parseCode THEN THE Host SHALL 接受语言类型、代码内容和错误指针
3. WHEN 解析成功 THEN THE parseCode SHALL 返回包含 AST 的 QJsonObject
4. IF 解析失败 THEN THE parseCode SHALL 返回空对象并设置错误信息
5. THE Host SHALL 使用 tree-sitter 实现代码解析

### 需求 52: 插件卸载和重载

**用户故事**: 作为最终用户，我希望能够在不重启应用的情况下卸载和重载插件，以便快速更新插件。

#### 验收标准

1. WHERE 支持热重载 THEN THE PluginManager SHALL 提供 unloadPlugin 方法
2. WHERE 支持热重载 THEN THE PluginManager SHALL 提供 reloadPlugin 方法
3. WHEN 卸载插件 THEN THE PluginManager SHALL 销毁提供者实例
4. WHEN 卸载插件 THEN THE PluginManager SHALL 调用 QPluginLoader::unload
5. WHEN 重载插件 THEN THE PluginManager SHALL 先卸载再加载
6. WHEN 重载插件 THEN THE PluginManager SHALL 保持插件配置不变


### 需求 53: 工具调用协议兼容性

**用户故事**: 作为 SDK 架构师，我希望工具调用协议与 OpenAI API 兼容，以便与现有 LLM 集成。

#### 验收标准

1. THE ToolCall SHALL 支持从 OpenAI 格式的 JSON 反序列化
2. WHEN LLM 返回 tool_calls THEN THE ToolCall.fromJson SHALL 解析 id、type、function 字段
3. WHEN 解析 function 字段 THEN THE ToolCall SHALL 提取 name 和 arguments
4. WHEN 解析 arguments THEN THE ToolCall SHALL 将 JSON 字符串解析为 QJsonObject
5. THE Tool SHALL 支持序列化为 OpenAI 格式的 JSON
6. WHEN 序列化工具 THEN THE Tool.toJson SHALL 生成包含 type 和 function 的对象
7. WHEN 返回工具结果给 LLM THEN THE Agent SHALL 使用 ToolResult.rawContent 作为 content 字段

### 需求 54: 插件权限声明

**用户故事**: 作为插件开发者，我希望能够声明插件需要的权限，以便用户了解插件的访问范围。

#### 验收标准

1. WHERE 插件需要权限 THEN THE ToolPluginDescriptor SHALL 包含 requiredPermissions 字段
2. THE SDK SHALL 定义标准权限类型：filesystem.read、filesystem.write、network.access、process.execute、system.info
3. WHEN 插件声明权限 THEN THE requiredPermissions SHALL 使用 QStringList 类型
4. WHEN 加载插件 THEN THE Host SHALL 读取 requiredPermissions 字段
5. WHEN 首次加载插件 THEN THE Host SHALL 向用户显示权限请求
6. WHEN 用户批准权限 THEN THE Host SHALL 记录信任决策
7. WHEN 用户拒绝权限 THEN THE Host SHALL 拒绝加载插件

### 需求 55: 工具执行上下文

**用户故事**: 作为插件开发者，我希望在工具执行时能够访问上下文信息，以便实现上下文相关的功能。

#### 验收标准

1. THE ToolCall SHALL 包含 id 字段唯一标识调用
2. THE ToolCall SHALL 包含 name 字段指定工具名称
3. THE ToolCall SHALL 包含 input 字段传递参数
4. WHEN 执行工具 THEN THE Provider SHALL 能够通过 IToolPluginHost 访问应用上下文
5. WHEN 执行工具 THEN THE Provider SHALL 能够通过 getAppConfig 读取应用配置
6. WHEN 执行工具 THEN THE Provider SHALL 能够通过 getPluginDataDir 访问数据目录


### 需求 56: 工具结果元数据

**用户故事**: 作为 AI Agent，我希望工具结果包含结构化元数据，以便更好地理解和处理结果。

#### 验收标准

1. THE ToolResult SHALL 包含 rawContent 字段提供给 LLM 的完整数据
2. THE ToolResult SHALL 包含 userSummary 字段提供给用户的简短摘要
3. THE ToolResult SHALL 包含 success 字段表示执行状态
4. THE ToolResult SHALL 包含 data 字段提供结构化元数据
5. WHEN 工具执行成功 THEN THE ToolResult SHALL 设置 success=true
6. WHEN 工具执行失败 THEN THE ToolResult SHALL 设置 success=false
7. WHERE 工具返回结构化数据 THEN THE ToolResult.data SHALL 使用 QJsonObject 格式
8. THE ToolResult SHALL 提供默认构造函数初始化 success=true

### 需求 57: 插件分类管理

**用户故事**: 作为最终用户，我希望插件按类别组织，以便快速找到需要的插件。

#### 验收标准

1. THE ToolPluginDescriptor SHALL 包含 category 字段指定插件类别
2. THE SDK SHALL 建议使用标准类别：tools、backends、utilities、integrations、examples
3. WHEN 显示插件列表 THEN THE Host SHALL 按类别分组显示
4. WHEN 搜索插件 THEN THE Host SHALL 支持按类别筛选

### 需求 58: 版本信息查询

**用户故事**: 作为插件开发者，我希望能够在编译时查询 SDK 版本，以便实现版本相关的逻辑。

#### 验收标准

1. THE SDK SHALL 在 version.h 中定义 TMAGENT_SDK_VERSION_MAJOR 宏
2. THE SDK SHALL 在 version.h 中定义 TMAGENT_SDK_VERSION_MINOR 宏
3. THE SDK SHALL 在 version.h 中定义 TMAGENT_SDK_VERSION_PATCH 宏
4. THE SDK SHALL 在 version.h 中定义 TMAGENT_SDK_VERSION_STRING 宏
5. WHEN 插件编译 THEN THE Plugin SHALL 能够使用版本宏进行条件编译
6. WHEN 插件初始化 THEN THE Plugin SHALL 能够使用版本宏填充 descriptor


### 需求 59: 插件依赖声明

**用户故事**: 作为插件开发者，我希望能够声明插件的依赖关系，以便主应用检查依赖是否满足。

#### 验收标准

1. WHERE 插件有依赖 THEN THE Metadata_File SHALL 包含 dependencies 对象
2. THE dependencies SHALL 支持声明 Qt 版本依赖（如 "qt": ">=5.15.0"）
3. THE dependencies SHALL 支持声明 SDK 版本依赖（如 "sdk": "^1.0.0"）
4. WHERE 插件依赖其他插件 THEN THE dependencies SHALL 支持声明插件依赖
5. WHEN 加载插件 THEN THE Host SHALL 检查依赖是否满足
6. IF 依赖不满足 THEN THE Host SHALL 拒绝加载插件并记录错误

### 需求 60: 工具 Schema 验证

**用户故事**: 作为主应用开发者，我希望验证工具参数符合 Schema，以便在参数错误时给出明确提示。

#### 验收标准

1. WHEN 接收工具调用 THEN THE Host SHALL 获取工具的 inputSchema
2. WHEN 获取 Schema THEN THE Host SHALL 使用 JSON Schema 验证器验证参数
3. IF 参数不符合 Schema THEN THE Host SHALL 返回包含 "invalid_parameter" 错误码的 ToolResult
4. WHEN 验证失败 THEN THE ToolResult SHALL 在错误信息中说明哪个参数不符合要求
5. THE Host SHALL 支持 JSON Schema Draft 7 标准

### 需求 61: 插件加载失败恢复

**用户故事**: 作为最终用户，我希望在插件加载失败后能够查看失败原因并重试，而无需重启应用。

#### 验收标准

1. WHEN 插件加载失败 THEN THE PluginManager SHALL 记录失败信息到 m_failedPlugins 列表
2. THE FailedPluginInfo SHALL 包含插件路径和错误信息
3. THE PluginManager SHALL 提供 getFailedPlugins 方法查询失败列表
4. THE PluginManager SHALL 提供 retryLoadPlugin 方法重试加载失败的插件
5. WHEN 用户修复插件后 THEN THE User SHALL 能够通过 UI 触发重试
6. WHEN 重试成功 THEN THE PluginManager SHALL 从失败列表中移除该插件


### 需求 62: 工具执行超时控制

**用户故事**: 作为主应用开发者，我希望能够为工具执行设置超时时间，以防止工具长时间阻塞。

#### 验收标准

1. WHERE 工具支持超时控制 THEN THE Tool SHALL 在 inputSchema 中声明 timeout 参数
2. WHEN 执行工具 THEN THE Host SHALL 检查是否超过超时时间
3. IF 工具执行超时 THEN THE Host SHALL 中止执行并返回超时错误
4. WHEN 返回超时错误 THEN THE ToolResult SHALL 包含 "timeout" 错误码
5. THE Host SHALL 为同步工具提供默认超时时间（30 秒）

### 需求 63: 插件信息查询

**用户故事**: 作为最终用户，我希望能够查看已加载插件的信息，以便了解当前可用的功能。

#### 验收标准

1. THE PluginManager SHALL 提供 listPlugins 方法返回所有已加载插件的描述符
2. THE PluginManager SHALL 提供 getPlugin 方法根据 ID 查询特定插件
3. THE PluginManager SHALL 提供 getProvider 方法根据插件 ID 获取提供者实例
4. WHEN 查询插件 THEN THE PluginManager SHALL 返回 ToolPluginDescriptor 或 BackendDescriptor
5. WHEN 查询不存在的插件 THEN THE getPlugin SHALL 返回空值

### 需求 64: 工具名称唯一性

**用户故事**: 作为主应用开发者，我希望确保工具名称全局唯一，以避免工具调用冲突。

#### 验收标准

1. WHEN 注册工具 THEN THE ToolDispatcher SHALL 检查工具名称是否已存在
2. IF 工具名称冲突 THEN THE ToolDispatcher SHALL 记录警告并拒绝注册
3. WHEN 工具名称冲突 THEN THE Host SHALL 在日志中记录冲突的插件 ID
4. THE ToolDispatcher SHALL 维护工具名称到提供者的映射表
5. WHEN 查询工具 THEN THE ToolDispatcher SHALL 根据名称快速定位提供者


### 需求 65: 后端类型支持

**用户故事**: 作为后端插件开发者，我希望能够声明后端支持的类型，以便主应用正确使用后端。

#### 验收标准

1. THE BackendDescriptor SHALL 包含 supportsDelegate 布尔字段
2. THE BackendDescriptor SHALL 包含 supportsTeammate 布尔字段
3. WHEN 后端支持委托 THEN THE supportsDelegate SHALL 设置为 true
4. WHEN 后端支持队友 THEN THE supportsTeammate SHALL 设置为 true
5. WHEN 创建委托后端 THEN THE IBackendPlugin SHALL 提供 createDelegateBackend 方法
6. WHEN 创建队友后端 THEN THE IBackendPlugin SHALL 提供 createTeammateBackend 方法
7. IF 后端不支持委托 THEN THE createDelegateBackend SHALL 返回 nullptr
8. IF 后端不支持队友 THEN THE createTeammateBackend SHALL 返回 nullptr

### 需求 66: 工具执行事件通知

**用户故事**: 作为主应用开发者，我希望能够监听工具执行事件，以便在 UI 中展示执行进度。

#### 验收标准

1. THE DelegateCallbacks SHALL 包含 onToolEvent 回调函数
2. WHEN 委托会话中执行工具 THEN THE Backend SHALL 调用 onToolEvent 回调
3. THE ToolExecutionEvent SHALL 包含工具名称、调用 ID、执行状态
4. WHEN 工具开始执行 THEN THE Backend SHALL 发送 "started" 事件
5. WHEN 工具执行完成 THEN THE Backend SHALL 发送 "completed" 事件
6. WHEN 工具执行失败 THEN THE Backend SHALL 发送 "failed" 事件

### 需求 67: 流式输出支持

**用户故事**: 作为最终用户，我希望能够实时看到 AI 的输出，以便了解执行进度。

#### 验收标准

1. THE DelegateCallbacks SHALL 包含 onStreamDelta 回调函数
2. WHEN 后端生成流式输出 THEN THE Backend SHALL 调用 onStreamDelta 回调
3. WHEN 调用 onStreamDelta THEN THE Backend SHALL 传递增量文本内容
4. WHEN 流式输出完成 THEN THE Backend SHALL 调用 onSuccess 或 onFailure 回调
5. THE Backend SHALL 支持 SSE（Server-Sent Events）或类似的流式协议


### 需求 68: 插件元数据本地化

**用户故事**: 作为国际用户，我希望插件的显示名称和描述支持多语言，以便使用母语了解插件功能。

#### 验收标准

1. WHERE 插件支持多语言 THEN THE Metadata_File SHALL 包含 i18n 对象
2. THE i18n SHALL 包含语言代码到翻译的映射（如 "zh-CN": {...}）
3. WHEN 加载插件 THEN THE Host SHALL 根据系统语言选择对应翻译
4. IF 翻译不存在 THEN THE Host SHALL 回退到默认语言（英语）
5. THE Translation SHALL 包含 displayName 和 description 的翻译

### 需求 69: 插件更新检查

**用户故事**: 作为最终用户，我希望能够检查插件更新，以便使用最新功能和修复。

#### 验收标准

1. WHERE 插件提供更新源 THEN THE Metadata_File SHALL 包含 updateUrl 字段
2. WHERE 支持更新检查 THEN THE PluginManager SHALL 提供 checkUpdates 方法
3. WHEN 检查更新 THEN THE PluginManager SHALL 访问 updateUrl 获取最新版本信息
4. WHEN 发现新版本 THEN THE PluginManager SHALL 通知用户
5. WHERE 支持自动更新 THEN THE PluginManager SHALL 提供 updatePlugin 方法

### 需求 70: 插件沙箱隔离

**用户故事**: 作为系统管理员，我希望插件在隔离环境中运行，以防止恶意插件危害系统。

#### 验收标准

1. WHERE 支持进程隔离 THEN THE Host SHALL 在独立进程中加载插件
2. WHERE 支持进程隔离 THEN THE Host SHALL 使用 IPC 机制与插件通信
3. WHEN 插件崩溃 THEN THE Host SHALL 检测崩溃并重启插件进程
4. WHEN 插件崩溃 THEN THE Host SHALL 确保主应用不受影响
5. WHERE 支持资源限制 THEN THE Host SHALL 限制插件的 CPU 和内存使用


### 需求 71: 工具调用批处理

**用户故事**: 作为 AI Agent，我希望能够批量调用多个工具，以提高执行效率。

#### 验收标准

1. WHERE 支持批处理 THEN THE IToolProvider SHALL 提供 executeBatch 方法
2. WHEN 批量执行工具 THEN THE executeBatch SHALL 接受 QList<ToolCall> 参数
3. WHEN 批量执行完成 THEN THE executeBatch SHALL 返回 QList<ToolResult>
4. WHEN 批量执行 THEN THE Provider SHALL 保持调用顺序与结果顺序一致
5. IF 批处理中某个工具失败 THEN THE Provider SHALL 继续执行其他工具

### 需求 72: 插件性能监控

**用户故事**: 作为系统管理员，我希望能够监控插件的性能指标，以便识别性能问题。

#### 验收标准

1. WHERE 支持性能监控 THEN THE Host SHALL 记录每个工具调用的执行时间
2. WHERE 支持性能监控 THEN THE Host SHALL 记录每个插件的内存使用
3. WHERE 支持性能监控 THEN THE Host SHALL 提供 getPluginMetrics 方法查询性能指标
4. THE PluginMetrics SHALL 包含调用次数、平均执行时间、失败次数
5. WHEN 工具执行时间超过阈值 THEN THE Host SHALL 记录性能警告

### 需求 73: 插件开发模板

**用户故事**: 作为第三方开发者，我希望有插件项目模板，以便快速创建新插件。

#### 验收标准

1. THE SDK SHALL 提供工具插件项目模板
2. THE SDK SHALL 提供后端插件项目模板
3. THE Template SHALL 包含完整的项目结构（源文件、头文件、构建配置）
4. THE Template SHALL 包含占位符代码和注释说明
5. WHEN 使用模板 THEN THE Developer SHALL 能够在 10 分钟内创建可编译的插件
6. THE Template SHALL 包含 README 文件说明如何使用模板


### 需求 74: 插件市场集成

**用户故事**: 作为最终用户，我希望能够从插件市场浏览和安装插件，以便扩展应用功能。

#### 验收标准

1. WHERE 支持插件市场 THEN THE Host SHALL 提供插件市场 UI
2. WHERE 支持插件市场 THEN THE Host SHALL 提供 browseMarketplace 方法
3. WHEN 浏览市场 THEN THE Host SHALL 显示插件列表、评分、下载量
4. WHERE 支持插件市场 THEN THE Host SHALL 提供 installPlugin 方法
5. WHEN 安装插件 THEN THE Host SHALL 下载插件文件到用户插件目录
6. WHEN 安装完成 THEN THE Host SHALL 自动加载新安装的插件

### 需求 75: 递归委托控制

**用户故事**: 作为系统架构师，我希望能够控制委托的递归深度，以防止无限递归。

#### 验收标准

1. THE AgentConfig SHALL 包含 recursionDepth 字段
2. WHEN 创建子 Agent THEN THE Host SHALL 将 recursionDepth 减 1
3. WHEN recursionDepth 为 0 THEN THE AgentConfig.canDelegate SHALL 返回 false
4. WHEN 子 Agent 尝试委托且 recursionDepth 为 0 THEN THE Host SHALL 拒绝委托请求
5. THE DelegateRequest SHALL 包含 restrictDelegation 标志
6. WHEN restrictDelegation 为 true THEN THE Backend SHALL 禁止子 Agent 进一步委托

### 需求 76: 工具权限继承

**用户故事**: 作为系统架构师，我希望能够控制子 Agent 可以使用哪些工具，以确保安全性。

#### 验收标准

1. THE DelegateRequest SHALL 包含 inheritedAllowedTools 字段
2. WHEN 创建委托会话 THEN THE Host SHALL 传递父 Agent 允许的工具列表
3. WHEN 子 Agent 调用工具 THEN THE Backend SHALL 检查工具是否在允许列表中
4. IF 工具不在允许列表中 THEN THE Backend SHALL 拒绝工具调用
5. WHERE 允许列表为空 THEN THE Backend SHALL 允许所有工具


### 需求 77: 插件卸载清理

**用户故事**: 作为最终用户，我希望卸载插件时能够清理所有相关数据，以释放磁盘空间。

#### 验收标准

1. WHEN 卸载插件 THEN THE Host SHALL 提供选项删除插件数据目录
2. WHEN 用户选择删除数据 THEN THE Host SHALL 删除 getPluginDataDir 返回的目录
3. WHEN 用户选择保留数据 THEN THE Host SHALL 仅删除插件动态库文件
4. WHEN 卸载插件 THEN THE Host SHALL 从配置中移除插件的信任决策
5. WHEN 卸载完成 THEN THE Host SHALL 从已加载插件列表中移除该插件

### 需求 78: 工具调用日志记录

**用户故事**: 作为系统管理员，我希望记录所有工具调用的详细日志，以便审计和调试。

#### 验收标准

1. WHEN 执行工具调用 THEN THE ToolDispatcher SHALL 记录调用开始日志
2. THE Call_Log SHALL 包含时间戳、工具名称、调用 ID、插件 ID
3. WHEN 工具执行完成 THEN THE ToolDispatcher SHALL 记录完成日志
4. THE Completion_Log SHALL 包含执行时间、成功状态、结果大小
5. WHEN 工具执行失败 THEN THE ToolDispatcher SHALL 记录错误日志
6. THE Error_Log SHALL 包含错误码、错误信息、堆栈跟踪（如果可用）

### 需求 79: 插件依赖解析

**用户故事**: 作为主应用开发者，我希望自动解析插件依赖关系，以便按正确顺序加载插件。

#### 验收标准

1. WHEN 加载插件 THEN THE PluginManager SHALL 解析 dependencies 字段
2. WHEN 插件依赖其他插件 THEN THE PluginManager SHALL 先加载依赖插件
3. IF 依赖插件不存在 THEN THE PluginManager SHALL 拒绝加载该插件
4. IF 依赖关系形成循环 THEN THE PluginManager SHALL 检测循环并拒绝加载
5. WHEN 依赖解析失败 THEN THE PluginManager SHALL 记录详细错误信息


### 需求 80: 工具调用取消

**用户故事**: 作为最终用户，我希望能够取消正在执行的工具调用，以便中止不需要的操作。

#### 验收标准

1. WHERE 工具支持取消 THEN THE IToolProvider SHALL 提供 cancel 方法
2. WHEN 调用 cancel THEN THE Provider SHALL 接受调用 ID 参数
3. WHEN 取消工具 THEN THE Provider SHALL 中止执行并返回取消状态
4. WHEN 取消成功 THEN THE cancel SHALL 返回 true
5. IF 工具已完成或不支持取消 THEN THE cancel SHALL 返回 false
6. WHEN 异步工具被取消 THEN THE Provider SHALL 不发出 toolCompleted 信号

### 需求 81: 插件调试模式

**用户故事**: 作为插件开发者，我希望能够在调试模式下运行插件，以便获取更详细的日志和诊断信息。

#### 验收标准

1. WHERE 支持调试模式 THEN THE Host SHALL 提供 setDebugMode 方法
2. WHEN 启用调试模式 THEN THE Host SHALL 记录所有插件接口调用
3. WHEN 启用调试模式 THEN THE Host SHALL 记录所有数据传递的详细内容
4. WHEN 启用调试模式 THEN THE Host SHALL 禁用性能优化（如缓存）
5. WHEN 禁用调试模式 THEN THE Host SHALL 恢复正常日志级别

### 需求 82: 工具 Schema 扩展

**用户故事**: 作为插件开发者，我希望能够使用扩展的 Schema 特性，以便更精确地定义工具参数。

#### 验收标准

1. THE ToolSchemaBuilder SHALL 支持 JSON Schema 的 enum 约束
2. THE ToolSchemaBuilder SHALL 支持 JSON Schema 的 pattern 约束
3. THE ToolSchemaBuilder SHALL 支持 JSON Schema 的 minimum/maximum 约束
4. THE ToolSchemaBuilder SHALL 支持嵌套对象和数组类型
5. THE ToolSchemaBuilder SHALL 支持 oneOf/anyOf/allOf 组合
6. WHEN 构建复杂 Schema THEN THE ToolSchemaBuilder SHALL 提供链式 API


### 需求 83: 插件加载优先级

**用户故事**: 作为系统管理员，我希望能够控制插件的加载优先级，以便优先加载关键插件。

#### 验收标准

1. WHERE 插件声明优先级 THEN THE Metadata_File SHALL 包含 priority 字段
2. THE priority SHALL 使用整数值，数值越大优先级越高
3. WHEN 加载插件 THEN THE PluginManager SHALL 按优先级从高到低排序
4. WHEN 优先级相同 THEN THE PluginManager SHALL 按字母顺序加载
5. WHERE 未声明优先级 THEN THE PluginManager SHALL 使用默认优先级 0

### 需求 84: 工具执行上下文传递

**用户故事**: 作为插件开发者，我希望能够在工具调用之间传递上下文信息，以便实现有状态的工具。

#### 验收标准

1. WHERE 工具需要上下文 THEN THE ToolCall SHALL 支持在 input 中传递 context 字段
2. WHERE 工具返回上下文 THEN THE ToolResult SHALL 支持在 data 中返回 context 字段
3. WHEN Agent 连续调用工具 THEN THE Agent SHALL 能够传递前一次调用返回的上下文
4. THE Context SHALL 使用 QJsonObject 格式，支持任意结构化数据

### 需求 85: 插件禁用和启用

**用户故事**: 作为最终用户，我希望能够禁用不需要的插件，以减少资源占用。

#### 验收标准

1. THE PluginManager SHALL 提供 disablePlugin 方法禁用插件
2. THE PluginManager SHALL 提供 enablePlugin 方法启用插件
3. WHEN 禁用插件 THEN THE PluginManager SHALL 卸载插件但保留配置
4. WHEN 禁用插件 THEN THE Host SHALL 记录禁用状态到配置文件
5. WHEN 启用插件 THEN THE PluginManager SHALL 重新加载插件
6. WHEN 应用重启 THEN THE PluginManager SHALL 不加载已禁用的插件

