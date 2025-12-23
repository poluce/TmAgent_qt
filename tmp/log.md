User:
file:///F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md 这个文档说了什么？
Assistant:我来读取这个设计文档的内容。
🔧 执行 view_file...

🔧 工具调用:
工具: view_file
参数: {"file_path":"F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md"}
→ 读取文件: F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md
→ 结果:
文件路径: F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md
文件大小: 9978 字节
总行数: 259
---内容开始---
下面是**重新生成**的一份《Qt Agent-Core 多 Agent 编排架构文档（需求导向版）》：把你关心的“从完成角度看能力（终端启用/读写、写改比查、编译等）”明确落到**模块、接口、任务类型（Task type）、能力声明（Capability）**上，便于你后续继续补需求或直接开工。

---

# Qt Agent-Core 多 Agent 编排架构文档（Linux/CI 优先）

## 1. 目标与边界

### 1.1 总目标

构建一个可在 **Linux 命令行/CI** 运行的自动化 **agent-core**：
由**主 Agent（Orchestrator）** 分

... (文件内容过长，中间部分已省略，完整内容已发送给 LLM) ...

iffAgent + TextSearchAgent，完成“查 → 改 → 比 → 编译”闭环

- M3：Scheduler（并发+锁）+ BuildAgent 专用化 + 语义查询（SemanticAgent）+ 缓存索引
- M4：服务化（可选）+ LLM Tool Schema（可选）

---

如果你愿意继续罗列需求，最有价值的下一步是：**把第一阶段要支持的 Task type 列成清单，并为每个 Task type 写出“最小参数字段 + 最小结果字段”**。这一步定下来，主 Agent 的路由、子 Agent 的边界、Tools 的接口就都能稳稳落地。

