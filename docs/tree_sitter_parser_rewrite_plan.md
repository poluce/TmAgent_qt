# TreeSitterParser 重写计划

## 范围
- 用 Qt 友好的方式封装 tree-sitter C API，替换已删除的 TreeSitterParser。
- 保留 UTF-8 源码副本，满足 tree-sitter 对指针生命周期的要求。
- 提供节点信息、遍历与定位等上层需要的辅助能力。

## 需先确认的决策（阻塞）
- 语言支持：先只支持 C++，同时预留 `setLanguage(TSLanguage*)` 扩展点。
- 公共类型：对外使用 QString/QByteArray，内部可保留轻量视图（如 std::string_view）。
- 位置单位：行 1-based，列统一使用 UTF-8 字节偏移。
- 错误反馈：`bool + QString lastError()`。
- 增量解析：v1 直接支持（保留旧树、接收编辑信息并增量解析）。
- 线程安全：明确类本身不线程安全，或定义线程使用约束。
- 语法错误节点：是否暴露 ERROR 节点的检查/查询接口。

## 详细步骤
1. 现状盘点与集成确认
   - 确认 `3rdparty/tree-sitter.pri` 已正确包含核心库与 C++ 语法。
   - 更新 `TmAgent.pro`，加入新的解析器头/源文件路径。
2. API 设计
   - 设计 RAII 所有权、禁拷贝语义与清晰的生命周期规则。
   - 明确核心接口：initialize/setLanguage、parse、rootNode、nodeText、节点信息与遍历。
   - 明确节点失效规则（重新解析后旧节点不可用）。
3. 核心实现
   - 正确创建与释放 `TSParser` / `TSTree`。
   - 使用保留的 UTF-8 缓冲进行解析。
   - rootNode 空树场景的安全返回。
   - 支持增量解析：保存旧树、应用编辑（`ts_tree_edit`）、用旧树进行增量解析。
4. 节点工具
   - 节点类型、起止行/列（字节偏移）的辅助函数。
   - 子节点、父节点、兄弟节点遍历接口。
   - 按位置查找节点、S-expression（如需要）。
   - ERROR 节点检测（如 `nodeHasError`）与遍历策略说明。
5. 错误处理与安全
   - 处理空 parser/tree、非法范围与空输入。
   - 确定错误传递策略（bool + 可选错误文本）。
6. 使用方接入（如有）
   - 若新增调用点，按新 API 接入并修复 include 路径与编译错误。
7. 测试与验证
   - 解析小段 C++ 代码，校验 root/nodeText。
   - 验证 UTF-8 多字节偏移（如中文标识符）。
   - 确认重新 parse 后旧节点不再使用。
   - 验证 ERROR 节点存在时的行为与错误提示。
   - 验证增量解析前后树结构与节点位置一致性。

## 涉及文件
- `src/core/parser/TreeSitterParser.h`
- `src/core/parser/TreeSitterParser.cpp`
- `TmAgent.pro`
- 可选：`docs/tree_sitter_parser_usage.md`

## 交付结果
- 新 TreeSitterParser 可编译并链接 tree-sitter 依赖。
- 行为与限制在代码注释或文档中清晰记录。
