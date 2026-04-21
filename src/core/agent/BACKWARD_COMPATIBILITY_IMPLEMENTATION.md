# 向后兼容支持实现文档

## 概述

本文档描述了 TmAgent 插件系统向后兼容支持的实现，允许主应用在迁移期间同时支持旧接口和新 SDK 接口的插件。

## 实现的需求

- **需求 23.1-23.7**: 向后兼容支持
  - 主应用首先尝试 SDK 接口（TmAgent::IToolPlugin）
  - 如果失败则回退到旧接口（IToolPlugin）
  - 记录使用旧接口的警告日志
  - 旧插件通过适配器桥接到新接口
  - 旧插件标记 sdkVersionMajor=0 表示旧版本

## 实现的组件

### 1. LegacyPluginAdapter 类

**文件**: 
- `src/core/agent/LegacyPluginAdapter.h`
- `src/core/agent/LegacyPluginAdapter.cpp`

**功能**:
- 实现 `TmAgent::IToolPlugin` 接口
- 包装旧版本的 `::IToolPlugin` 实例
- 转换旧格式元数据到新格式
- 标记 `sdkVersionMajor=0` 表示旧版本插件

**关键方法**:

```cpp
// 转换旧格式元数据到新格式
TmAgent::ToolPluginDescriptor descriptor() const override;

// 桥接 createProvider 调用
TmAgent::IToolProvider* createProvider(TmAgent::IToolPluginHost* host, 
                                      QObject* parent) override;

// 桥接 configureProvider 调用
bool configureProvider(TmAgent::IToolProvider* provider,
                      const QJsonObject& config,
                      QString* error = nullptr) override;

// 转换健康状态格式
TmAgent::ToolPluginHealth health(const TmAgent::IToolProvider* provider) const override;
```

**元数据转换**:
- 复制所有基本字段（pluginId, displayName, version, description, category, toolNames, configSchema）
- 设置 `sdkVersionMajor=0, sdkVersionMinor=0` 标记为旧版本

**健康状态转换**:
- `"ok"` → `"healthy"`
- `"error"` → `"unhealthy"`
- `"disabled"` → `"unhealthy"` (添加诊断信息)
- `"unknown"` → `"unhealthy"` (添加诊断信息)

### 2. ToolPluginManager 双接口支持

**文件**: 
- `src/core/agent/ToolPluginManager.h`
- `src/core/agent/ToolPluginManager.cpp`

**修改内容**:

#### LoadedPlugin 结构扩展

```cpp
struct LoadedPlugin {
    // ... 现有字段 ...
    IToolPlugin* plugin = nullptr;              // 旧接口插件（已弃用）
    TmAgent::IToolPlugin* sdkPlugin = nullptr;  // SDK 接口插件（推荐）
    LegacyPluginAdapter* adapter = nullptr;     // 旧插件适配器
    bool isLegacy = false;                      // 标记是否使用旧接口
    // ... 其他字段 ...
};
```

#### tryLoadPlugin 方法更新

**加载流程**:

1. **首先尝试 SDK 接口** (`TmAgent::IToolPlugin`):
   ```cpp
   auto* sdkPlugin = qobject_cast<TmAgent::IToolPlugin*>(instance);
   if (sdkPlugin) {
       // 使用 SDK 接口
       loaded.sdkPlugin = sdkPlugin;
       loaded.isLegacy = false;
       // 执行版本兼容性检查
   }
   ```

2. **回退到旧接口** (`::IToolPlugin`):
   ```cpp
   else {
       auto* legacyPlugin = qobject_cast<::IToolPlugin*>(instance);
       if (legacyPlugin) {
           // 创建适配器包装旧插件
           loaded.adapter = new LegacyPluginAdapter(legacyPlugin, this);
           loaded.plugin = legacyPlugin;
           loaded.sdkPlugin = loaded.adapter;
           loaded.isLegacy = true;
           // 记录警告日志
       }
   }
   ```

3. **如果两者都不支持**:
   ```cpp
   else {
       // 记录错误并拒绝加载
       recordFailedPlugin(filePath, metaDescriptor.pluginId, 
                         "Does not implement IToolPlugin or TmAgent::IToolPlugin interface");
   }
   ```

#### isCompatible 方法更新

```cpp
bool ToolPluginManager::isCompatible(const TmAgent::ToolPluginDescriptor& descriptor) const
{
    // 旧版本插件（sdkVersionMajor=0）始终兼容
    if (descriptor.sdkVersionMajor == 0) {
        return true;
    }
    
    // SDK 插件的版本兼容性检查
    if (descriptor.sdkVersionMajor != TMAGENT_SDK_VERSION_MAJOR) {
        return false;
    }
    
    if (descriptor.sdkVersionMinor > TMAGENT_SDK_VERSION_MINOR) {
        return false;
    }
    
    return true;
}
```

#### applyConfigToLoadedPlugins 方法更新

所有插件操作现在通过 `loaded.sdkPlugin` 接口进行：

```cpp
// 使用 sdkPlugin 接口（对于旧插件，这是适配器）
loaded.sdkPlugin->configureProvider(loaded.provider, loaded.config, &configureError);
healthInfo = loaded.sdkPlugin->health(loaded.provider);
```

## 日志输出

### SDK 接口插件加载

