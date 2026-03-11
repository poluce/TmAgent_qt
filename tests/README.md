# TmAgent 测试套件

## 目录结构

```
tests/
├── parser/                           # 解析器测试模块
│   ├── TreeSitterParserTest.pro
│   ├── TreeSitterParserTest.cpp
│   ├── README.md
│   └── TEST_REPORT.md
├── memory/                           # 记忆系统测试模块
│   ├── MemoryReflectionTest.pro
│   └── MemoryReflectionTest.cpp
├── service/                          # 服务层测试模块
│   ├── MessageRouterTest.pro
│   └── MessageRouterTest.cpp
├── agent/                            # Agent 测试 (待添加)
├── tools/                            # 工具测试 (待添加)
└── README.md                         # 本文件
```

## 测试模块

| 模块              | 状态     | 描述                      |
| ----------------- | -------- | ------------------------- |
| [parser](parser/) | ✅ 14/14 | TreeSitterParser 封装测试 |
| [memory](memory/) | ✅ 新增 | 反思任务/质量评分（M4）无头集成测试 |
| [service](service/) | ✅ 新增 | MessageRouter 路由规则测试 |
| agent             | 🔜       | LLMAgent、ToolDispatcher  |
| tools             | 🔜       | FileTool、ShellTool       |

## 运行测试

```powershell
# Parser 模块
cd tests/parser
mkdir build; cd build; qmake ..; mingw32-make -j4
.\release\TreeSitterParserTest.exe

# Service 模块
cd tests/service
mkdir build; cd build; qmake ..; mingw32-make -j4
.\release\MessageRouterTest.exe

# Memory 模块
cd tests/memory
mkdir build; cd build; qmake ..; mingw32-make -j4
.\release\MemoryReflectionTest.exe
```

## 添加新模块

1. 创建 `tests/<module>/` 目录
2. 添加 `*.pro`、`*Test.cpp`、`README.md`
3. 更新本文件的模块表格
