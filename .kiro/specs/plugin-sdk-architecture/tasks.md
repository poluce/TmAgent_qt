# 实施计划：TmAgent 插件 SDK 架构重构

## 概述

本实施计划将 TmAgent 插件系统重构为独立的 SDK 架构，分为 4 个阶段：SDK 基础设施、主应用适配、官方插件迁移、文档和生态。每个阶段都有明确的验收标准，确保平滑过渡。

## 任务列表

### 阶段 1: SDK 基础设施（2-3 周）

- [x] 1. 创建 SDK 项目结构
  - 创建 tmagent-plugin-sdk 目录和基础文件结构
  - 创建 include/tmagent/ 目录层级（plugin/、types/、support/）
  - 创建 examples/、docs/ 目录
  - 创建 LICENSE、README.md 文件
  - _需求: 1.1, 1.4_

- [x] 2. 定义核心插件接口
  - [x] 2.1 创建 IToolPlugin 接口
    - 定义 descriptor()、createProvider()、configureProvider()、health() 方法
    - 添加 TMAGENT_TOOL_PLUGIN_IID 宏定义
    - 添加 SDK 版本字段到 ToolPluginDescriptor
    - _需求: 2.1, 2.6, 2.7, 5.6_
  
  - [x] 2.2 创建 IToolProvider 接口
    - 定义 listTools()、execute() 方法
    - _需求: 2.3, 42.1, 42.2_
  
  - [x] 2.3 创建 IToolPluginHost 接口
    - 定义查询服务方法（availableTeammateBackendIds、availableTools）
    - 定义工具调用服务方法（executeHostTool）
    - 定义日志服务方法（logDebug、logInfo、logWarning、logError）
    - 定义配置服务方法（getPluginConfig、setPluginConfig）
    - 定义文件服务方法（getPluginDataDir、getAppDataDir）
    - 定义代码解析服务方法（parseCode）
    - _需求: 4.1-4.8, 19.1-19.7, 20.1-20.8, 27.1-27.6, 51.1-51.5_


- [x] 3. 定义后端插件接口
  - [x] 3.1 创建 IBackendPlugin 接口
    - 定义 descriptor()、createDelegateBackend()、createTeammateBackend() 方法
    - 添加 TMAGENT_BACKEND_PLUGIN_IID 宏定义
    - 添加 SDK 版本字段到 BackendDescriptor
    - _需求: 2.2, 2.4, 5.7, 65.1-65.8_
  
  - [x] 3.2 创建 IDelegateBackend 接口
    - 定义 backendId()、createSession() 方法
    - 定义 DelegateRequest 结构（包含 IToolExecutor、IModelFactory 接口指针）
    - 定义 DelegateCallbacks 结构（包含所有回调函数）
    - 定义 IDelegateSession 接口（start、cancel 方法）
    - _需求: 2.4, 15.1-15.10, 33.1-33.9, 34.1-34.8, 46.1-46.7_
  
  - [x] 3.3 创建 ITeammateBackend 接口
    - 定义 backendId()、ensureReady()、isReady() 方法
    - 定义 createSession()、sendMessage()、cancelTurn()、destroySession()、shutdown() 方法
    - 定义 CreateResult、SendResult 结构
    - _需求: 2.5, 16.1-16.11, 44.1-44.7, 45.1-45.10_
  
  - [x] 3.4 创建工具执行回调接口
    - 定义 IToolExecutor 接口（executeToolSync、executeToolAsync 方法）
    - 定义 IModelFactory 接口（createProvider、getModelCapabilities 方法）
    - _需求: 9.7, 9.8, 47.1-47.6, 48.1-48.7_

