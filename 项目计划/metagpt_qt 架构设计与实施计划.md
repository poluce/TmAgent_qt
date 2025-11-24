metagpt_qt 架构设计与实施计划
项目概述
项目背景与痛点
现状: MetaGPT 通过引入 SOP(标准作业程序)和多智能体协作,极大提高了 AI 生成代码的质量。它按照"需求 → 设计 → 实现"的流程工作,而不仅仅是代码补全。

技术栈断层: MetaGPT 原生基于 Python 开发,高度依赖 Python 运行时环境。而 Qt 客户端开发主要使用 C++,构建系统为 qmake/CMake。

困境: Qt C++ 开发者想利用 MetaGPT 能力时,需要频繁切换环境,且生成的代码往往缺乏 Qt 特有的工程结构(.pro 配置、资源文件、信号槽宏定义)。

项目目标
metagpt_qt 旨在构建一个 Qt C++ 原生 的智能开发环境:

借鉴而非移植: 借鉴 MetaGPT 的核心流程思想,但摒弃其厚重的 Python 依赖框架
控制权反转: 将 Python 降级为单纯的"逻辑推理内核",工程控制权交还给 Qt C++
领域专精: 生成符合 Qt 5.14.2/C++11 标准的高质量代码
闭环验证: 集成本地编译器,实现代码生成-编译-反馈的完整闭环
IMPORTANT

核心理念: Qt C++ 拥有绝对控制权,Python 仅作为轻量级推理内核。所有工程管理、流程控制、文件操作、编译验证均由 Qt C++ 完成。

MetaGPT 架构分析
核心架构
MetaGPT 采用双层架构设计:

1. 基础组件层 (Foundational Components Layer)
基础组件层
Environment协作环境
Memory记忆系统
Roles角色定义
Actions原子操作
Tools工具集
Environment: 提供协作工作空间和通信平台
Memory: 存储和检索历史消息与上下文
Roles: 封装专业技能、行为和工作流(如产品经理、架构师、工程师)
Actions: 执行特定任务的原子单元
Tools: 增强智能体能力的工具集合
2. 协作层 (Collaboration Layer)
知识共享Knowledge Sharing
工作流封装SOP Workflows
消息总线Message Pool
角色协作Multi-Agent
知识共享: 通过全局消息池实现高效信息交换
SOP 工作流: 将复杂任务分解为标准化子任务
消息机制: 基于角色订阅的透明消息访问
多智能体协作: 专业化角色协同工作
核心组件详解
Role (角色)
MetaGPT 定义了模拟软件公司的专业角色:

Product Manager: 分析需求,生成 PRD(产品需求文档)
Architect: 设计系统架构、类图、序列图
Project Manager: 分解任务,分配工作
Engineer: 编写代码实现
QA Engineer: 质量保证和测试
每个角色包含:

Profile(档案): 名称、角色定位
Goal(目标): 角色的核心职责
Constraints(约束): 工作规范和限制
Actions(能力): 可执行的操作集合
Action (动作)
Action 是智能体执行任务的原子单元:

通过自然语言描述任务
角色选择合适的 Action 设为 todo
执行 Action 产生结构化输出
Message (消息)
采用全局消息池机制:

智能体发布结构化消息到全局池
基于角色兴趣订阅相关消息类型
透明访问其他智能体的消息
减少显式通信开销
SOP (标准操作流程)
SOP 是 MetaGPT 的核心创新:

任务分解: 将复杂任务拆解为可管理的子任务
结构化输出: 强制生成标准化文档(PRD、设计文档、接口规范)
流程协调: 定义智能体间的协作顺序和交接标准
减少幻觉: 通过标准化约束降低 LLM 的不一致性
典型 SOP 流程:

需求分析 → PRD 编写 → 架构设计 → 任务分解 → 代码实现 → 测试验证
metagpt_qt 架构设计
设计原则
Qt C++ 绝对控制:

所有工程管理(文件创建、目录结构、.pro 生成)由 Qt C++ 完成
所有流程控制(SOP 状态机、智能体调度)由 Qt C++ 完成
所有外部工具调用(编译器、qmake)由 Qt C++ 完成
Python 极简化:

Python 仅作为"推理内核",职责限定为:
调用 LLM API(OpenAI/Claude)
拼接 Prompt 模板
解析 LLM 返回的 Markdown
Python 不参与任何工程决策和文件操作
Python 以无状态服务方式运行,随时可替换
进程隔离与通信:

双进程架构,Qt C++ 为主进程,Python 为子进程
通过标准输入/输出的 JSON-RPC 通信
Python 进程崩溃不影响 Qt 主程序
领域专精:

针对 Qt 5.14.2/C++11 深度定制 Prompt
强制生成符合 Qt 工程规范的代码
闭环验证:

集成本地编译器实现自动验证
编译错误自动反馈给 LLM 修复
架构对比
组件	MetaGPT (Python)	metagpt_qt (Qt C++ + Python)	职责划分
Environment	Python 运行时环境	QtEnvironment (C++) + WorkspaceManager	Qt C++: 完全控制工程结构
Memory	Python 对象存储	MessageStore (C++) + SQLite	Qt C++: 持久化存储,会话管理
Roles	Python 类继承	AgentManager (C++) + Prompt 模板	Qt C++: 角色调度; Python: 提供 Prompt
Actions	Python 函数	ActionExecutor (C++) + QProcess	Qt C++: 动作编排; Python: LLM 推理
Tools	Python 库	CompilerBridge、QmakeWrapper	Qt C++: 独占工具链调用
Message Bus	Python 字典	Qt Signals & Slots	Qt C++: 原生异步消息机制
SOP	Python 流程控制	WorkflowEngine (C++)	Qt C++: 状态机驱动工作流
LLM 交互	Python 内置	PythonProxy (C++) → Python 子进程	Python: 仅负责 LLM API 调用
系统架构图
外部系统
Python 进程 (计算)
Qt C++ 进程 (主控)
用户输入
编译结果
JSON-RPC
编译命令
编译日志
GUIQt Widgets
WorkflowEngine工作流引擎
AgentManager智能体管理器
ActionExecutor动作执行器
CompilerBridge编译器桥接
PythonProxyPython 代理
MessageStore消息存储
WorkspaceManager工作空间管理
LLMClientLLM 客户端
PromptTemplate提示词模板
MarkdownParserMarkdown 解析器
qmake/MSVC编译器
OpenAI/ClaudeLLM API
核心组件设计
1. Qt C++ 端组件
WorkflowEngine (工作流引擎)
职责: 管理 SOP 流程的执行

class WorkflowEngine : public QObject {
    Q_OBJECT
public:
    enum State {
        Idle,
        RequirementAnalysis,  // 需求分析
        PRDGeneration,        // PRD 生成
        ArchitectureDesign,   // 架构设计
        TaskDecomposition,    // 任务分解
        CodeGeneration,       // 代码生成
        Compilation,          // 编译验证
        Finished
    };
    
    void startWorkflow(const QString& userRequirement);
    void transitionTo(State nextState);
    
signals:
    void stateChanged(State newState);
    void workflowCompleted(bool success);
    
private:
    State m_currentState;
    QStateMachine* m_stateMachine;
};
特点:

使用 QStateMachine 实现状态机
每个状态对应 SOP 的一个阶段
支持状态回退和错误恢复
AgentManager (智能体管理器)
职责: 管理不同角色的智能体

class AgentManager : public QObject {
    Q_OBJECT
public:
    enum Role {
        QtProductManager,   // Qt 产品经理
        QtArchitect,        // Qt 架构师
        QtEngineer,         // Qt 工程师
        QtQAEngineer        // Qt 测试工程师
    };
    
    void executeRole(Role role, const QString& task);
    QJsonObject getRolePrompt(Role role);
    
signals:
    void roleCompleted(Role role, const QJsonObject& result);
    
private:
    QMap<Role, QString> m_promptTemplates;
    MessageStore* m_messageStore;
};
特点:

每个角色对应专门的 Prompt 模板
模板中包含 Qt 5.14.2/C++11 的严格约束
通过 MessageStore 访问历史上下文
ActionExecutor (动作执行器)
职责: 执行具体的 Action,协调 Python 和编译器

class ActionExecutor : public QObject {
    Q_OBJECT
public:
    void executePythonAction(const QString& script, const QJsonObject& params);
    void executeCompileAction(const QString& proFile);
    
signals:
    void actionCompleted(const QJsonObject& result);
    void actionFailed(const QString& error);
    
private:
    PythonProxy* m_pythonProxy;
    CompilerBridge* m_compilerBridge;
};
PythonProxy (Python 代理)
职责: 管理 Python 子进程,实现 IPC 通信