---内容结束---
❌ 文件路径: F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md 文件大小: 9978 字节 总行数: 259 ---内容开始--- 下面是**重新生成**的一份《Qt Agent-Core 多 Agent 编排架构文档（需求导向版）》：把你关心的“从完成角度看能力（终端启用/读写、写改比查、编译等）”明确落到**模块、接口、任务类型（Task type）、能力声明（Capability）**上，便于你后续继续补需求或直接开工。 --- # Qt Agent-Core 多 Agent 编排架构文档（Linux/CI 优先） ## 1. 目标与边界 ### 1.1 总目标 构建一个可在 **Linux 命令行/CI** 运行的自动化 **agent-core**： 由**主 Agent（Orchestrator）** 分配任务给一系列 **子 Agent（Workers）**，各子 Agent 通过 **Tools** 调用外部工具链与工程能力，最终完成工作并输出可审计的结果。 ### 1.2 你强调的“从完成角度”的能力目标 系统需要覆盖： - 能从终端启用（CLI） - 能使用终端（执行命令） - 能从终端读取信息（stdout/stderr 流式） - 能写入终端（stdin/会话式喂命令，至少保留接口位） - 能写代码、修改代码 - 能比较代码（diff） - 能查询代码（文本搜索 + 语义查询） - 能执行编译/构建（cmake 等） ### 1.3 第一阶段边界 - Linux 优先（bash/rg/git/cmake 可用） - 不要求强交互式 TTY（vim/top/ssh 等交互程序可暂不支持） - 语义查询需要工程编译信息（compile_commands.json），缺失时允许降级 --- ## 2. 核心概念 - **Agent**：可被调度的执行实体，有生命周期/状态/日志/结果。 - **主 Agent（Orchestrator）**：负责拆解任务、调度与汇总，不直接干具体活。 - **子 Agent（Worker）**：负责一类任务的执行（执行/查询/改写/构建等）。 - **Tool**：可调用的功能单元（Shell、FS、Diff、Search、Semantic、Git、Build…）。 Agent “决定做什么”，Tool “负责怎么做”。 - **Capability（能力标签）**：描述 Agent 能做什么，用于任务路由。 - **Task type（任务类型）**：标准化“这是什么任务”，让系统能可靠调度与复现。 - **Context/Artifacts（上下文/产物）**：跨步骤共享的信息与文件引用（patch、报告、日志文件等）。 - **Scheduler（调度器）**：并发与资源锁（workspace/builddir）控制，避免冲突。 --- ## 3. 总体架构分层 ### 3.1 分层 1. **CLI/Runtime 层** - 启动入口、参数解析、配置加载、退出码、日志格式（text/json） 2. **Orchestrator 层（主 Agent）** - Plan 解析/生成、依赖调度、Agent 选择、失败策略、汇总报告 3. **Worker Agents 层（子 Agent）** - 按任务类型执行，产生日志、产物与 Result 4. **Tools 层** - ShellTool / FileTool / DiffTool / SearchTool / SemanticTool / GitTool / BuildTool 5. **Infrastructure 层** - AgentRegistry、ToolRegistry、ToolGateway（权限/审计）、ContextStore、ArtifactStore、EventBus、Scheduler、安全策略 --- ## 4. Agent 的公共合同（所有 Agent 必须统一） > 能力不同没关系，但“接口语义必须统一”，这样主 Agent 才能稳定编排。 ### 4.1 生命周期 - start(task)：开始执行一个任务 - cancel(taskId)：取消当前任务（配合超时） - stop()：停止 Agent（释放资源） ### 4.2 状态 最小状态集合建议： - Idle / Running / Succeeded / Failed / Canceled / TimedOut ### 4.3 可观测事件 - log(channel, message)：channel=stdout/stderr/agent - progress(stage, value)：可选（阶段式也行） - metric(key, value)：可选（耗时、输出量、重试次数） - finished(result)：任务完成的最终事件 ### 4.4 结果 Result（结构化） 至少包含： - taskId、status、exitCode（若有）、summary（人读） - artifacts（产物引用：文件路径/报告路径/patch 路径） - metrics（耗时、输出字节、重试次数） - degraded（是否降级，如语义查询降级为文本搜索） --- ## 5. Task type 是什么、为什么必须定 ### 5.1 定义 **Task type = 任务类别标识**，类似 API 名称。 主 Agent 下发任务时不说“随意一句话”，而是下发： - type=exec_shell / search_text / semantic_query / apply_patch / diff_workspace / build … 这样系统才能： - 选对能做这类任务的 Agent - 校验参数（安全/路径/超时） - 统一结果形态（可测试、可复现、CI 友好） ### 5.2 第一批 Task type（建议最小闭环） 围绕你关心的“查 → 改 → 比 → 编译”，第一阶段建议至少有： - **exec_shell**：执行命令（git/cmake/rg 也可先走它） - **search_text**：文本搜索（rg/grep） - **semantic_query**：语义查询（clangd/LSP，缺失 compile db 可降级） - **read_file** / **write_file**：文件读写（写代码基础） - **apply_patch**：应用补丁（安全修改、可回滚） - **diff_workspace**：对比工作区（git diff 或 diff -u） - **build**：构建（封装 cmake configure/build/test，可内部调用 exec_shell） > 你要的“写/改/比/查”就分别落到：read/write/apply_patch/diff/search/semantic 上。 --- ## 6. Capability（能力标签）与路由策略 ### 6.1 能力标签示例 - Shell：ShellExec、ShellSession、ProcessControl、EnvInject、StreamIO - FS：FileRead、FileWriteAtomic、PatchApply、Snapshot - Diff：DiffWorkspace、ChangeSummary - Search：TextSearch、SemanticQuery - Build：Configure、Build、Test、ParseDiagnostics - VCS：RepoStatus、Checkout、ApplyPatch ### 6.2 主 Agent 如何选子 Agent 两种都支持，推荐默认“自动路由 + 可强制指定”： - **能力路由（capability routing）**：按 task.requiredCaps 或 task.type 找候选 agent，按负载/资源/健康度选一个 - **显式指定 agentId**：plan 中写死某一步由哪个 agent 执行（调试/强约束） --- ## 7. 工具系统（Tools）设计 ### 7.1 Tool 的职责边界 Tool 是“动作执行器”，必须明确： - 输入参数 schema（结构化） - 输出结果 schema（stdout/stderr/exitCode/artifacts） - 统一的错误模型（可重试/不可重试） ### 7.2 ToolRegistry 与 ToolGateway - **ToolRegistry**：注册与发现（toolId → tool 实例） - **ToolGateway**：统一调用入口（权限校验、审计、日志归并、超时/取消注入） > “每个 agent 都能调用工具”应改写为： > **每个 agent 都通过 ToolGateway 调用工具，但有自己的 tool allowlist（权限）**。 ### 7.3 第一阶段核心工具集合 - ShellTool（bash OneShot；可选常驻 Session） - FileTool（read/write atomic/snapshot） - DiffTool（git diff/diff -u） - TextSearchTool（rg） - GitTool / BuildTool（可先由 ShellTool 包装，后续再专用化） - SemanticTool（clangd/LSP，第二阶段） --- ## 8. 多 Agent 协作的基础设施 ### 8.1 ContextStore（共享上下文） - 保存 workspace 路径、builddir、关键中间结果（如搜索命中列表）、生成的 patch 路径等 - 原则：传“引用”（文件路径/artifact id），不要在消息里塞超大文本 ### 8.2 ArtifactStore（产物登记） - patch、报告、日志文件、编译输出摘要等统一登记，便于下游步骤消费 ### 8.3 EventBus（事件总线） - 聚合所有 agent 的 log/progress/finished - Orchestrator 订阅并汇总输出到终端/JSON 报告 ### 8.4 Scheduler（调度器） - 并发限制（全局/按类型） - 资源锁：workspace lock、builddir lock（防止并发写/并发 build 冲突） --- ## 9. 安全与审计（必须有） - workspace 沙箱：禁止越界路径（../、符号链接逃逸等） - 危险命令策略：至少 denylist（rm -rf、sudo、curl|sh…） - 修改必须可审计：写文件前 snapshot 或生成 patch；Result 必须包含 diff/patch 引用 - 大输出治理：截断 + 落盘（artifact），终端只显示摘要/尾部 --- ## 10. 运行形态与 LLM 调用方式 ### 10.1 两种运行形态 - **CLI 模式（CI 首选）**：一次运行一个 plan，输出日志+JSON+退出码 - **Service 模式（高频编排更优）**：常驻服务（可缓存 clangd 索引/常驻 shell），通过本地 API 调用 ### 10.2 LLM 怎么“调用你的 agent” LLM 不直接运行命令；LLM 产生结构化 tool call（task），Orchestrator 执行并回传 observation。 你只要保证：**Task/Result schema 稳定**，以后接不接 LLM 都不推倒重来。 --- ## 11. 推荐的最小子 Agent 划分（能闭环、易扩展） 第一阶段（强烈建议先这样）： - **ExecAgent**：专注 exec_shell（跑 git/cmake/rg 都可先走它） - **EditAgent**：read/write/apply_patch（写改代码，输出 patch） - **DiffAgent**：diff_workspace（统一输出 diff + 摘要） - **TextSearchAgent**：search_text（更快更专用） 第二阶段： - **BuildAgent**：build（更强的构建封装、诊断解析） - **SemanticAgent**：semantic_query（clangd/LSP + compile db） --- ## 12. 里程碑（落地路径） - M1：CLI + Orchestrator（顺序执行）+ ExecAgent + ShellTool + 报告/退出码 - M2：加入 EditAgent + DiffAgent + TextSearchAgent，完成“查 → 改 → 比 → 编译”闭环 - M3：Scheduler（并发+锁）+ BuildAgent 专用化 + 语义查询（SemanticAgent）+ 缓存索引 - M4：服务化（可选）+ LLM Tool Schema（可选） --- 如果你愿意继续罗列需求，最有价值的下一步是：**把第一阶段要支持的 Task type 列成清单，并为每个 Task type 写出“最小参数字段 + 最小结果字段”**。这一步定下来，主 Agent 的路由、子 Agent 的边界、Tools 的接口就都能稳稳落地。 ---内容结束--- 文档内容较长，让我继续查看剩余部分：
🔧 执行 read_file_lines...