- [x] 4. 定义数据结构
  - [x] 4.1 创建 ToolTypes.h
    - 定义 Tool 结构（name、description、inputSchema）
    - 定义 ToolCall 结构（id、name、input）
    - 定义 ToolResult 结构（rawContent、userSummary、success、data）
    - 实现 Tool::toJson() 方法
    - 实现 ToolCall::fromJson() 静态方法
    - _需求: 3.1, 3.2, 3.3, 3.10, 38.1-38.6, 53.1-53.7, 56.1-56.8_
  
  - [x] 4.2 创建 PluginTypes.h
    - 定义 ToolPluginDescriptor 结构（pluginId、displayName、version、description、category、toolNames、configSchema、sdkVersionMajor、sdkVersionMinor）
    - 实现 ToolPluginDescriptor::isValid() 方法
    - 定义 ToolPluginHealth 结构（status、diagnostics）
    - _需求: 3.4, 3.9, 10.1-10.8, 41.1-41.7, 54.1-54.7, 57.1-57.4_
  
  - [x] 4.3 创建 BackendTypes.h
    - 定义 BackendDescriptor 结构（backendId、displayName、version、supportsDelegate、supportsTeammate、sdkVersionMajor、sdkVersionMinor）
    - 实现 BackendDescriptor::isValid() 方法
    - _需求: 3.5, 3.9, 10.4, 10.5, 17.7_
  
  - [x] 4.4 创建 CommonTypes.h
    - 定义 AgentConfig 结构（uuid、userName、providerInstanceId、selectedModelId、configId、systemPrompt、executionMode、workspaceDir、recursionDepth）
    - 实现 AgentConfig::isValid() 和 canDelegate() 方法
    - 定义 TeammateConfig 结构（name、role、backend、persistence、workingDirectory、ownerAgentId、turnIdleTimeoutMs、autoCleanup、ephemeralOwnerTurnId、backendOverrides）
    - 定义 TeammateState 结构（id、threadId、activeTurnId、status、lastError、turnCount、createdAtMs、lastActiveAtMs）
    - _需求: 3.6, 3.7, 3.8, 35.1-35.10, 36.1-36.8, 37.1-37.11, 75.1-75.6_

- [x] 5. 创建辅助工具
  - [x] 5.1 创建 ToolSchemaBuilder.h
    - 实现 makePropertySchema() 函数（支持 string、number、boolean、object、array 类型）
    - 实现 makeToolSchema() 函数（接受工具名称、描述、属性对象、必需字段列表）
    - 添加对 enum、pattern、minimum/maximum 约束的支持
    - _需求: 11.1-11.6, 82.1-82.6_
  
  - [x] 5.2 创建 PluginMacros.h
    - 定义 TMAGENT_TOOL_PLUGIN_IID 宏
    - 定义 TMAGENT_BACKEND_PLUGIN_IID 宏
    - 添加 Q_DECLARE_INTERFACE 宏声明
    - _需求: 50.1-50.5_
  
  - [x] 5.3 创建异步工具辅助函数
    - 实现 isDeferredToolResult() 函数
    - 实现 stripDeferredPrefix() 函数
    - _需求: 26.1-26.7_

- [x] 6. 创建版本管理
  - [x] 6.1 创建 version.h
    - 定义 TMAGENT_SDK_VERSION_MAJOR 宏
    - 定义 TMAGENT_SDK_VERSION_MINOR 宏
    - 定义 TMAGENT_SDK_VERSION_PATCH 宏
    - 定义 TMAGENT_SDK_VERSION_STRING 宏
    - _需求: 5.1-5.5, 58.1-58.6_


- [x] 7. 创建构建配置
  - [x] 7.1 创建 qmake 构建配置
    - 创建 tmagent-plugin-sdk.pri 文件
    - 配置 INCLUDEPATH 指向 include/ 目录
    - 列出所有头文件
    - 定义 SDK 版本宏
    - _需求: 1.5, 14.1, 14.3, 14.5, 14.6_
  
  - [x] 7.2 创建 CMake 构建配置
    - 创建 CMakeLists.txt 文件
    - 定义 INTERFACE 库目标
    - 配置头文件安装规则
    - 生成 CMake 配置文件（tmagent-plugin-sdk-config-version.cmake）
    - _需求: 1.5, 14.2, 14.4, 14.5, 14.6, 14.7_

