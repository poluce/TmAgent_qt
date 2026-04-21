# Task 29 回归测试 - 完成指南

## 当前状态

✅ **Task 29.1 完成** - 基础测试套件已运行并通过  
🔄 **Task 29.2 进行中** - 手动测试待执行  
🔄 **Task 29.3 进行中** - 性能基准测试待执行

---

## 快速开始

### 选项 1: 运行自动化脚本（推荐）

```powershell
cd tests/plugin-integration
.\run_regression_tests.ps1 -Verbose
```

这将自动运行所有可用的测试并生成报告。

### 选项 2: 分步执行

按照下面的详细步骤手动执行每个测试类别。

---

## Task 29.2: 手动测试关键功能

### 步骤 1: 启动 TmAgent 应用

```powershell
cd ../../build
.\TmAgent.exe
```

### 步骤 2: 打开手动测试清单

在编辑器中打开：`tests/plugin-integration/MANUAL_TEST_CHECKLIST.md`

### 步骤 3: 执行测试

按照清单中的 10 个测试类别逐项测试：

1. **插件加载测试** - 验证所有插件成功加载
2. **工具功能测试** - 测试每个工具的核心功能
3. **工具间调用测试** - 测试工具之间的相互调用
4. **后端功能测试** - 测试委托和队友后端
5. **错误处理测试** - 测试各种错误场景
6. **异步工具测试** - 测试延迟工具结果
7. **配置和日志测试** - 测试配置读写和日志记录
8. **性能观察测试** - 观察加载和执行速度
9. **回归对比测试** - 与迁移前功能对比
10. **边界和压力测试** - 测试边界情况

### 步骤 4: 记录结果

在清单中标记每个测试项的通过/失败状态，并记录任何问题。

### 预期结果

- ✅ 所有工具插件正常工作
- ✅ 所有后端插件正常工作
- ✅ 工具间调用正常
- ✅ 委托和队友功能正常
- ✅ 功能与迁移前一致

---

## Task 29.3: 性能基准测试

### 步骤 1: 构建性能测试

```powershell
cd tests/plugin-integration

# 创建构建目录
mkdir build-perf
cd build-perf

# 使用 qmake 构建
qmake ../performance_benchmark.pro
mingw32-make -j4

cd ..
```

### 步骤 2: 运行性能基准测试

```powershell
.\build-perf\release\performance_benchmark.exe
```

### 步骤 3: 验证性能指标

测试将验证以下性能要求：

| 需求 | 目标 | 测试内容 |
|------|------|---------|
| 18.1 | < 50ms | 单个插件加载时间 |
| 18.2 | < 200ms | 10个插件并行加载 |
| 18.3 | < 10ms | 工具调用调度时间 |
| 18.5 | < 5MB | 插件内存占用 |

### 预期输出示例

```
[Benchmark] Single Plugin Load Time
Requirement: < 50ms per plugin

  ✓ PASS WorkspaceToolsPlugin.dll: 25ms
  ✓ PASS ShellToolsPlugin.dll: 18ms
  ✓ PASS WebToolsPlugin.dll: 32ms
  ✓ PASS MemoryToolsPlugin.dll: 28ms
  ✓ PASS SchedulerToolsPlugin.dll: 22ms

Statistics:
  Min:     18ms
  Max:     32ms
  Average: 25ms
  Target:  < 50ms

[Benchmark] Parallel Plugin Load Time
Requirement: 10 plugins < 200ms

  Plugins tested: 5
  Average time:   120ms
  Target:         < 200ms
  Result:         ✓ PASS

[Benchmark] Tool Call Dispatch Time
Requirement: < 10ms per dispatch

  Iterations:  1000
  Min:         50μs
  Max:         500μs
  Average:     150μs (0.15ms)
  Target:      < 10ms (10000μs)
  Result:      ✓ PASS
```

---

## 构建剩余集成测试（可选）

如果想运行完整的集成测试套件，可以构建以下测试：

### 插件加载集成测试

```powershell
cd tests/plugin-integration
mkdir build-loading
cd build-loading
qmake ../PluginLoadingIntegrationTest.pro
mingw32-make -j4
.\release\PluginLoadingIntegrationTest.exe
cd ..
```

### 版本兼容性测试

```powershell
mkdir build-version
cd build-version
qmake ../VersionCompatibilityTest.pro
mingw32-make -j4
.\release\VersionCompatibilityTest.exe
cd ..
```

### 工具执行集成测试

```powershell
mkdir build-execution
cd build-execution
qmake ../ToolExecutionIntegrationTest.pro
mingw32-make -j4
.\release\ToolExecutionIntegrationTest.exe
cd ..
```

---

## 验收标准

### Task 29.1 ✅
- [x] 所有单元测试通过
- [x] 基础集成测试通过
- [x] 测试框架建立完成

### Task 29.2 🔄
- [ ] 所有工具插件核心功能正常
- [ ] 所有后端插件核心功能正常
- [ ] 工具间调用正常
- [ ] 委托和队友功能正常
- [ ] 功能与迁移前一致

### Task 29.3 🔄
- [ ] 单个插件加载 < 50ms
- [ ] 10个插件并行加载 < 200ms
- [ ] 工具调用调度 < 10ms
- [ ] 插件内存占用 < 5MB

---

## 故障排除

### 问题 1: 测试可执行文件未找到

**解决方案**: 确保已构建测试
```powershell
cd tests/plugin-integration/build-perf
qmake ../performance_benchmark.pro
mingw32-make -j4
```

### 问题 2: 插件未找到

**解决方案**: 确保插件已构建
```powershell
cd build-plugins
qmake ../plugins.pro
mingw32-make -j4
```

### 问题 3: 性能测试失败

**解决方案**:
1. 关闭其他应用程序释放资源
2. 确保使用 Release 构建（不是 Debug）
3. 验证硬件满足最低要求

### 问题 4: 手动测试中工具不工作

**解决方案**:
1. 检查应用日志中的错误信息
2. 验证插件版本与 SDK 版本兼容
3. 确保所有依赖已安装

---

## 完成后的下一步

### 如果所有测试通过 ✅

1. 更新测试报告：`TASK_29_REGRESSION_TEST_REPORT.md`
2. 标记 Task 29 为完成
3. 继续 Task 30: Checkpoint - 官方插件迁移验收
4. 准备进入阶段 4: 文档和生态

### 如果部分测试失败 ⚠️

1. 记录所有失败的测试
2. 按严重程度排序问题
3. 修复关键问题
4. 重新运行失败的测试
5. 更新测试报告

### 如果性能未达标 ⚠️

1. 分析性能瓶颈
2. 优化关键路径
3. 重新运行性能基准测试
4. 记录任何权衡

---

## 参考文档

- 完整测试指南: `REGRESSION_TEST_GUIDE.md`
- 手动测试清单: `MANUAL_TEST_CHECKLIST.md`
- 测试执行报告: `TASK_29_REGRESSION_TEST_REPORT.md`
- 需求文档: `.kiro/specs/plugin-sdk-architecture/requirements.md`
- 设计文档: `.kiro/specs/plugin-sdk-architecture/design.md`

---

## 联系支持

如果遇到问题或需要帮助，请：
1. 查看故障排除部分
2. 检查测试日志中的详细错误信息
3. 参考相关文档

---

**文档版本**: 1.0  
**最后更新**: 2026-04-01  
**状态**: 活跃