```
[ToolPluginManager] detected SDK interface plugin: my-plugin
[ToolPluginManager] loaded tool plugin: my-plugin from /path/to/plugin.dll (SDK interface)
```

### 旧接口插件加载

```
[ToolPluginManager] detected legacy interface plugin: old-plugin - This plugin should be migrated to the SDK interface
[LegacyPluginAdapter] Wrapping legacy plugin interface. This plugin should be migrated to the SDK interface.
[ToolPluginManager] wrapped legacy plugin with adapter: old-plugin
[ToolPluginManager] loaded tool plugin: old-plugin from /path/to/plugin.dll (legacy interface)
```

### 版本兼容性检查

```
[ToolPluginManager] legacy plugin detected (sdkVersionMajor=0): old-plugin - always compatible
```

## 构建配置

**文件**: `src/core/agent/agent.pri`

添加了 LegacyPluginAdapter 到构建配置：

```qmake
# Adapter classes - Bridge SDK interfaces to main application
SOURCES += \
    $PWD/ToolExecutorAdapter.cpp \
    $PWD/ConfigAdapter.cpp \
    $PWD/ModelFactoryAdapter.cpp \
    $PWD/LegacyPluginAdapter.cpp

HEADERS += \
    $PWD/ToolExecutorAdapter.h \
    $PWD/ConfigAdapter.h \
    $PWD/ModelFactoryAdapter.h \
    $PWD/LegacyPluginAdapter.h
```

## 使用场景

### 场景 1: 加载 SDK 接口插件

1. PluginManager 扫描插件目录
2. 使用 QPluginLoader 加载插件
3. 尝试 `qobject_cast<TmAgent::IToolPlugin*>(instance)`
4. 成功 → 直接使用 SDK 接口
5. 执行版本兼容性检查
6. 创建 Provider 并注册

### 场景 2: 加载旧接口插件

1. PluginManager 扫描插件目录
2. 使用 QPluginLoader 加载插件
3. 尝试 `qobject_cast<TmAgent::IToolPlugin*>(instance)` → 失败
4. 回退到 `qobject_cast<::IToolPlugin*>(instance)` → 成功
5. 创建 LegacyPluginAdapter 包装旧插件
6. 记录警告日志
7. 使用适配器作为 SDK 接口
8. 创建 Provider 并注册

### 场景 3: 插件不支持任何接口

1. PluginManager 扫描插件目录
2. 使用 QPluginLoader 加载插件
3. 尝试 `qobject_cast<TmAgent::IToolPlugin*>(instance)` → 失败
4. 回退到 `qobject_cast<::IToolPlugin*>(instance)` → 失败
5. 记录错误到失败列表
6. 继续加载其他插件

## 迁移路径

### 阶段 1: 双接口支持期（当前）

- 主应用同时支持旧接口和 SDK 接口
- 旧插件通过适配器继续工作
- 记录警告日志提示迁移

### 阶段 2: 迁移期

- 官方插件迁移到 SDK 接口
- 第三方开发者收到迁移指南
- 旧接口标记为 deprecated

### 阶段 3: 完全迁移

- 移除旧接口支持
- 移除 LegacyPluginAdapter
- 完全基于 SDK 架构

## 测试建议

### 单元测试

1. **LegacyPluginAdapter 测试**:
   - 测试元数据转换正确性
   - 测试健康状态转换正确性
   - 测试方法调用桥接

2. **ToolPluginManager 测试**:
   - 测试 SDK 接口插件加载
   - 测试旧接口插件加载
   - 测试版本兼容性检查
   - 测试适配器创建

### 集成测试

1. 创建测试插件（SDK 接口）
2. 创建测试插件（旧接口）
3. 验证两者都能正常加载
4. 验证工具调用功能正常
5. 验证配置和健康检查功能

## 注意事项

1. **内存管理**: 
   - LegacyPluginAdapter 由 ToolPluginManager 管理（通过 Qt 父子对象机制）
   - 旧插件实例由 QPluginLoader 管理
   - 适配器不拥有旧插件的所有权

2. **线程安全**: 
   - 所有插件操作在主线程执行
   - 适配器不引入额外的线程安全问题

3. **性能影响**: 
   - 适配器引入的开销极小（仅数据结构转换）
   - 不影响工具执行性能

4. **兼容性**: 
   - 旧插件标记 sdkVersionMajor=0 始终兼容
   - SDK 插件遵循语义化版本规则

## 相关文件

- `src/core/agent/LegacyPluginAdapter.h` - 适配器头文件
- `src/core/agent/LegacyPluginAdapter.cpp` - 适配器实现
- `src/core/agent/ToolPluginManager.h` - 插件管理器头文件
- `src/core/agent/ToolPluginManager.cpp` - 插件管理器实现
- `src/core/agent/agent.pri` - 构建配置
- `src/core/agent/IToolPlugin.h` - 旧接口定义
- `tmagent-plugin-sdk/include/tmagent/plugin/IToolPlugin.h` - SDK 接口定义

## 参考需求

- 需求 23.1: 首先尝试 SDK 接口
- 需求 23.2: 回退到旧接口
- 需求 23.3: 记录警告日志
- 需求 23.4: 实现适配器类
- 需求 23.5: 转换元数据格式
- 需求 23.6: 标记旧版本
- 需求 23.7: 确保新旧插件都能正常工作