- [x] 8. 创建最小示例插件
  - [x] 8.1 创建最小工具插件示例
    - 创建 examples/minimal-tool-plugin/ 目录
    - 实现 MinimalToolPlugin 类（继承 IToolPlugin）
    - 实现 MinimalToolProvider 类（实现 echo 工具）
    - 创建 minimal_tool.json 元数据文件
    - 创建 MinimalToolPlugin.pro 构建配置
    - 创建 CMakeLists.txt 构建配置
    - 添加 README.md 说明文件
    - _需求: 21.5, 21.7, 73.1, 73.3, 73.4_
  
  - [x] 8.2 创建最小后端插件示例
    - 创建 examples/minimal-backend-plugin/ 目录
    - 实现 MinimalBackendPlugin 类（继承 IBackendPlugin）
    - 实现 MinimalDelegateBackend 类（基础委托功能）
    - 创建 minimal_backend.json 元数据文件
    - 创建构建配置文件
    - 添加 README.md 说明文件
    - _需求: 21.6, 21.7, 73.2, 73.3, 73.4_

- [ ] 9. 编写 SDK 文档
  - [x] 9.1 编写 README.md
    - 添加 SDK 简介和特性说明
    - 添加快速开始指南
    - 添加安装说明
    - 添加基础使用示例
    - _需求: 21.1_
  
  - [x] 9.2 编写 API 参考文档
    - 创建 docs/API.md 文件
    - 文档化所有公开接口（IToolPlugin、IToolProvider、IToolPluginHost、IBackendPlugin、IDelegateBackend、ITeammateBackend）
    - 文档化所有数据结构（Tool、ToolCall、ToolResult、ToolPluginDescriptor、BackendDescriptor、AgentConfig、TeammateConfig、TeammateState）
    - 文档化所有辅助函数
    - _需求: 21.2, 21.9_
  
  - [x] 9.3 编写插件开发教程
    - 创建 docs/TUTORIAL.md 文件
    - 编写从零开始创建工具插件的完整步骤
    - 编写从零开始创建后端插件的完整步骤
    - 添加常见问题和最佳实践
    - _需求: 21.3, 21.8_
  
  - [x] 9.4 编写迁移指南
    - 创建 docs/MIGRATION.md 文件
    - 文档化从旧接口迁移到 SDK 的步骤
    - 提供接口对比表
    - 提供迁移示例代码
    - _需求: 21.4, 21.10_

- [x] 10. Checkpoint - SDK 基础设施验收
  - 验证 SDK 可以独立编译
  - 验证 SDK 大小小于 100KB
  - 验证示例插件可以基于 SDK 编译
  - 验证文档完整性
  - 确保所有测试通过，询问用户是否有问题


### 阶段 2: 主应用适配（3-4 周）

- [ ] 11. 主应用引入 SDK 依赖
  - 修改 TmAgent.pro 引入 SDK 路径
  - 修改 src/core/core.pri 包含 SDK 头文件
  - 更新 INCLUDEPATH 指向 SDK include/ 目录
  - _需求: 1.1, 9.1_

- [ ] 12. 实现 IToolPluginHost 扩展接口
  - [ ] 12.1 创建 ToolPluginHostImpl 类
    - 实现 availableTeammateBackendIds() 方法
    - 实现 availableTools() 方法
    - 实现 executeHostTool() 方法（桥接到 ToolDispatcher）
    - _需求: 4.2, 4.3, 27.1-27.6, 22.1_
  
  - [ ] 12.2 实现日志服务
    - 实现 logDebug()、logInfo()、logWarning()、logError() 方法
    - 在日志中包含插件 ID
    - 使用 Qt 标准日志系统（qDebug/qWarning/qCritical）
    - _需求: 4.4, 19.1-19.7_
  
  - [ ] 12.3 实现配置服务
    - 实现 getPluginConfig() 方法（读取 QJsonObject 格式配置）
    - 实现 setPluginConfig() 方法（保存 QJsonObject 格式配置）
    - 实现 getPluginDataDir() 方法（返回插件专用数据目录）
    - 实现 getAppDataDir() 方法（返回应用数据目录）
    - _需求: 4.5, 4.6, 4.7, 20.1-20.8_
  
  - [ ] 12.4 实现代码解析服务
    - 实现 parseCode() 方法（使用 tree-sitter）
    - 支持常见语言类型（C++、Python、JavaScript 等）
    - 返回 AST 的 QJsonObject 表示
    - _需求: 4.8, 51.1-51.5_

