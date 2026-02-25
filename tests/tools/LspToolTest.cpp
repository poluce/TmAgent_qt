#include <QDebug>
#include <QCoreApplication>
#include <QFileInfo>
#include <QList>
#include <QString>

// 注意: LspTool.h 依赖 AgentEventBus.h, LspServerManager.h 等重量级头文件，
// 无法独立编译。因此本测试文件复制其私有静态辅助函数进行独立测试。

static int g_testCount = 0;
static int g_passCount = 0;

// 打印测试信息的辅助宏
#define PRINT_DIVIDER() qDebug().noquote() << "────────────────────────────────────────"
#define PRINT_INPUT(name, value) qDebug().noquote() << "  [输入] " << name << ": " << value
#define PRINT_EXPECTED(value) qDebug().noquote() << "  [期望] " << value
#define PRINT_ACTUAL(value) qDebug().noquote() << "  [实际] " << value
#define PRINT_RESULT(pass) qDebug().noquote() << (pass ? "  ✅ 通过" : "  ❌ 失败")

#define TEST(name) \
    ++g_testCount; \
    PRINT_DIVIDER(); \
    qDebug().noquote() << QString("[测试 %1] %2").arg(g_testCount).arg(name); \
    if (auto result = [&]() -> int

#define END_TEST \
    (); result != 0) { \
        PRINT_RESULT(false); \
    } else { \
        ++g_passCount; \
        PRINT_RESULT(true); \
    }

static int Fail(const QString& expected, const QString& actual) {
    PRINT_EXPECTED(expected);
    PRINT_ACTUAL(actual);
    return 1;
}

// ─── 镜像 LspProtocol.h 中的结构体 ───────────────────────────
namespace LspMock {
    struct Position { int line; int character; };
    struct Range { Position start; Position end; };
    struct Location { QString uri; Range range; };

    // 镜像 Lsp::uriToPath 的简化版本
    static QString uriToPath(const QString& uri) {
        if (uri.startsWith("file:///")) return uri.mid(8);
        if (uri.startsWith("file://")) return uri.mid(7);
        return uri;
    }
}

// ─── 镜像 LspTool 私有方法 ──────────────────────────────────

// 镜像 LspTool::isCppFile (与 LspTool.h 第171-173行逻辑一致)
static bool isCppFile(const QString &filePath) {
    QString ext = "." + QFileInfo(filePath).suffix().toLower();
    return ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".cc" || ext == ".cxx";
}

// 镜像 LspTool::formatLocations (与 LspTool.h 第198-208行逻辑一致)
static QString formatLocations(const QList<LspMock::Location>& locs) {
    if (locs.isEmpty()) return "未找到结果";
    QString res = "找到结果:\n";
    for (const auto& l : locs) {
        res += QString("- %1:%2:%3\n")
            .arg(LspMock::uriToPath(l.uri))
            .arg(l.range.start.line + 1)
            .arg(l.range.start.character + 1);
    }
    return res;
}

