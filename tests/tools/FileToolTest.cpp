#include <QDebug>
#include <QTextCodec>
#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include "core/tools/FileTool.h"

static int g_testCount = 0;
static int g_passCount = 0;

// 测试目录路径
static QString g_fixturesDir;  // 公共测试数据目录 (只读)
static QString g_tempDir;      // 临时写入目录

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

void setupDirs() {
    // 公共测试数据目录 (相对于测试执行位置)
    g_fixturesDir = QDir::currentPath() + "/../fixtures";
    if (!QDir(g_fixturesDir).exists()) {
        // 尝试其他可能路径
        g_fixturesDir = QDir::currentPath() + "/../../fixtures";
    }
    if (!QDir(g_fixturesDir).exists()) {
        g_fixturesDir = "E:/Document/TmAgent_qt/tests/fixtures";
    }
    
    // 临时目录 (用于写入测试)
    g_tempDir = QDir::currentPath() + "/temp";
    QDir dir(g_tempDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
}

void cleanupTempDir() {
    QDir dir(g_tempDir);
    if (dir.exists()) {
        dir.removeRecursively();
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "        FileTool 测试套件";
    qDebug().noquote() << "════════════════════════════════════════";

    setupDirs();
    qDebug().noquote() << "测试数据目录: " << g_fixturesDir;
    qDebug().noquote() << "临时写入目录: " << g_tempDir;

    // ========================================
    // 测试 1: readFile 读取文件
    // ========================================
    TEST("readFile - 读取测试文件") {
        QString inputFile = g_fixturesDir + "/sample_text.txt";
        PRINT_INPUT("file_path", inputFile);
        
        QString expected = "包含 'Line 1 - Hello World' 和 '中文测试内容'";
        PRINT_EXPECTED(expected);
        
        QString result = FileTool::readFile(inputFile);
        
        bool pass = result.contains("Line 1 - Hello World") && 
                    result.contains("中文测试内容");
        
        if (!pass) {
            PRINT_ACTUAL(result.left(200) + "...");
            return 1;
        }
        PRINT_ACTUAL("✓ 包含预期内容");
        return 0;
    } END_TEST

    // ========================================
    // 测试 2: readFileLines 读取行范围
    // ========================================
    TEST("readFileLines - 读取第 2-4 行") {
        QString inputFile = g_fixturesDir + "/sample_text.txt";
        PRINT_INPUT("file_path", inputFile);
        PRINT_INPUT("start_line", "2");
        PRINT_INPUT("end_line", "4");
        
        QString expected = "包含 'Line 2'、'Line 3'、'Line 4'，不包含 'Line 1'、'Line 5'";
        PRINT_EXPECTED(expected);
        
        QString result = FileTool::readFileLines(inputFile, 2, 4);
        
        bool pass = result.contains("Line 2") && 
                    result.contains("Line 3") && 
                    result.contains("Line 4") &&
                    !result.contains("Line 1 -") &&  // 避免匹配行号
                    !result.contains("Line 5");
        
        if (!pass) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 正确返回第 2-4 行");
        return 0;
    } END_TEST

    // ========================================
    // 测试 3: createFile 创建文件
    // ========================================
    TEST("createFile - 创建新文件") {
        QString directory = g_tempDir;
        QString filename = "new_file.txt";
        QString content = "Hello, Test!";
        
        PRINT_INPUT("directory", directory);
        PRINT_INPUT("filename", filename);
        PRINT_INPUT("content", content);
        
        QString expected = "成功创建文件，内容为 'Hello, Test!'";
        PRINT_EXPECTED(expected);
        
        QString result = FileTool::createFile(directory, filename, content);
        
        if (!result.startsWith("成功:")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        
        // 验证文件内容
        QString actualContent = FileTool::readFileContent(g_tempDir + "/" + filename);
        if (actualContent != content) {
            PRINT_ACTUAL(QString("文件内容: '%1'").arg(actualContent));
            return 1;
        }
        PRINT_ACTUAL("✓ 文件已创建，内容正确");
        return 0;
    } END_TEST

    // ========================================
    // 测试 4: createFile 中文内容
    // ========================================
    TEST("createFile - 中文内容 (UTF-8)") {
        QString directory = g_tempDir;
        QString filename = "chinese.txt";
        QString content = "你好，世界！";
        
        PRINT_INPUT("directory", directory);
        PRINT_INPUT("filename", filename);
        PRINT_INPUT("content", content);
        
        QString expected = "成功创建，读回内容为 '你好，世界！'";
        PRINT_EXPECTED(expected);
        
        FileTool::createFile(directory, filename, content);
        QString actualContent = FileTool::readFileContent(g_tempDir + "/" + filename);
        
        if (actualContent != content) {
            PRINT_ACTUAL(QString("读回内容: '%1'").arg(actualContent));
            return 1;
        }
        PRINT_ACTUAL("✓ 中文内容正确保存和读取");
        return 0;
    } END_TEST

    // ========================================
    // 测试 5: replaceInFile 替换内容
    // ========================================
    TEST("replaceInFile - 替换文本") {
        // 准备测试文件
        FileTool::createFile(g_tempDir, "replace_test.txt", "Hello World");
        QString filePath = g_tempDir + "/replace_test.txt";
        
        PRINT_INPUT("file_path", filePath);
        PRINT_INPUT("target_content", "World");
        PRINT_INPUT("replacement_content", "Qt");
        
        QString expected = "文件内容变为 'Hello Qt'";
        PRINT_EXPECTED(expected);
        
        QString result = FileTool::replaceInFile(filePath, "World", "Qt");
        
        if (!result.startsWith("成功:")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        
        QString actualContent = FileTool::readFileContent(filePath);
        if (!actualContent.contains("Hello Qt")) {
            PRINT_ACTUAL(QString("文件内容: '%1'").arg(actualContent));
            return 1;
        }
        PRINT_ACTUAL("✓ 替换成功: 'Hello Qt'");
        return 0;
    } END_TEST

    // ========================================
    // 测试 6: replaceInFile 目标不存在
    // ========================================
    TEST("replaceInFile - 目标不存在时报错") {
        FileTool::createFile(g_tempDir, "replace_test2.txt", "Hello World");
        QString filePath = g_tempDir + "/replace_test2.txt";
        
        PRINT_INPUT("file_path", filePath);
        PRINT_INPUT("target_content", "NotExist");
        PRINT_INPUT("replacement_content", "Anything");
        
        QString expected = "返回错误，包含 '未找到'";
        PRINT_EXPECTED(expected);
        
        QString result = FileTool::replaceInFile(filePath, "NotExist", "Anything");
        
        if (!result.startsWith("错误:") || !result.contains("未找到")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 正确返回错误: " + result.left(50));
        return 0;
    } END_TEST

    // ========================================
    // 测试 7: insertContent 插入内容
    // ========================================
    TEST("insertContent - 在第 1 行后插入") {
        FileTool::createFile(g_tempDir, "insert_test.txt", "Line 1\nLine 3");
        QString filePath = g_tempDir + "/insert_test.txt";
        
        PRINT_INPUT("file_path", filePath);
        PRINT_INPUT("line_number", "1");
        PRINT_INPUT("content", "Line 2");
        
        QString expected = "文件变为 'Line 1\\nLine 2\\nLine 3'";
        PRINT_EXPECTED(expected);
        
        QString result = FileTool::insertContent(filePath, 1, "Line 2");
        
        if (!result.startsWith("成功:")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        
        QString actualContent = FileTool::readFileContent(filePath);
        QStringList lines = actualContent.split('\n');
        
        if (lines.size() < 3 || lines[1] != "Line 2") {
            PRINT_ACTUAL(QString("文件内容: %1").arg(actualContent));
            return 1;
        }
        PRINT_ACTUAL("✓ 插入成功，第 2 行为 'Line 2'");
        return 0;
    } END_TEST

    // ========================================
    // 测试 8: grepSearch 搜索内容
    // ========================================
    TEST("grepSearch - 搜索 'Hello'") {
        PRINT_INPUT("pattern", "Hello");
        PRINT_INPUT("directory", g_fixturesDir);
        PRINT_INPUT("file_pattern", "*.txt");
        
        QString expected = "找到 3 处匹配 (search_test.txt 中有 3 个 Hello)";
        PRINT_EXPECTED(expected);
        
        QString result = FileTool::grepSearch("Hello", g_fixturesDir, "*.txt");
        
        if (result.startsWith("错误:")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        
        // 计算匹配数
        int count = result.count("Hello");
        PRINT_ACTUAL(QString("找到 %1 处 'Hello'").arg(count));
        
        if (count < 3) {
            return 1;
        }
        return 0;
    } END_TEST

    // ========================================
    // 测试 9: findByName 按名称搜索
    // ========================================
    TEST("findByName - 搜索 '*.txt'") {
        PRINT_INPUT("pattern", "*.txt");
        PRINT_INPUT("directory", g_fixturesDir);
        
        QString expected = "至少找到 2 个 .txt 文件";
        PRINT_EXPECTED(expected);
        
        QString result = FileTool::findByName("*.txt", g_fixturesDir);
        
        if (result.startsWith("错误:")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        
        bool hasSampleText = result.contains("sample_text.txt");
        bool hasSearchTest = result.contains("search_test.txt");
        
        if (!hasSampleText || !hasSearchTest) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 找到 sample_text.txt 和 search_test.txt");
        return 0;
    } END_TEST

    // ========================================
    // 测试 10: listDirectory 列出目录
    // ========================================
    TEST("listDirectory - 列出 fixtures 目录") {
        PRINT_INPUT("directory_path", g_fixturesDir);
        PRINT_INPUT("recursive", "false");
        
        QString expected = "列出至少 3 个文件";
        PRINT_EXPECTED(expected);
        
        QString result = FileTool::listDirectory(g_fixturesDir, false);
        
        if (result.startsWith("错误:")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        
        bool ok = result.contains("sample_text.txt") &&
                  result.contains("sample_code.cpp");
        
        if (!ok) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 正确列出目录内容");
        return 0;
    } END_TEST

    // ========================================
    // 测试 11: deleteFile 删除文件
    // ========================================
    TEST("deleteFile - 删除文件") {
        FileTool::createFile(g_tempDir, "to_delete.txt", "Delete me");
        QString filePath = g_tempDir + "/to_delete.txt";
        
        PRINT_INPUT("file_path", filePath);
        
        QString expected = "文件被删除，不再存在";
        PRINT_EXPECTED(expected);
        
        if (!QFile::exists(filePath)) {
            PRINT_ACTUAL("文件未创建");
            return 1;
        }
        
        QString result = FileTool::deleteFile(filePath);
        
        if (!result.startsWith("成功:")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        
        if (QFile::exists(filePath)) {
            PRINT_ACTUAL("文件仍然存在");
            return 1;
        }
        PRINT_ACTUAL("✓ 文件已成功删除");
        return 0;
    } END_TEST

    // ========================================
    // 测试 12: convertMsysPath 路径转换
    // ========================================
    TEST("convertMsysPath - MSYS 路径转换") {
        QString input1 = "/e/Document/test.txt";
        QString input2 = "C:/Windows/test.txt";
        
        PRINT_INPUT("MSYS 路径", input1);
        PRINT_INPUT("Windows 路径", input2);
        
        QString expected = "/e/xxx -> E:/xxx, C:/xxx 保持不变";
        PRINT_EXPECTED(expected);
        
        QString result1 = FileTool::convertMsysPath(input1);
        QString result2 = FileTool::convertMsysPath(input2);
        
        bool pass = (result1 == "E:/Document/test.txt") && 
                    (result2 == "C:/Windows/test.txt");
        
        PRINT_ACTUAL(QString("'%1' -> '%2'").arg(input1).arg(result1));
        qDebug().noquote() << QString("         '%1' -> '%2'").arg(input2).arg(result2);
        
        return pass ? 0 : 1;
    } END_TEST

    // ========================================
    // 测试 13: 文件不存在错误
    // ========================================
    TEST("readFile - 文件不存在") {
        QString inputFile = "/nonexistent/path/file.txt";
        PRINT_INPUT("file_path", inputFile);
        
        QString expected = "返回错误，包含 '文件不存在'";
        PRINT_EXPECTED(expected);
        
        QString result = FileTool::readFile(inputFile);
        
        if (!result.startsWith("错误:") || !result.contains("文件不存在")) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 正确返回错误信息");
        return 0;
    } END_TEST

    // ========================================
    // 清理并输出结果
    // ========================================
    cleanupTempDir();

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