- [ ] 13. 实现适配器类
  - [ ] 13.1 创建 ToolExecutorAdapter 类
    - 实现 IToolExecutor 接口
    - 桥接 executeToolSync() 到 ToolDispatcher::execute()
    - 桥接 executeToolAsync() 到 ToolDispatcher::executeAsync()
    - _需求: 9.7, 22.2, 22.3, 22.4, 47.6_
  
  - [ ] 13.2 创建 ConfigAdapter 类
    - 实现 toSdkConfig() 函数转换 LLMConfig 到 AgentConfig
    - 映射所有必需字段（uuid、userName、providerInstanceId、selectedModelId、configId、systemPrompt、executionMode、workspaceDir、recursionDepth）
    - _需求: 9.5, 22.5, 22.6, 37.1-37.11_
  
  - [ ] 13.3 创建 ModelFactoryAdapter 类
    - 实现 IModelFactory 接口
    - 桥接 createProvider() 到 ModelFactory 的对应方法
    - 桥接 getModelCapabilities() 到 ModelFactory 的对应方法
    - _需求: 9.6, 22.7, 22.8, 48.7_

- [ ] 14. 修改 PluginManager 支持版本检查
  - [ ] 14.1 实现版本兼容性检查
    - 实现 isCompatible() 方法检查 SDK 版本
    - 检查主版本号必须匹配
    - 检查次版本号不能大于 SDK 次版本号
    - _需求: 5.2, 5.3, 6.6, 6.7, 6.8, 28.5, 28.6_
  
  - [ ] 14.2 实现插件加载流程
    - 扫描预定义插件目录（应用内置、用户、系统）
    - 使用 QPluginLoader 加载动态库
    - 验证插件实现 IToolPlugin 或 IBackendPlugin 接口
    - 调用 descriptor() 获取元数据
    - 执行版本兼容性检查
    - 调用 createProvider() 创建提供者实例
    - _需求: 6.1-6.10, 29.1-29.7_
  
  - [ ] 14.3 实现插件搜索路径
    - 定义插件搜索优先级顺序
    - 实现多目录扫描逻辑
    - 处理同名插件冲突（使用优先级最高的）
    - _需求: 29.1-29.7_
  
  - [ ] 14.4 实现插件加载失败处理
    - 记录失败信息到 m_failedPlugins 列表
    - 提供 getFailedPlugins() 方法查询失败列表
    - 提供 retryLoadPlugin() 方法重试加载
    - 记录错误日志并继续加载其他插件
    - _需求: 6.10, 8.1, 19.7, 61.1-61.6_


- [ ] 15. 修改 ToolDispatcher 使用 SDK 接口
  - [ ] 15.1 实现工具调用路由
    - 根据工具名称查找对应的 Provider
    - 调用 Provider::execute() 方法
    - 返回 ToolResult 结构
    - _需求: 7.1-7.3_
  
  - [ ] 15.2 实现异步工具支持
    - 检测 ToolResult 是否包含 "__DEFERRED__" 前缀
    - 监听 Provider 的 toolCompleted 信号
    - 将完成结果传递给 Agent
    - _需求: 7.4-7.8, 26.1-26.7_
  
  - [ ] 15.3 实现异常处理
    - 在插件边界捕获所有 C++ 异常
    - 转换异常为 ToolResult{success=false}
    - 记录详细堆栈信息到日志
    - 确保其他插件继续正常工作
    - _需求: 8.2-8.5, 31.7_
  
  - [ ] 15.4 实现工具名称唯一性检查
    - 维护工具名称到提供者的映射表
    - 检查工具名称是否已存在
    - 记录冲突警告并拒绝注册
    - _需求: 64.1-64.5_
  
  - [ ] 15.5 实现工具调用日志记录
    - 记录调用开始日志（时间戳、工具名称、调用 ID、插件 ID）
    - 记录完成日志（执行时间、成功状态、结果大小）
    - 记录错误日志（错误码、错误信息、堆栈跟踪）
    - _需求: 78.1-78.6_

