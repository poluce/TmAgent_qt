# TmAgent 开发指南

## 项目构建 (Build Commands)

### 依赖环境
- Qt 5.14.2+
- C++17
- MinGW 7.3+ / MSVC 2017+

### 构建步骤
1. 创建构建目录：
   ```bash
   mkdir build && cd build
   ```
2. 生成 Makefile：
   ```bash
   qmake ../TmAgent.pro
   ```
3. 编译：
   ```bash
   make -j4  # Linux/MinGW
   # 或者
   nmake     # MSVC
   ```

## 代码架构 (Code Architecture)

本项目采用分层架构设计：
- **Core Layer (`src/core`)**: 核心业务逻辑，包括智能体管理 (`agent`)、工具系统 (`tools`)、LSP 协议处理 (`lsp`) 和通用工具 (`utils`)。
- **UI Layer (`src/ui`)**: 基于 Qt Widgets 的用户界面，负责展示聊天记录和工具执行状态。
- **3rdparty**: 集成第三方库 (YAML, Tree-sitter 等)。

## 代码规范 (Code Standards)

### 引入规范 (Imports)
- 优先使用 `<Header>` 格式引入 Qt 和标准库。
- 本地头文件使用 `"Header.h"`。

### 格式化规则 (Formatting)
- **缩进**: 使用 4 个空格。
- **大括号**: 函数和控制流的大括号起始于同一行 (K&R 风格)。

### 命名约定 (Naming Conventions)
- **类名**: PascalCase (e.g., `DeepSeekClient`)
- **函数名**: camelCase (e.g., `postRequest`)
- **成员变量**: `m_` 前缀 + camelCase (e.g., `m_manager`)
- **局部变量/参数**: camelCase (e.g., `config`)

### 错误处理 (Error Handling)
- 使用 Qt 的信号槽机制传递错误 (e.g., `emit errorOccurred(...)`)。
- 网络请求通过检查 `QNetworkReply::error()` 处理异常。
- 资源管理遵循 RAII 原则，利用 Qt 父子对象机制自动管理内存。

## 文件组织 (File Organization)
- `TmAgent.pro`: QMake 项目配置文件。
- `src/`: 源代码根目录。
  - `core/`: 核心逻辑。
  - `ui/`: 界面代码。
- `3rdparty/`: 第三方依赖库。
- `resources/`: 资源文件。
- `tests/`: 测试代码。

## 技术栈 (Tech Stack)
- **语言**: C++17
- **框架**: Qt 5 (Core, Gui, Network, Widgets)
- **构建系统**: qmake
- **依赖管理**: 手动管理 (`3rdparty`) + `.pri` 文件包含