// ─── 测试用例 ────────────────────────────────────────────────

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug().noquote() << "========================================";
    qDebug().noquote() << "  LspTool 单元测试";
    qDebug().noquote() << "========================================";

    // ── isCppFile 测试 ──

    TEST("isCppFile - .cpp") {
        bool val = isCppFile("test.cpp");
        PRINT_INPUT("filePath", "test.cpp");
        if (!val) return Fail("true", "false");
        return 0;
    } END_TEST

    TEST("isCppFile - .h") {
        bool val = isCppFile("test.h");
        PRINT_INPUT("filePath", "test.h");
        if (!val) return Fail("true", "false");
        return 0;
    } END_TEST

    TEST("isCppFile - .hpp") {
        bool val = isCppFile("test.hpp");
        PRINT_INPUT("filePath", "test.hpp");
        if (!val) return Fail("true", "false");
        return 0;
    } END_TEST

    TEST("isCppFile - .cc") {
        bool val = isCppFile("test.cc");
        PRINT_INPUT("filePath", "test.cc");
        if (!val) return Fail("true", "false");
        return 0;
    } END_TEST

    TEST("isCppFile - .cxx") {
        bool val = isCppFile("test.cxx");
        PRINT_INPUT("filePath", "test.cxx");
        if (!val) return Fail("true", "false");
        return 0;
    } END_TEST

    TEST("isCppFile - .py") {
        bool val = isCppFile("test.py");
        PRINT_INPUT("filePath", "test.py");
        if (val) return Fail("false", "true");
        return 0;
    } END_TEST

    TEST("isCppFile - .java") {
        bool val = isCppFile("test.java");
        PRINT_INPUT("filePath", "test.java");
        if (val) return Fail("false", "true");
        return 0;
    } END_TEST

    TEST("isCppFile - 无扩展名") {
        bool val = isCppFile("Makefile");
        PRINT_INPUT("filePath", "Makefile");
        if (val) return Fail("false", "true");
        return 0;
    } END_TEST

    // ── formatLocations 测试 ──

    TEST("formatLocations - 空列表") {
        QList<LspMock::Location> empty;
        QString out = formatLocations(empty);
        PRINT_INPUT("locations", "[]");
        if (out != "未找到结果") return Fail("未找到结果", out);
        return 0;
    } END_TEST

    TEST("formatLocations - 单个结果") {
        QList<LspMock::Location> locs;
        locs.append({
            "file:///home/test.cpp",
            {{9, 4}, {9, 10}}
        });
        QString out = formatLocations(locs);
        PRINT_INPUT("locations", "[{file:///home/test.cpp, line=9, char=4}]");
        // 0-based -> 1-based: line 9->10, char 4->5
        if (!out.contains("找到结果:"))
            return Fail("包含 '找到结果:'", out);
        if (!out.contains("home/test.cpp:10:5"))
            return Fail("包含 'home/test.cpp:10:5'", out);
        return 0;
    } END_TEST

    TEST("formatLocations - 多个结果") {
        QList<LspMock::Location> locs;
        locs.append({"file:///src/a.cpp", {{0, 0}, {0, 5}}});
        locs.append({"file:///src/b.h",   {{19, 3}, {19, 8}}});
        locs.append({"file:///src/c.hpp",  {{99, 11}, {99, 20}}});
        QString out = formatLocations(locs);
        PRINT_INPUT("locations", "[a.cpp:0:0, b.h:19:3, c.hpp:99:11]");
        if (!out.contains("src/a.cpp:1:1"))
            return Fail("包含 'src/a.cpp:1:1'", out);
        if (!out.contains("src/b.h:20:4"))
            return Fail("包含 'src/b.h:20:4'", out);
        if (!out.contains("src/c.hpp:100:12"))
            return Fail("包含 'src/c.hpp:100:12'", out);
        return 0;
    } END_TEST

    TEST("formatLocations - 缺少代码预览 (Bug确认)") {
        // Bug: formatLocations 只输出 file:line:col，没有代码片段预览
        // 理想情况下应包含对应行的代码内容，提升可用性
        QList<LspMock::Location> locs;
        locs.append({"file:///home/main.cpp", {{5, 0}, {5, 10}}});
        QString out = formatLocations(locs);
        PRINT_INPUT("locations", "[{file:///home/main.cpp, line=5}]");
        // 确认输出中没有代码预览内容 —— 只有 "- path:line:col" 格式
        QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        bool hasOnlyPathFormat = true;
        for (const auto& line : lines) {
            if (line.startsWith("- ")) {
                // 每行应只有 "- path:line:col" 格式，无额外代码内容
                QString content = line.mid(2).trimmed();
                // 格式: path:number:number
                QStringList parts = content.split(':');
                if (parts.size() < 3) {
                    hasOnlyPathFormat = false;
                }
            }
        }
        if (!hasOnlyPathFormat)
            return Fail("仅 path:line:col 格式 (无代码预览)", out);
        // Bug 确认: 缺少代码预览，应在后续版本中增强
        qDebug().noquote() << "  [备注] Bug确认: formatLocations 缺少代码预览，建议后续增强";
        return 0;
    } END_TEST

    TEST("documentSymbol 格式缺陷 (Bug文档)") {
        // Bug文档: LspTool::execute 中 documentSymbol 返回格式为 "- name (detail)"
        // 缺失信息: symbol kind (function/class/variable)、行号、层级关系
        // 这是一个文档测试，验证已知格式模式
        QString mockOutput = "- main (int main(int, char**))";
        PRINT_INPUT("mockOutput", mockOutput);
        // 验证格式匹配 "- name (detail)" 模式
        bool matchesPattern = mockOutput.startsWith("- ") && mockOutput.contains("(") && mockOutput.contains(")");
        if (!matchesPattern)
            return Fail("匹配 '- name (detail)' 模式", mockOutput);
        // 确认缺失信息
        bool missingKind = !mockOutput.contains("Function") && !mockOutput.contains("Class");
        bool missingLine = !mockOutput.contains(":") || !mockOutput.mid(2).split(' ').first().contains(':');
        if (!missingKind)
            return Fail("缺少 symbol kind 信息", "包含了 kind 信息");
        if (!missingLine)
            return Fail("缺少行号信息", "包含了行号信息");
        qDebug().noquote() << "  [备注] Bug文档: documentSymbol 缺少 symbol kind、行号、层级信息";
        return 0;
    } END_TEST

    TEST("workspaceSymbol 格式缺陷 (Bug文档)") {
        // Bug文档: LspTool::execute 中 workspaceSymbol 返回格式为 "- name (uri)"
        // 缺失信息: 行号、symbol kind
        // 这是一个文档测试，验证已知格式模式
        QString mockOutput = "- MyClass (file:///src/myclass.h)";
        PRINT_INPUT("mockOutput", mockOutput);
        // 验证格式匹配 "- name (uri)" 模式
        bool matchesPattern = mockOutput.startsWith("- ") && mockOutput.contains("(file://");
        if (!matchesPattern)
            return Fail("匹配 '- name (uri)' 模式", mockOutput);
        // 确认缺失信息
        bool missingLine = true;
        // uri 中不包含行号信息
        QString uriPart = mockOutput.mid(mockOutput.indexOf('(') + 1);
        uriPart.chop(1); // 去掉 ')'
        if (uriPart.count(':') > 3) missingLine = false; // 如果有额外的 : 可能包含行号
        // 注意: 不能用 contains("Class") 因为 "MyClass" 会误匹配
        // 检查是否有独立的 kind 标签如 "[Class]" "[Function]" "[Variable]"
        bool missingKind = !mockOutput.contains("[Function]") && !mockOutput.contains("[Class]") && !mockOutput.contains("[Variable]");
        if (!missingLine)
            return Fail("缺少行号信息", "包含了行号信息");
        if (!missingKind)
            return Fail("缺少 symbol kind 信息", "包含了 kind 信息");
        qDebug().noquote() << "  [备注] Bug文档: workspaceSymbol 缺少行号、symbol kind 信息";
        return 0;
    } END_TEST

    // ── 汇总 ──

    PRINT_DIVIDER();
    qDebug().noquote() << QString("\n测试完成: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    if (g_passCount == g_testCount) {
        qDebug().noquote() << "全部通过!";
    } else {
        qDebug().noquote() << QString("%1 个测试失败").arg(g_testCount - g_passCount);
    }

    return (g_passCount == g_testCount) ? 0 : 1;
}