- [ ] 16. 实现数据验证
  - [ ] 16.1 实现工具调用参数验证
    - 验证工具名称存在
    - 使用 JSON Schema 验证器验证参数
    - 限制字符串长度不超过 1MB
    - 限制数组大小在合理范围内
    - _需求: 17.1-17.4, 60.1-60.5_
  
  - [ ] 16.2 实现插件元数据验证
    - 调用 descriptor.isValid() 方法
    - 验证插件 ID 非空
    - 验证后端插件至少支持一种模式
    - _需求: 17.5-17.7_

- [ ] 17. 实现向后兼容支持
  - [ ] 17.1 创建 LegacyPluginAdapter 类
    - 实现 TmAgent::IToolPlugin 接口
    - 桥接到旧接口（IToolPlugin）
    - 转换旧格式元数据到新格式
    - 标记 sdkVersionMajor=0 表示旧版本
    - _需求: 23.1-23.7_
  
  - [ ] 17.2 修改 PluginManager 支持双接口
    - 首先尝试 SDK 接口（TmAgent::IToolPlugin）
    - 如果失败则回退到旧接口（IToolPlugin）
    - 记录使用旧接口的警告日志
    - _需求: 23.1-23.7_

- [ ] 18. 编写集成测试
  - [ ] 18.1 测试插件加载流程
    - 创建测试插件（新旧接口各一个）
    - 测试 PluginManager 加载插件
    - 验证版本兼容性检查
    - 验证插件元数据正确性
    - _需求: 39.3, 39.4_
  
  - [ ] 18.2 测试工具调用端到端流程
    - 测试同步工具调用
    - 测试异步工具调用
    - 测试工具调用失败场景
    - 测试异常处理
    - _需求: 39.4_
  
  - [ ] 18.3 测试版本兼容性
    - 测试主版本号不匹配的插件被拒绝
    - 测试次版本号过高的插件被拒绝
    - 测试次版本号较低的插件被接受
    - _需求: 39.5_

- [ ] 19. Checkpoint - 主应用适配验收
  - 验证主应用可以加载基于 SDK 的插件
  - 验证现有插件（旧接口）继续工作
  - 验证版本检查机制生效
  - 确保所有集成测试通过，询问用户是否有问题


### 阶段 3: 官方插件迁移（2-3 周）

- [ ] 20. 迁移工具插件 - workspace
  - [ ] 20.1 更新 workspace 插件构建配置
    - 修改 .pro 文件引入 SDK
    - 移除对 src/core/ 的直接依赖
    - 更新 INCLUDEPATH 和 HEADERS
    - _需求: 24.3, 24.4, 24.5_
  
  - [ ] 20.2 更新 workspace 插件接口实现
    - 修改插件类继承 TmAgent::IToolPlugin
    - 更新 descriptor() 方法添加 SDK 版本字段
    - 更新 createProvider() 方法签名
    - _需求: 24.3, 24.4_
  
  - [ ] 20.3 测试 workspace 插件
    - 验证插件可以独立编译
    - 验证所有工具功能正常
    - 运行单元测试和集成测试
    - _需求: 24.6, 24.7_

- [ ] 21. 迁移工具插件 - shell
  - [ ] 21.1 更新 shell 插件构建配置
    - 修改 .pro 文件引入 SDK
    - 移除对 src/core/ 的直接依赖
    - _需求: 24.3, 24.4, 24.5_
  
  - [ ] 21.2 更新 shell 插件接口实现
    - 修改插件类继承 TmAgent::IToolPlugin
    - 更新 descriptor() 和 createProvider() 方法
    - _需求: 24.3, 24.4_
  
  - [ ] 21.3 测试 shell 插件
    - 验证插件可以独立编译
    - 验证所有工具功能正常
    - _需求: 24.6, 24.7, 24.8_