🔧 工具调用:
工具: read_file_lines
参数: {"end_line":200,"file_path":"F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md","start_line":100}
→ 读取文件: F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md (第 100 ~ 200 行)
→ 结果:
文件: F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md
总行数: 258
显示范围: 第 100 ~ 200 行

---

100: 至少包含：
101:
102: - taskId、status、exitCode（若有）、summary（人读）
103: - artifacts（产物引用：文件路径/报告路径/patch 路径）
104: - metrics（耗时、输出字节、重试次数）
105: - degraded（是否降级，如语义查询降级为文本搜索）
106:
107: ---
108:
109: ## 5. Task type 是什么、为什么必须定
110:
111: ### 5.1 定义
112:
113: **Task type = 任务类别标识**，类似 API 名称。
114: 主 Agent 下发任务时不说“随意一句话”，而是下发：
115:
116: - type=exec_shell / search_text / semantic_query / apply_patch / diff_w
... (内容过长，已截断显示) ...
SemanticTool（clangd/LSP，第二阶段）
184:
185: ---
186:
187: ## 8. 多 Agent 协作的基础设施
188:
189: ### 8.1 ContextStore（共享上下文）
190:
191: - 保存 workspace 路径、builddir、关键中间结果（如搜索命中列表）、生成的 patch 路径等
192: - 原则：传“引用”（文件路径/artifact id），不要在消息里塞超大文本
193:
194: ### 8.2 ArtifactStore（产物登记）
195:
196: - patch、报告、日志文件、编译输出摘要等统一登记，便于下游步骤消费
197:
198: ### 8.3 EventBus（事件总线）
199:
200: - 聚合所有 agent 的 log/progress/finished
❌ 文件: F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md 总行数: 258 显示范围: 第 100 ~ 200 行 --- 100: 至少包含： 101: 102: - taskId、status、exitCode（若有）、summary（人读） 103: - artifacts（产物引用：文件路径/报告路径/patch 路径） 104: - metrics（耗时、输出字节、重试次数） 105: - degraded（是否降级，如语义查询降级为文本搜索） 106: 107: --- 108: 109: ## 5. Task type 是什么、为什么必须定 110: 111: ### 5.1 定义 112: 113: **Task type = 任务类别标识**，类似 API 名称。 114: 主 Agent 下发任务时不说“随意一句话”，而是下发： 115: 116: - type=exec_shell / search_text / semantic_query / apply_patch / diff_workspace / build … 117: 这样系统才能： 118: - 选对能做这类任务的 Agent 119: - 校验参数（安全/路径/超时） 120: - 统一结果形态（可测试、可复现、CI 友好） 121: 122: ### 5.2 第一批 Task type（建议最小闭环） 123: 124: 围绕你关心的“查 → 改 → 比 → 编译”，第一阶段建议至少有： 125: 126: - **exec_shell**：执行命令（git/cmake/rg 也可先走它） 127: - **search_text**：文本搜索（rg/grep） 128: - **semantic_query**：语义查询（clangd/LSP，缺失 compile db 可降级） 129: - **read_file** / **write_file**：文件读写（写代码基础） 130: - **apply_patch**：应用补丁（安全修改、可回滚） 131: - **diff_workspace**：对比工作区（git diff 或 diff -u） 132: - **build**：构建（封装 cmake configure/build/test，可内部调用 exec_shell） 133: 134: > 你要的“写/改/比/查”就分别落到：read/write/apply_patch/diff/search/semantic 上。 135: 136: --- 137: 138: ## 6. Capability（能力标签）与路由策略 139: 140: ### 6.1 能力标签示例 141: 142: - Shell：ShellExec、ShellSession、ProcessControl、EnvInject、StreamIO 143: - FS：FileRead、FileWriteAtomic、PatchApply、Snapshot 144: - Diff：DiffWorkspace、ChangeSummary 145: - Search：TextSearch、SemanticQuery 146: - Build：Configure、Build、Test、ParseDiagnostics 147: - VCS：RepoStatus、Checkout、ApplyPatch 148: 149: ### 6.2 主 Agent 如何选子 Agent 150: 151: 两种都支持，推荐默认“自动路由 + 可强制指定”： 152: 153: - **能力路由（capability routing）**：按 task.requiredCaps 或 task.type 找候选 agent，按负载/资源/健康度选一个 154: - **显式指定 agentId**：plan 中写死某一步由哪个 agent 执行（调试/强约束） 155: 156: --- 157: 158: ## 7. 工具系统（Tools）设计 159: 160: ### 7.1 Tool 的职责边界 161: 162: Tool 是“动作执行器”，必须明确： 163: 164: - 输入参数 schema（结构化） 165: - 输出结果 schema（stdout/stderr/exitCode/artifacts） 166: - 统一的错误模型（可重试/不可重试） 167: 168: ### 7.2 ToolRegistry 与 ToolGateway 169: 170: - **ToolRegistry**：注册与发现（toolId → tool 实例） 171: - **ToolGateway**：统一调用入口（权限校验、审计、日志归并、超时/取消注入） 172: 173: > “每个 agent 都能调用工具”应改写为： 174: > **每个 agent 都通过 ToolGateway 调用工具，但有自己的 tool allowlist（权限）**。 175: 176: ### 7.3 第一阶段核心工具集合 177: 178: - ShellTool（bash OneShot；可选常驻 Session） 179: - FileTool（read/write atomic/snapshot） 180: - DiffTool（git diff/diff -u） 181: - TextSearchTool（rg） 182: - GitTool / BuildTool（可先由 ShellTool 包装，后续再专用化） 183: - SemanticTool（clangd/LSP，第二阶段） 184: 185: --- 186: 187: ## 8. 多 Agent 协作的基础设施 188: 189: ### 8.1 ContextStore（共享上下文） 190: 191: - 保存 workspace 路径、builddir、关键中间结果（如搜索命中列表）、生成的 patch 路径等 192: - 原则：传“引用”（文件路径/artifact id），不要在消息里塞超大文本 193: 194: ### 8.2 ArtifactStore（产物登记） 195: 196: - patch、报告、日志文件、编译输出摘要等统一登记，便于下游步骤消费 197: 198: ### 8.3 EventBus（事件总线） 199: 200: - 聚合所有 agent 的 log/progress/finished 让我查看文档的最后部分：
🔧 执行 read_file_lines...

