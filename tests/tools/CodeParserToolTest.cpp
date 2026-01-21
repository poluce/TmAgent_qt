#include <QDebug>
#include <QTextCodec>
#include <QCoreApplication>
#include <QDir>

#include "core/tools/CodeParserTool.h"

static int g_testCount = 0;
static int g_passCount = 0;

// 测试目录路径
static QString g_fixturesDir;

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

void setupDirs() {
    g_fixturesDir = QDir::currentPath() + "/../fixtures";
    if (!QDir(g_fixturesDir).exists()) {
        g_fixturesDir = QDir::currentPath() + "/../../fixtures";
    }
    if (!QDir(g_fixturesDir).exists()) {
        g_fixturesDir = "E:/Document/TmAgent_qt/tests/fixtures";
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "      CodeParserTool 测试套件";
    qDebug().noquote() << "════════════════════════════════════════";

    setupDirs();
    qDebug().noquote() << "测试数据目录: " << g_fixturesDir;

    QString sampleCodePath = g_fixturesDir + "/sample_code.cpp";

    // ========================================
    // 测试 1: view_file_outline 基本功能
    // ========================================
    TEST("view_file_outline - 解析 C++ 文件") {
        PRINT_INPUT("file_path", sampleCodePath);
        
        QString expected = "找到: namespace TestNamespace, class Calculator, main, helperFunction";
        PRINT_EXPECTED(expected);
        
        QString result = CodeParserTool::viewFileOutline(sampleCodePath);
        
        if (result.startsWith("错误:")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        
        bool hasNamespace = result.contains("TestNamespace");
        bool hasClass = result.contains("Calculator");
        bool hasMain = result.contains("main");
        bool hasHelper = result.contains("helperFunction");
        
        qDebug().noquote() << "";
        qDebug().noquote() << "  --- 实际输出 ---";
        for (const QString& line : result.split('\n')) {
            if (line.contains("[") || line.contains("文件:") || line.contains("总行数:") || line.contains("共")) {
                qDebug().noquote() << "  " << line;
            }
        }
        qDebug().noquote() << "  ---------------";
        
        if (!hasClass || !hasMain || !hasHelper) {
            return 1;
        }
        PRINT_ACTUAL("✓ 正确识别类、函数、命名空间");
        return 0;
    } END_TEST

    // ========================================
    // 测试 2: view_file_outline 文件不存在
    // ========================================
    TEST("view_file_outline - 文件不存在") {
        QString inputFile = "/nonexistent/file.cpp";
        PRINT_INPUT("file_path", inputFile);
        
        QString expected = "返回错误，包含 '文件不存在'";
        PRINT_EXPECTED(expected);
        
        QString result = CodeParserTool::viewFileOutline(inputFile);
        
        if (!result.startsWith("错误:") || !result.contains("文件不存在")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 正确返回错误: " + result.left(40));
        return 0;
    } END_TEST

    // ========================================
    // 测试 3: view_code_item 查找函数
    // ========================================
    TEST("view_code_item - 查找 main 函数") {
        PRINT_INPUT("file_path", sampleCodePath);
        PRINT_INPUT("item_name", "main");
        
        QString expected = "返回 main 函数代码，包含 'int main()' 和 'return 0'";
        PRINT_EXPECTED(expected);
        
        QString result = CodeParserTool::viewCodeItem(sampleCodePath, "main");
        
        if (result.startsWith("错误:")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        
        bool hasMainSignature = result.contains("int main()") || result.contains("main()");
        bool hasReturn = result.contains("return 0");
        
        qDebug().noquote() << "";
        qDebug().noquote() << "  --- 实际输出 (前 10 行) ---";
        QStringList lines = result.split('\n');
        for (int i = 0; i < qMin(10, lines.size()); ++i) {
            qDebug().noquote() << "  " << lines[i];
        }
        qDebug().noquote() << "  ---------------------------";
        
        if (!hasMainSignature || !hasReturn) {
            return 1;
        }
        PRINT_ACTUAL("✓ 正确返回 main 函数代码");
        return 0;
    } END_TEST

    // ========================================
    // 测试 4: view_code_item 查找类方法
    // ========================================
    TEST("view_code_item - 查找 Calculator::add 方法") {
        PRINT_INPUT("file_path", sampleCodePath);
        PRINT_INPUT("item_name", "add");
        
        QString expected = "返回 add 方法代码，包含 'return a + b'";
        PRINT_EXPECTED(expected);
        
        QString result = CodeParserTool::viewCodeItem(sampleCodePath, "add");
        
        if (result.startsWith("错误:")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        
        bool hasAdd = result.contains("add");
        bool hasReturn = result.contains("return a + b");
        
        qDebug().noquote() << "";
        qDebug().noquote() << "  --- 实际输出 ---";
        for (const QString& line : result.split('\n')) {
            qDebug().noquote() << "  " << line;
        }
        qDebug().noquote() << "  ---------------";
        
        if (!hasAdd || !hasReturn) {
            return 1;
        }
        PRINT_ACTUAL("✓ 正确返回 add 方法代码");
        return 0;
    } END_TEST

    // ========================================
    // 测试 5: view_code_item 未找到
    // ========================================
    TEST("view_code_item - 未找到的代码项") {
        PRINT_INPUT("file_path", sampleCodePath);
        PRINT_INPUT("item_name", "nonExistentFunction");
        
        QString expected = "返回错误，包含 '未找到代码项'，并列出可用项";
        PRINT_EXPECTED(expected);
        
        QString result = CodeParserTool::viewCodeItem(sampleCodePath, "nonExistentFunction");
        
        if (!result.startsWith("错误:") || !result.contains("未找到代码项")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        
        bool hasAvailableList = result.contains("可用的代码项");
        
        qDebug().noquote() << "";
        qDebug().noquote() << "  --- 实际输出 ---";
        for (const QString& line : result.split('\n').mid(0, 8)) {
            qDebug().noquote() << "  " << line;
        }
        qDebug().noquote() << "  ---------------";
        
        if (!hasAvailableList) {
            return 1;
        }
        PRINT_ACTUAL("✓ 正确返回错误并列出可用代码项");
        return 0;
    } END_TEST

    // ========================================
    // 测试 6: JSON 接口
    // ========================================
    TEST("executeViewFileOutline - JSON 接口") {
        QJsonObject input;
        input["file_path"] = sampleCodePath;
        
        PRINT_INPUT("JSON", QString("{\"file_path\": \"%1\"}").arg(sampleCodePath));
        
        QString expected = "通过 JSON 接口正确解析文件";
        PRINT_EXPECTED(expected);
        
        QString result = CodeParserTool::executeViewFileOutline(input);
        
        if (result.startsWith("错误:")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        
        bool hasClass = result.contains("[类]") || result.contains("Calculator");
        bool hasFunction = result.contains("[函数]") || result.contains("main");
        
        if (!hasClass && !hasFunction) {
            PRINT_ACTUAL("未找到类或函数标记");
            return 1;
        }
        PRINT_ACTUAL("✓ JSON 接口正常工作");
        return 0;
    } END_TEST

    // ========================================
    // 测试总结
    // ========================================
    qDebug().noquote() << "";
    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << QString("        测试完成: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";

    if (g_passCount == g_testCount) {
        qDebug().noquote() << "🎉 所有测试通过!";
        return 0;
    } else {
        qCritical().noquote() << "❌ 有测试失败!";
        return 1;
    }
}
