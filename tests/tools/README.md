# 工具测试用例

本目录包含工具类的单元测试。

## 测试文件

| 文件 | 测试目标 |
|------|----------|
| `FileToolTest.cpp` | FileTool 文件操作工具 |
| `CodeParserToolTest.cpp` | CodeParserTool 代码解析工具 |

## 编译运行

### FileTool 测试

```bash
cd tests/tools
qmake FileToolTest.pro
make
./release/FileToolTest.exe
```

### CodeParserTool 测试

```bash
cd tests/tools
qmake CodeParserToolTest.pro
make
./release/CodeParserToolTest.exe
```

## 测试覆盖

### FileTool (13 个测试)
- `createFile` - 创建文件（含中文 UTF-8）
- `readFile` / `readFileLines` - 读取文件
- `replaceInFile` - 替换内容
- `insertContent` - 插入内容
- `listDirectory` - 目录列表
- `grepSearch` - 内容搜索
- `findByName` - 文件名搜索
- `deleteFile` - 删除文件
- `convertMsysPath` - 路径转换
- JSON 接口测试

### CodeParserTool (5 个测试)
- `view_file_outline` - 文件大纲
- `view_code_item` - 查看代码项
- 错误处理测试