- [ ] 22. 迁移工具插件 - codeintel
  - [ ] 22.1 更新 codeintel 插件构建配置
    - 修改 .pro 文件引入 SDK
    - 移除对 src/core/ 的直接依赖
    - _需求: 24.3, 24.4, 24.5_
  
  - [ ] 22.2 更新 codeintel 插件接口实现
    - 修改插件类继承 TmAgent::IToolPlugin
    - 使用 IToolPluginHost::parseCode() 替代直接依赖 TreeSitterParser
    - _需求: 24.3, 24.4, 51.5_
  
  - [ ] 22.3 测试 codeintel 插件
    - 验证插件可以独立编译
    - 验证代码解析功能正常
    - _需求: 24.6, 24.7, 24.8_

- [ ] 23. 迁移工具插件 - web
  - [ ] 23.1 更新 web 插件构建配置
    - 修改 .pro 文件引入 SDK
    - 移除对 src/core/ 的直接依赖
    - _需求: 24.3, 24.4, 24.5_
  
  - [ ] 23.2 更新 web 插件接口实现
    - 修改插件类继承 TmAgent::IToolPlugin
    - 更新 descriptor() 和 createProvider() 方法
    - _需求: 24.3, 24.4_
  
  - [ ] 23.3 测试 web 插件
    - 验证插件可以独立编译
    - 验证网络请求功能正常
    - _需求: 24.6, 24.7, 24.8_

- [ ] 24. 迁移工具插件 - memory
  - [ ] 24.1 更新 memory 插件构建配置
    - 修改 .pro 文件引入 SDK
    - 移除对 src/core/ 的直接依赖
    - _需求: 24.3, 24.4, 24.5_
  
  - [ ] 24.2 更新 memory 插件接口实现
    - 修改插件类继承 TmAgent::IToolPlugin
    - 使用 IToolPluginHost::getPluginDataDir() 管理数据存储
    - _需求: 24.3, 24.4, 20.6_
  
  - [ ] 24.3 测试 memory 插件
    - 验证插件可以独立编译
    - 验证记忆存储和检索功能正常
    - _需求: 24.6, 24.7, 24.8_

- [ ] 25. 迁移工具插件 - scheduler
  - [ ] 25.1 更新 scheduler 插件构建配置
    - 修改 .pro 文件引入 SDK
    - 移除对 src/core/service/include/SchedulerService.h 的依赖
    - _需求: 24.3, 24.4, 24.5_
  
  - [ ] 25.2 更新 scheduler 插件接口实现
    - 修改插件类继承 TmAgent::IToolPlugin
    - 通过 IToolPluginHost 提供调度服务（如果需要）
    - _需求: 24.3, 24.4_
  
  - [ ] 25.3 测试 scheduler 插件
    - 验证插件可以独立编译
    - 验证调度功能正常
    - _需求: 24.6, 24.7, 24.8_


- [ ] 26. 迁移工具插件 - coordination
  - [ ] 26.1 更新 coordination 插件构建配置
    - 修改 .pro 文件引入 SDK
    - 移除对 src/core/ 的直接依赖
    - _需求: 24.3, 24.4, 24.5_
  
  - [ ] 26.2 更新 coordination 插件接口实现
    - 修改插件类继承 TmAgent::IToolPlugin
    - 使用 IToolPluginHost::availableTeammateBackendIds() 查询后端
    - _需求: 24.3, 24.4, 4.2_
  
  - [ ] 26.3 测试 coordination 插件
    - 验证插件可以独立编译
    - 验证协调功能正常
    - _需求: 24.6, 24.7, 24.8_