class PythonProxy : public QObject {
    Q_OBJECT
public:
    void startPythonProcess();
    void sendRequest(const QJsonObject& request);
    
signals:
    void responseReceived(const QJsonObject& response);
    
private:
    QProcess* m_pythonProcess;
    QByteArray m_buffer;
    
    void parseJsonRPC(const QByteArray& data);
};
通信协议: JSON-RPC 2.0

请求示例:

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "generate_code",
  "params": {
    "role": "QtEngineer",
    "task": "实现一个带信号槽的按钮类",
    "context": {
      "previous_messages": [...],
      "project_structure": {...}
    }
  }
}
响应示例:

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "files": [
      {
        "path": "mybutton.h",
        "content": "..."
      },
      {
        "path": "mybutton.cpp",
        "content": "..."
      }
    ],
    "explanation": "..."
  }
}
CompilerBridge (编译器桥接)
职责: 调用本地 qmake/编译器,解析编译结果

class CompilerBridge : public QObject {
    Q_OBJECT
public:
    void compile(const QString& proFile);
    void parseCompileLog(const QString& log);
    
signals:
    void compileSucceeded();
    void compileFailed(const QStringList& errors);
    
private:
    QString m_qmakePath;
    QString m_compilerPath;
};
特点:

自动检测系统中的 qmake 和编译器
解析编译错误,提取关键信息反馈给 LLM
支持增量编译
MessageStore (消息存储)
职责: 持久化存储智能体间的消息

class MessageStore : public QObject {
    Q_OBJECT
public:
    void storeMessage(const Message& msg);
    QList<Message> getMessagesByRole(AgentManager::Role role);
    QList<Message> getRecentMessages(int count);
    
private:
    QSqlDatabase m_db;
};
struct Message {
    QString id;
    AgentManager::Role sender;
    AgentManager::Role receiver;
    QString content;
    QDateTime timestamp;
    QString messageType;  // "PRD", "Architecture", "Code", etc.
};
WorkspaceManager (工作空间管理)
职责: 管理生成的 Qt 项目文件

class WorkspaceManager : public QObject {
    Q_OBJECT
public:
    void createProject(const QString& projectName);
    void addFile(const QString& relativePath, const QString& content);
    void generateProFile(const QStringList& sources, const QStringList& headers);
    
    QString projectPath() const;
    
private:
    QString m_workspacePath;
    QString m_currentProject;
};
2. Python 端组件
CAUTION

Python 端必须保持极度轻量化,严格限定职责范围:

✅ 允许: 调用 LLM API、拼接 Prompt、解析 Markdown
❌ 禁止: 文件操作、工程管理、流程控制、状态存储
Python 端仅作为无状态的推理服务:

LLMClient (LLM 客户端)
class LLMClient:
    """LLM API 客户端"""
    
    def __init__(self, api_key: str, model: str = "gpt-4"):
        self.api_key = api_key
        self.model = model
    
    async def chat(self, messages: List[Dict]) -> str:
        """调用 LLM API"""
        # 调用 OpenAI/Claude API
        pass
PromptTemplate (提示词模板)
class PromptTemplate:
    """Qt 专属提示词模板"""
    
    QT_ARCHITECT_TEMPLATE = """
你是一位资深的 Qt C++ 架构师。
约束条件:
1. 必须使用 Qt 5.14.2 API
2. 必须使用 C++11 标准
3. 头文件(.h)和源文件(.cpp)必须分离
4. 必须正确使用 Q_OBJECT 宏和 MOC
5. 信号槽连接必须类型安全
任务: {task}
上下文: {context}
请生成符合以上约束的设计方案,输出格式为 Markdown。
"""
    
    QT_ENGINEER_TEMPLATE = """
你是一位精通 Qt 的 C++ 工程师。
代码规范:
1. 使用 Qt 5.14.2 API,不使用更高版本特性
2. 遵循 C++11 标准,不使用 C++14/17 特性
3. 头文件必须包含 include guards
4. 类声明必须在 .h 文件,实现在 .cpp 文件
5. 继承自 QObject 的类必须添加 Q_OBJECT 宏
6. 信号声明在 signals: 区域,槽声明在 public/private slots:
任务: {task}
请生成完整的 .h 和 .cpp 文件,确保可以直接编译通过。
"""
MarkdownParser (Markdown 解析器)
class MarkdownParser:
    """解析 LLM 返回的 Markdown 格式代码"""
    
    def extract_code_blocks(self, markdown: str) -> List[CodeBlock]:
        """提取代码块"""
        pass
    
    def parse_file_structure(self, markdown: str) -> Dict[str, str]:
        """解析文件结构"""
        # 返回 {"myclass.h": "...", "myclass.cpp": "..."}
        pass