🔧 工具调用:
工具: read_file_lines
参数: {"end_line":259,"file_path":"F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md","start_line":200}
→ 读取文件: F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md (第 200 ~ 259 行)
→ 结果:
文件: F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md
总行数: 258
显示范围: 第 200 ~ 258 行

---

200: - 聚合所有 agent 的 log/progress/finished
201: - Orchestrator 订阅并汇总输出到终端/JSON 报告
202:
203: ### 8.4 Scheduler（调度器）
204:
205: - 并发限制（全局/按类型）
206: - 资源锁：workspace lock、builddir lock（防止并发写/并发 build 冲突）
207:
208: ---
209:
210: ## 9. 安全与审计（必须有）
211:
212: - workspace 沙箱：禁止越界路径（../、符号链接逃逸等）
213: - 危险命令策略：至少 denylist（rm -rf、sudo、curl|sh…）
214: - 修改必须可审计：写文件前 snapshot 或生成 patch；Result 必须包含 diff/patch 引用
215
... (内容过长，已截断显示) ...
CLI + Orchestrator（顺序执行）+ ExecAgent + ShellTool + 报告/退出码
252: - M2：加入 EditAgent + DiffAgent + TextSearchAgent，完成“查 → 改 → 比 → 编译”闭环
253: - M3：Scheduler（并发+锁）+ BuildAgent 专用化 + 语义查询（SemanticAgent）+ 缓存索引
254: - M4：服务化（可选）+ LLM Tool Schema（可选）
255:
256: ---
257:
258: 如果你愿意继续罗列需求，最有价值的下一步是：**把第一阶段要支持的 Task type 列成清单，并为每个 Task type 写出“最小参数字段 + 最小结果字段”**。这一步定下来，主 Agent 的路由、子 Agent 的边界、Tools 的接口就都能稳稳落地。
✅ 文件: F:/B_My_Document/GitHub/meta_gpt_qt/metagpt_qt/项目描述/设计.md 总行数: 258 显示范围: 第 200 ~ 258 行 --- 200: - 聚合所有 agent 的 log/progress/finished 201: - Orchestrator 订阅并汇总输出到终端/JSON 报告 202: 203: ### 8.4 Scheduler（调度器） 204: 205: - 并发限制（全局/按类型） 206: - 资源锁：workspace lock、builddir lock（防止并发写/并发 build 冲突） 207: 208: --- 209: 210: ## 9. 安全与审计（必须有） 211: 212: - workspace 沙箱：禁止越界路径（../、符号链接逃逸等） 213: - 危险命令策略：至少 denylist（rm -rf、sudo、curl|sh…） 214: - 修改必须可审计：写文件前 snapshot 或生成 patch；Result 必须包含 diff/patch 引用 215: - 大输出治理：截断 + 落盘（artifact），终端只显示摘要/尾部 216: 217: --- 218: 219: ## 10. 运行形态与 LLM 调用方式 220: 221: ### 10.1 两种运行形态 222: 223: - **CLI 模式（CI 首选）**：一次运行一个 plan，输出日志+JSON+退出码 224: - **Service 模式（高频编排更优）**：常驻服务（可缓存 clangd 索引/常驻 shell），通过本地 API 调用 225: 226: ### 10.2 LLM 怎么“调用你的 agent” 227: 228: LLM 不直接运行命令；LLM 产生结构化 tool call（task），Orchestrator 执行并回传 observation。 229: 你只要保证：**Task/Result schema 稳定**，以后接不接 LLM 都不推倒重来。 230: 231: --- 232: 233: ## 11. 推荐的最小子 Agent 划分（能闭环、易扩展） 234: 235: 第一阶段（强烈建议先这样）： 236: 237: - **ExecAgent**：专注 exec_shell（跑 git/cmake/rg 都可先走它） 238: - **EditAgent**：read/write/apply_patch（写改代码，输出 patch） 239: - **DiffAgent**：diff_workspace（统一输出 diff + 摘要） 240: - **TextSearchAgent**：search_text（更快更专用） 241: 242: 第二阶段： 243: 244: - **BuildAgent**：build（更强的构建封装、诊断解析） 245: - **SemanticAgent**：semantic_query（clangd/LSP + compile db） 246: 247: --- 248: 249: ## 12. 里程碑（落地路径） 250: 251: - M1：CLI + Orchestrator（顺序执行）+ ExecAgent + ShellTool + 报告/退出码 252: - M2：加入 EditAgent + DiffAgent + TextSearchAgent，完成“查 → 改 → 比 → 编译”闭环 253: - M3：Scheduler（并发+锁）+ BuildAgent 专用化 + 语义查询（SemanticAgent）+ 缓存索引 254: - M4：服务化（可选）+ LLM Tool Schema（可选） 255: 256: --- 257: 258: 如果你愿意继续罗列需求，最有价值的下一步是：**把第一阶段要支持的 Task type 列成清单，并为每个 Task type 写出“最小参数字段 + 最小结果字段”**。这一步定我来读取这个设计文档的内容。文档内容较长，让我继续查看剩余部分：让我查看文档的最后部分：## 文档总结
这个设计文档详细描述了一个 Qt Agent-Core 多 Agent 编排架构，主要面向 Linux/CI 环境的自动化工程任务执行。以下是核心内容：