- [ ] 27. 迁移后端插件 - codex
  - [ ] 27.1 更新 codex 后端插件构建配置
    - 修改 .pro 文件引入 SDK
    - 移除对 src/core/ 的直接依赖
    - _需求: 24.3, 24.4, 24.5_
  
  - [ ] 27.2 更新 codex 后端插件接口实现
    - 修改插件类继承 TmAgent::IBackendPlugin
    - 实现 createDelegateBackend() 方法
    - 实现 createTeammateBackend() 方法
    - 更新 descriptor() 方法添加 SDK 版本字段
    - _需求: 24.3, 24.4, 65.5, 65.6_
  
  - [ ] 27.3 更新 codex 委托后端实现
    - 修改 DelegateBackend 使用 IToolExecutor 接口替代 ToolDispatcher 指针
    - 修改 DelegateBackend 使用 IModelFactory 接口替代 ModelFactory 指针
    - 更新 DelegateRequest 结构使用 AgentConfig
    - _需求: 9.7, 9.8, 15.5, 15.6_
  
  - [ ] 27.4 更新 codex 队友后端实现
    - 修改 TeammateBackend 使用 TeammateConfig 和 TeammateState POD 结构
    - 使用队友 ID 标识实例，而非 Teammate 指针
    - 实现 CreateResult 和 SendResult 结构
    - _需求: 9.4, 16.1-16.11_
  
  - [ ] 27.5 测试 codex 后端插件
    - 验证插件可以独立编译
    - 验证委托功能正常
    - 验证队友功能正常
    - _需求: 24.6, 24.7, 24.8_

- [ ] 28. 迁移后端插件 - tmagent
  - [ ] 28.1 更新 tmagent 后端插件构建配置
    - 修改 .pro 文件引入 SDK
    - 移除对 src/core/ 的直接依赖
    - _需求: 24.3, 24.4, 24.5_
  
  - [ ] 28.2 更新 tmagent 后端插件接口实现
    - 修改插件类继承 TmAgent::IBackendPlugin
    - 实现 createDelegateBackend() 和 createTeammateBackend() 方法
    - 更新 descriptor() 方法
    - _需求: 24.3, 24.4_
  
  - [ ] 28.3 更新 tmagent 委托和队友后端实现
    - 使用 IToolExecutor 和 IModelFactory 接口
    - 使用 AgentConfig、TeammateConfig、TeammateState POD 结构
    - _需求: 9.4, 9.7, 9.8_
  
  - [ ] 28.4 测试 tmagent 后端插件
    - 验证插件可以独立编译
    - 验证委托和队友功能正常
    - _需求: 24.6, 24.7, 24.8_

- [ ] 29. 回归测试
  - [ ] 29.1 运行完整测试套件
    - 运行所有单元测试
    - 运行所有集成测试
    - 验证所有测试通过
    - _需求: 24.7, 39.7_
  
  - [ ] 29.2 手动测试关键功能
    - 测试所有工具插件的核心功能
    - 测试所有后端插件的核心功能
    - 测试工具间调用
    - 测试委托和队友功能
    - _需求: 24.6_
  
  - [ ] 29.3 性能基准测试
    - 测试插件加载时间（单个 < 50ms，10 个并行 < 200ms）
    - 测试工具调用调度时间（< 10ms）
    - 测试插件内存占用（< 5MB）
    - _需求: 18.1-18.6_

- [ ] 30. Checkpoint - 官方插件迁移验收
  - 验证所有官方插件基于 SDK 编译
  - 验证插件可以独立编译（不依赖主应用源码）
  - 验证功能与迁移前一致
  - 确保所有测试通过，询问用户是否有问题


### 阶段 4: 文档和生态（1-2 周）

- [ ] 31. 完善插件开发教程
  - [ ] 31.1 编写工具插件开发教程
    - 详细说明如何创建工具插件项目
    - 说明如何定义工具 Schema
    - 说明如何实现工具执行逻辑
    - 说明如何使用 IToolPluginHost 回调服务
    - 提供完整的代码示例
    - _需求: 21.3, 21.8_
  
  - [ ] 31.2 编写后端插件开发教程
    - 详细说明如何创建后端插件项目
    - 说明如何实现委托后端
    - 说明如何实现队友后端
    - 说明如何处理回调和会话管理
    - 提供完整的代码示例
    - _需求: 21.3, 21.8_
  
  - [ ] 31.3 编写高级主题教程
    - 说明如何实现异步工具
    - 说明如何实现工具间调用
    - 说明如何处理错误和异常
    - 说明如何优化性能
    - _需求: 21.8_

- [ ] 32. 创建插件模板项目
  - [ ] 32.1 创建工具插件模板
    - 创建完整的项目结构（源文件、头文件、构建配置）
    - 添加占位符代码和注释说明
    - 添加 README 文件说明如何使用模板
    - _需求: 73.1-73.6_
  
  - [ ] 32.2 创建后端插件模板
    - 创建完整的项目结构
    - 添加占位符代码和注释说明
    - 添加 README 文件
    - _需求: 73.2-73.6_