JSON-RPC 服务器
设计理念: Python 端是纯粹的请求-响应服务,不维护任何状态。

import asyncio
import json
import sys
class MetaGPTService:
    """无状态 JSON-RPC 服务,仅负责 LLM 推理"""
    
    def __init__(self):
        self.llm_client = LLMClient(api_key=os.getenv("OPENAI_API_KEY"))
        self.prompt_template = PromptTemplate()
        self.parser = MarkdownParser()
    
    async def handle_request(self, request: dict) -> dict:
        """处理请求"""
        method = request["method"]
        params = request["params"]
        
        if method == "generate_code":
            return await self.generate_code(params)
        elif method == "generate_prd":
            return await self.generate_prd(params)
        # ...
    
    async def generate_code(self, params: dict) -> dict:
        """生成代码"""
        role = params["role"]
        task = params["task"]
        context = params.get("context", {})
        
        # 构建 Prompt
        if role == "QtEngineer":
            prompt = self.prompt_template.QT_ENGINEER_TEMPLATE.format(
                task=task,
                context=json.dumps(context, ensure_ascii=False)
            )
        
        # 调用 LLM
        response = await self.llm_client.chat([
            {"role": "system", "content": prompt},
            {"role": "user", "content": task}
        ])
        
        # 解析代码
        files = self.parser.parse_file_structure(response)
        
        return {
            "files": [
                {"path": path, "content": content}
                for path, content in files.items()
            ],
            "explanation": response
        }
    
    async def run(self):
        """运行服务"""
        while True:
            line = await asyncio.get_event_loop().run_in_executor(
                None, sys.stdin.readline
            )
            if not line:
                break
            
            request = json.loads(line)
            response = await self.handle_request(request)
            
            result = {
                "jsonrpc": "2.0",
                "id": request["id"],
                "result": response
            }
            print(json.dumps(result), flush=True)
if __name__ == "__main__":
    service = MetaGPTService()
    asyncio.run(service.run())
工作流程示例
以"创建一个带信号槽的按钮类"为例:

WorkspaceManager
MessageStore
CompilerBridge
Python LLM
PythonProxy
AgentManager
WorkflowEngine
Qt GUI
用户
WorkspaceManager
MessageStore
CompilerBridge
Python LLM
PythonProxy
AgentManager
WorkflowEngine
Qt GUI
用户
重新生成代码,循环直到成功
alt
[编译成功]
[编译失败]
输入需求
startWorkflow("创建按钮类")
转换到 RequirementAnalysis
executeRole(QtProductManager)
sendRequest(generate_prd)
JSON-RPC 请求
调用 GPT-4 生成 PRD
返回 PRD
PRD 文档
存储 PRD 消息
转换到 ArchitectureDesign
executeRole(QtArchitect)
sendRequest(generate_architecture)
JSON-RPC 请求(携带 PRD 上下文)
生成类设计
返回设计文档
设计文档
存储设计消息
转换到 CodeGeneration
executeRole(QtEngineer)
sendRequest(generate_code)
JSON-RPC 请求(携带设计上下文)
生成 .h 和 .cpp
返回代码文件
代码文件
写入文件
转换到 Compilation
compile(project.pro)
调用 qmake + 编译器
compileSucceeded()
显示成功
compileFailed(errors)
executeRole(QtEngineer, 修复错误)
关键技术点
1. Qt 专属 Prompt 工程
在 Prompt 中强制约束:

## 严格约束
### Qt 版本约束
- 只能使用 Qt 5.14.2 的 API
- 禁止使用 Qt 5.15+ 的新特性(如 QStringView)
- 禁止使用 Qt 6.x 的任何特性
### C++ 标准约束
- 只能使用 C++11 标准
- 禁止使用 C++14 的 auto 返回类型推导
- 禁止使用 C++17 的结构化绑定
### 代码组织约束
- 头文件(.h)必须包含:
  - Include guards (#ifndef/#define/#endif)
  - 必要的 #include
  - 类声明
  - Q_OBJECT 宏(如果继承 QObject)
  
- 源文件(.cpp)必须包含:
  - 对应头文件的 #include
  - 成员函数实现
  
### 信号槽约束
- 信号必须在 signals: 区域声明
- 槽必须在 public slots: 或 private slots: 区域声明
- 连接必须使用新式语法: connect(sender, &Class::signal, receiver, &Class::slot)
## 输出格式
请按以下格式输出:
### myclass.h
\`\`\`cpp
// 完整的头文件代码
\`\`\`
### myclass.cpp
\`\`\`cpp
// 完整的源文件代码
\`\`\`
### 设计说明
- 简要说明设计思路
- 列出使用的 Qt 类和机制
2. 编译错误反馈循环
void WorkflowEngine::handleCompilationFailure(const QStringList& errors) {
    // 提取关键错误信息
    QJsonArray errorArray;
    for (const QString& error : errors) {
        errorArray.append(parseCompilerError(error));
    }
    
    // 构建修复请求
    QJsonObject request;
    request["role"] = "QtEngineer";
    request["task"] = "修复以下编译错误";
    request["context"] = QJsonObject{
        {"errors", errorArray},
        {"previous_code", m_workspaceManager->getFileContents()},
        {"attempt", m_compilationAttempts}
    };
    
    // 重新生成代码
    m_agentManager->executeRole(AgentManager::QtEngineer, request);
    
    // 限制最大尝试次数
    if (++m_compilationAttempts > 3) {
        emit workflowFailed("编译失败次数过多");
    }
}
3. 消息订阅机制
模拟 MetaGPT 的消息池:

class MessageStore : public QObject {
public:
    // 发布消息
    void publish(const Message& msg) {
        m_messages.append(msg);
        emit messagePublished(msg);
        
        // 通知订阅者
        for (auto role : m_subscriptions[msg.messageType]) {
            emit messageForRole(role, msg);
        }
    }
    
    // 订阅消息类型
    void subscribe(AgentManager::Role role, const QString& messageType) {
        m_subscriptions[messageType].append(role);
    }
    
    // 获取相关消息
    QList<Message> getRelevantMessages(AgentManager::Role role) {
        QList<Message> relevant;
        for (const auto& msg : m_messages) {
            if (m_subscriptions[msg.messageType].contains(role)) {
                relevant.append(msg);
            }
        }
        return relevant;
    }
    
signals:
    void messagePublished(const Message& msg);
    void messageForRole(AgentManager::Role role, const Message& msg);
    
private:
    QList<Message> m_messages;
    QMap<QString, QList<AgentManager::Role>> m_subscriptions;
};
初始化订阅关系:

void AgentManager::setupSubscriptions() {
    // QtArchitect 订阅 PRD
    m_messageStore->subscribe(QtArchitect, "PRD");
    
    // QtEngineer 订阅 Architecture
    m_messageStore->subscribe(QtEngineer, "Architecture");
    
    // QtQAEngineer 订阅 Code
    m_messageStore->subscribe(QtQAEngineer, "Code");
}
项目结构
metagpt_qt/
├── src/                          # Qt C++ 源码
│   ├── core/                     # 核心组件
│   │   ├── workflow_engine.h/cpp
│   │   ├── agent_manager.h/cpp
│   │   ├── action_executor.h/cpp
│   │   ├── message_store.h/cpp
│   │   └── workspace_manager.h/cpp
│   ├── bridge/                   # 桥接组件
│   │   ├── python_proxy.h/cpp
│   │   └── compiler_bridge.h/cpp
│   ├── ui/                       # 界面
│   │   ├── mainwindow.h/cpp
│   │   └── workflow_widget.h/cpp
│   └── main.cpp
├── python/                       # Python 端
│   ├── metagpt_service.py       # JSON-RPC 服务
│   ├── llm_client.py            # LLM 客户端
│   ├── prompt_template.py       # Prompt 模板
│   └── markdown_parser.py       # Markdown 解析
├── prompts/                      # Prompt 模板文件
│   ├── qt_product_manager.txt
│   ├── qt_architect.txt
│   ├── qt_engineer.txt
│   └── qt_qa_engineer.txt
├── tests/                        # 测试
│   ├── test_workflow.cpp
│   └── test_python_integration.py
├── examples/                     # 示例项目
│   └── simple_button/
├── metagpt_qt.pro               # qmake 工程文件
└── README.md
实施计划
第一阶段: 基础框架 (2周)
任务清单
 Qt 端核心组件

 实现 WorkflowEngine 基础状态机
 实现 AgentManager 角色管理
 实现 MessageStore SQLite 存储
 实现 WorkspaceManager 文件管理
 Python 端服务

 实现 JSON-RPC 服务器
 实现 LLMClient (支持 OpenAI API)
 实现 MarkdownParser 代码提取
 进程通信

 实现 PythonProxy 子进程管理
 实现 JSON-RPC 协议
 编写通信测试用例
验证方式
单元测试

cd tests
qmake test.pro
make
./test_workflow
集成测试

启动 Qt 程序
验证 Python 子进程正常启动
发送测试请求,验证响应格式正确
第二阶段: Prompt 工程 (1周)
任务清单
 编写 Qt 专属 Prompt

 QtProductManager Prompt (需求分析)
 QtArchitect Prompt (架构设计)
 QtEngineer Prompt (代码生成)
 QtQAEngineer Prompt (测试生成)
 Prompt 测试

 测试生成简单 QWidget 类
 测试生成带信号槽的类
 测试生成 Model-View 结构
 验证代码符合 Qt 5.14.2/C++11
验证方式
手动测试

使用 ChatGPT/Claude 测试 Prompt
验证生成的代码可以编译通过
检查代码风格和规范
自动化测试

pytest tests/test_prompts.py
第三阶段: 编译器集成 (1周)
任务清单
 CompilerBridge 实现

 自动检测 qmake 路径
 自动检测编译器(MSVC/MinGW)
 实现编译日志解析
 提取编译错误关键信息
 错误反馈循环

 实现编译失败重试机制
 将编译错误反馈给 LLM
 验证修复后的代码
验证方式
编译测试

CompilerBridge bridge;
bridge.compile("examples/simple_button/simple_button.pro");
// 验证编译成功或失败处理正确
错误恢复测试

故意生成有错误的代码
验证系统能自动修复
第四阶段: 完整工作流 (1周)
任务清单
 SOP 流程实现

 需求分析 → PRD 生成
 PRD → 架构设计
 架构设计 → 代码生成
 代码生成 → 编译验证
 UI 开发

 主窗口设计
 工作流可视化
 实时日志显示
 生成结果展示
验证方式
端到端测试

输入需求: "创建一个计数器应用,有增加和减少按钮"
验证生成完整的 Qt 项目
验证项目可以编译运行
验证功能正确
用户验收测试

邀请 Qt 开发者试用
收集反馈和改进建议
第五阶段: 优化和文档 (1周)
任务清单
 性能优化

 优化 LLM 调用次数
 实现增量编译
 优化消息存储查询
 文档编写

 用户手册
 开发者文档
 API 参考
 示例教程
验证方式
性能测试

测试生成中等复杂度项目的耗时
目标: 5分钟内完成
文档审查

确保文档完整清晰
示例代码可运行
技术风险与应对
风险1: LLM 生成代码质量不稳定
应对措施:

使用严格的 Prompt 约束
实现编译验证反馈循环
限制最大重试次数(3次)
记录失败案例,持续优化 Prompt
风险2: 编译器检测失败
应对措施:

支持手动配置编译器路径
提供常见编译器路径列表
详细的错误提示
风险3: Python 进程通信异常
应对措施:

实现心跳检测机制
进程崩溃自动重启
请求超时处理(30秒)
详细的日志记录
风险4: Qt 版本兼容性
应对措施:

在 Prompt 中明确禁止使用高版本 API
提供 Qt 5.14.2 API 白名单
编译器会自动检测不兼容的代码
后续扩展方向
支持更多 Qt 模块

Qt Quick/QML 支持
Qt Network 网络编程
Qt Multimedia 多媒体
增强智能体能力

代码重构智能体
性能优化智能体
文档生成智能体
集成开发工具

Qt Creator 插件
VS Code 扩展
云端服务

在线版本
团队协作功能
代码库共享
总结
metagpt_qt 项目通过以下创新实现了 MetaGPT 架构在 Qt 生态的移植:

双进程架构: Qt C++ 负责系统控制,Python 负责 LLM 交互,职责清晰
领域专精: 针对 Qt 5.14.2/C++11 深度定制 Prompt,确保代码质量
闭环验证: 集成本地编译器,实现代码生成-编译-反馈的完整闭环
原生集成: 使用 Qt Signals & Slots 实现消息机制,UI 流畅不阻塞
该架构既保留了 MetaGPT 的核心优势(SOP、Multi-Agent、结构化输出),又充分利用了 Qt 的原生能力,为 Qt 开发者提供了真正好用的 AI 编程助手。