1. 项目目标
   构建一个可在 Linux 命令行/CI 运行的自动化 agent-core
   主 Agent（Orchestrator）分配任务给子 Agent（Workers）
   子 Agent 通过 Tools 调用外部工具链完成工程任务
2. 核心能力要求
   终端启用（CLI）
   终端命令执行与信息读取
   代码的写、改、比、查
   编译/构建执行（cmake 等）
3. 核心概念
   Agent：可调度的执行实体
   主 Agent：任务拆解、调度与汇总
   子 Agent：具体任务执行（执行/查询/改写/构建）
   Tool：功能单元（Shell、FS、Diff、Search、Git、Build）
   Capability：描述 Agent 能做什么
   Task type：标准化任务类型
4. 架构分层
   CLI/Runtime 层
   Orchestrator 层
   Worker Agents 层
   Tools 层
   Infrastructure 层
5. 关键设计
   Task type 标准化：exec_shell、search_text、semantic_query、read_file、write_file、apply_patch、diff_workspace、build
   能力路由机制：按任务类型自动选择合适 Agent
   安全审计：沙箱、危险命令限制、修改可审计
6. Agent 划分建议
   第一阶段：ExecAgent、EditAgent、DiffAgent、TextSearchAgent
   第二阶段：BuildAgent、SemanticAgent
7. 里程碑规划
   M1：CLI + Orchestrator + ExecAgent
   M2：加入 EditAgent + DiffAgent + TextSearchAgent
   M3：并发调度 + BuildAgent + SemanticAgent
   M4：服务化 + LLM 集成
   这是一个工程导向的设计文档，强调从"完成角度"定义能力，注重接口统一、任务类型标准化、安全审计等工程实践要点。文档为后续开发提供了清晰的架构蓝图和实现路径。