- [ ] 33. 编写迁移指南
  - [ ] 33.1 编写接口迁移指南
    - 提供旧接口到 SDK 接口的对比表
    - 说明每个接口的变更点
    - 提供迁移示例代码
    - _需求: 21.4, 21.10_
  
  - [ ] 33.2 编写数据结构迁移指南
    - 说明 LLMConfig 到 AgentConfig 的转换
    - 说明 Teammate 到 TeammateConfig/TeammateState 的转换
    - 说明 ToolDispatcher 到 IToolExecutor 的转换
    - _需求: 21.10_
  
  - [ ] 33.3 编写构建配置迁移指南
    - 说明如何从旧构建配置迁移到 SDK
    - 提供 qmake 和 CMake 的迁移示例
    - _需求: 21.10_

- [ ] 34. 完善 API 参考文档
  - [ ] 34.1 文档化所有接口
    - 为每个接口方法添加详细说明
    - 添加参数说明和返回值说明
    - 添加使用示例
    - _需求: 21.2, 21.9_
  
  - [ ] 34.2 文档化所有数据结构
    - 为每个字段添加详细说明
    - 添加验证规则说明
    - 添加序列化示例
    - _需求: 21.2, 21.9_
  
  - [ ] 34.3 文档化错误码
    - 列出所有标准错误码
    - 说明每个错误码的含义和恢复策略
    - _需求: 31.1-31.9_

- [ ] 35. 发布准备
  - [ ] 35.1 版本标记和打包
    - 在版本控制系统中创建 v1.0.0 标签
    - 生成发布包（包含头文件、文档、示例）
    - 创建发布说明（Release Notes）
    - _需求: 40.2_
  
  - [ ] 35.2 发布文档
    - 发布 SDK 到公开仓库
    - 发布文档到文档网站
    - 发布示例项目
    - _需求: 40.7_

- [ ] 36. Checkpoint - 文档和生态验收
  - 验证文档完整且易懂
  - 验证第三方开发者可以独立开发插件
  - 验证示例项目可以编译和运行
  - 确认 SDK 1.0.0 正式发布，询问用户是否有问题


## 注意事项

- 标记 `*` 的任务为可选任务，可以跳过以加快 MVP 交付
- 每个任务都引用了具体的需求编号，确保可追溯性
- Checkpoint 任务用于阶段验收，确保增量验证
- 所有插件迁移任务都包含独立编译验证，确保解耦成功
- 测试任务标记为可选，但强烈建议执行以确保质量
- 阶段 1 和阶段 2 是核心任务，必须完成
- 阶段 3 的插件迁移可以分批进行，优先迁移简单插件
- 阶段 4 的文档工作可以与阶段 3 并行进行

## 估算工作量

- 阶段 1（SDK 基础设施）：2-3 周，约 80-120 小时
- 阶段 2（主应用适配）：3-4 周，约 120-160 小时
- 阶段 3（官方插件迁移）：2-3 周，约 80-120 小时
- 阶段 4（文档和生态）：1-2 周，约 40-80 小时

总计：8-12 周，约 320-480 小时

## 风险和缓解措施

- **ABI 不兼容风险**：使用纯虚接口和 POD 结构，在多个平台和编译器上测试
- **性能回退风险**：进行性能基准测试，优化热路径
- **迁移工作量超预期**：分批迁移，保留旧接口支持
- **第三方开发者接受度低**：编写详细文档和教程，收集反馈并改进
- **安全漏洞风险**：实现权限系统，进行数据验证和边界检查

## 成功标准

- SDK 可以独立编译，大小小于 100KB
- 所有官方插件基于 SDK 编译，可以独立编译
- 插件加载时间满足性能要求（单个 < 50ms）
- 工具调用调度时间满足性能要求（< 10ms）
- 文档完整，第三方开发者可以独立开发插件
- 至少有 1 个社区插件成功集成（可选）
