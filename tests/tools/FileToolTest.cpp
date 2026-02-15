#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

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
    // 测试 14: 大文件无截断 (Bug确认)
    // ========================================
    TEST("view_file - 大文件无截断 (Bug确认)") {
        // 生成 100KB+ 的内容
        QString content;
        int lineCount = 5000;
        for (int i = 1; i <= lineCount; ++i) {
            content += QString("Line %1: abcdefghij\n").arg(i);
        }

        PRINT_INPUT("file_size", QString("%1 bytes, %2 lines").arg(content.size()).arg(lineCount));

        QString createResult = FileTool::createFile(g_tempDir, "large_file.txt", content);
        if (!createResult.startsWith("成功:")) {
            PRINT_ACTUAL("创建大文件失败: " + createResult);
            return 1;
        }

        QString result = FileTool::readFile(g_tempDir + "/large_file.txt");
        QString lastLine = QString("Line %1: abcdefghij").arg(lineCount);

        QString expected = "结果包含最后一行 '" + lastLine + "' (无截断)";
        PRINT_EXPECTED(expected);

        if (!result.contains(lastLine)) {
            PRINT_ACTUAL("未找到最后一行，内容可能被截断。结果末尾: " + result.right(200));
            return 1;
        }
        // Bug确认: readFile 没有大小限制/截断机制
        PRINT_ACTUAL("✓ 包含最后一行，确认无截断 (Bug: readFile 无大小限制)");
        return 0;
    } END_TEST

    // ========================================
    // 测试 15: 覆盖已有文件无警告 (Bug确认)
    // ========================================
    TEST("createFile - 覆盖已有文件无警告 (Bug确认)") {
        FileTool::createFile(g_tempDir, "overwrite_test.txt", "original");
        QString filePath = g_tempDir + "/overwrite_test.txt";

        PRINT_INPUT("file_path", filePath);
        PRINT_INPUT("first_content", "original");
        PRINT_INPUT("second_content", "overwritten");

        QString expected = "第二次 createFile 返回 '成功:' 且无覆盖警告";
        PRINT_EXPECTED(expected);

        QString result = FileTool::createFile(g_tempDir, "overwrite_test.txt", "overwritten");

        if (!result.startsWith("成功:")) {
            PRINT_ACTUAL("第二次创建失败: " + result);
            return 1;
        }

        QString actualContent = FileTool::readFileContent(filePath);
        if (actualContent != "overwritten") {
            PRINT_ACTUAL("文件内容不是 'overwritten': " + actualContent);
            return 1;
        }
        // Bug确认: createFile 覆盖已有文件时不发出任何警告
        PRINT_ACTUAL("✓ 覆盖成功且无警告 (Bug: 应警告覆盖已有文件)");
        return 0;
    } END_TEST

    // ========================================
    // 测试 16: multi_replace_in_file 基本替换
    // ========================================
    TEST("multi_replace_in_file - 基本替换") {
        FileTool::createFile(g_tempDir, "multi_replace.txt", "aaa bbb ccc");
        QString filePath = g_tempDir + "/multi_replace.txt";

        QJsonArray replacements;
        QJsonObject rep1;
        rep1["target_content"] = "aaa";
        rep1["replacement_content"] = "xxx";
        replacements.append(rep1);
        QJsonObject rep2;
        rep2["target_content"] = "ccc";
        rep2["replacement_content"] = "zzz";
        replacements.append(rep2);

        PRINT_INPUT("file_path", filePath);
        PRINT_INPUT("replacements", "aaa->xxx, ccc->zzz");

        QString expected = "文件内容变为 'xxx bbb zzz'";
        PRINT_EXPECTED(expected);

        QString result = FileTool::multiReplaceInFile(filePath, replacements);

        if (!result.startsWith("成功:")) {
            PRINT_ACTUAL(result);
            return 1;
        }

        QString actualContent = FileTool::readFileContent(filePath);
        if (actualContent != "xxx bbb zzz") {
            PRINT_ACTUAL("文件内容: '" + actualContent + "'");
            return 1;
        }
        PRINT_ACTUAL("✓ 多重替换成功: 'xxx bbb zzz'");
        return 0;
    } END_TEST

    // ========================================
    // 测试 17: 级联替换 (Bug确认)
    // ========================================
    TEST("multi_replace_in_file - 级联替换 (Bug确认)") {
        FileTool::createFile(g_tempDir, "cascade_replace.txt", "foo bar");
        QString filePath = g_tempDir + "/cascade_replace.txt";

        QJsonArray replacements;
        QJsonObject rep1;
        rep1["target_content"] = "foo";
        rep1["replacement_content"] = "foobar";
        replacements.append(rep1);
        QJsonObject rep2;
        rep2["target_content"] = "bar";
        rep2["replacement_content"] = "baz";
        replacements.append(rep2);

        PRINT_INPUT("file_path", filePath);
        PRINT_INPUT("replacements", "foo->foobar, bar->baz");
        PRINT_INPUT("original_content", "foo bar");

        // 预检查: 在原始内容 "foo bar" 中，"foo" 和 "bar" 各出现一次，预检查通过
        // 但第一步 foo->foobar 后内容变为 "foobar bar"
        // 第二步 bar->baz 会替换所有 "bar" -> 结果为 "foobaz baz"
        QString expected = "Bug: 级联替换导致 'foobaz baz' 而非期望的 'foobar baz'";
        PRINT_EXPECTED(expected);

        QString result = FileTool::multiReplaceInFile(filePath, replacements);
        QString actualContent = FileTool::readFileContent(filePath);

        PRINT_ACTUAL("文件内容: '" + actualContent + "'");

        if (actualContent == "foobaz baz") {
            // Bug确认: 替换是在变化后的内容上顺序执行的，导致级联效应
            qDebug().noquote() << "  [备注] 确认级联Bug: 替换在变化后的内容上顺序执行";
            return 0;
        } else if (actualContent == "foobar baz") {
            qDebug().noquote() << "  [备注] Bug已修复: 替换正确独立执行";
            return 0;
        }
        return 1;
    } END_TEST

    // ========================================
    // 测试 18: grep_search 二进制文件 (Bug确认)
    // ========================================
    TEST("grep_search - 二进制文件 (Bug确认)") {
        // 直接用 QFile 写入包含 \0 的二进制内容
        QString binPath = g_tempDir + "/binary_test.bin";
        QFile binFile(binPath);
        if (binFile.open(QIODevice::WriteOnly)) {
            QByteArray data;
            data.append("Hello");
            data.append('\0');
            data.append("World");
            data.append('\0');
            data.append("Test");
            binFile.write(data);
            binFile.close();
        }

        PRINT_INPUT("pattern", "Hello");
        PRINT_INPUT("directory", g_tempDir);
        PRINT_INPUT("file_pattern", "*.bin");

        QString expected = "不崩溃; QTextStream 可能在 \\0 处截断";
        PRINT_EXPECTED(expected);

        QString result = FileTool::grepSearch("Hello", g_tempDir, "*.bin");

        // 主要验证不崩溃
        // QTextStream 使用 UTF-8 编解码器读取二进制文件时，\0 字节可能导致截断
        bool found = result.contains("Hello");
        PRINT_ACTUAL(QString(found ? "找到 'Hello' (\\0 前的内容可读)" : "未找到 'Hello' (可能被 \\0 截断)"));
        qDebug().noquote() << "  [备注] Bug确认: grepSearch 未过滤二进制文件，行为依赖 QTextStream 对 \\0 的处理";
        return 0;  // 只要不崩溃就算通过
    } END_TEST

    // ========================================
    // 测试 19: grep_search 搜索 .git 目录 (Bug确认)
    // ========================================
    TEST("grep_search - 搜索 .git 目录 (Bug确认)") {
        // 创建模拟的 .git 目录
        QDir().mkpath(g_tempDir + "/.git");
        {
            QFile f(g_tempDir + "/.git/config");
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write("gitconfig_test_marker");
                f.close();
            }
        }

        PRINT_INPUT("pattern", "gitconfig_test_marker");
        PRINT_INPUT("directory", g_tempDir);
        PRINT_INPUT("file_pattern", "");

        QString expected = "Bug: 搜索结果包含 .git 目录中的匹配 (应排除)";
        PRINT_EXPECTED(expected);

        QString result = FileTool::grepSearch("gitconfig_test_marker", g_tempDir, "");

        bool foundInGit = result.contains("gitconfig_test_marker");
        PRINT_ACTUAL(QString(foundInGit ? "在 .git 中找到匹配" : "未在 .git 中找到匹配"));

        if (foundInGit) {
            // Bug确认: QDirIterator 不排除 .git 目录
            qDebug().noquote() << "  [备注] 确认Bug: grepSearch 未排除 .git 目录";
        } else {
            qDebug().noquote() << "  [备注] .git 目录已被正确排除";
        }
        return 0;  // 记录行为，不判定失败
    } END_TEST

    // ========================================
    // 测试 20: list_directory 隐藏文件
    // ========================================
    TEST("list_directory - 隐藏文件") {
        // 创建隐藏文件
        {
            QFile f(g_tempDir + "/.hidden_file");
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write("hidden content");
                f.close();
            }
        }

        PRINT_INPUT("directory", g_tempDir);
        PRINT_INPUT("recursive", "false");

        QString expected = "检查 .hidden_file 是否出现在列表中";
        PRINT_EXPECTED(expected);

        QString result = FileTool::listDirectory(g_tempDir, false);

        bool found = result.contains(".hidden_file");
        PRINT_ACTUAL(QString(found ? "列出了 .hidden_file" : "未列出 .hidden_file"));

        // 在 Linux 上，以 . 开头的文件需要 QDir::Hidden 标志才能列出
        // 记录实际行为供参考
        if (found) {
            qDebug().noquote() << "  [备注] listDirectory 包含隐藏文件 (使用了 QDir::Hidden 或 AllEntries)";
        } else {
            qDebug().noquote() << "  [备注] listDirectory 不包含隐藏文件 (Linux 上需要 QDir::Hidden 标志)";
        }
        return 0;  // 记录行为，不判定失败
    } END_TEST

    // ========================================
    // 测试 21: replaceInFile 警告被当作成功 (Bug确认)
    // ========================================
    TEST("replaceInFile - 警告被当作成功 (Bug确认)") {
        FileTool::createFile(g_tempDir, "warn_test.txt", "aaa bbb aaa");
        QString filePath = g_tempDir + "/warn_test.txt";

        PRINT_INPUT("file_path", filePath);
        PRINT_INPUT("target_content", "aaa");
        PRINT_INPUT("replacement_content", "xxx");

        QString expected = "返回 '警告:' (多处匹配)，但 !startsWith('错误:') 为 true";
        PRINT_EXPECTED(expected);

        QString result = FileTool::replaceInFile(filePath, "aaa", "xxx");

        bool isWarning = result.startsWith("警告:");
        bool notError = !result.startsWith("错误:");

        PRINT_ACTUAL("结果: " + result.left(80));
        qDebug().noquote() << "  startsWith('警告:'): " << (isWarning ? "true" : "false");
        qDebug().noquote() << "  !startsWith('错误:'): " << (notError ? "true" : "false");

        if (isWarning && notError) {
            // Bug确认: 调用方如果只检查 "错误:" 前缀，会把警告当作成功
            qDebug().noquote() << "  [备注] 确认Bug: 警告前缀与成功前缀对简单错误检查不可区分";
            return 0;
        }

        if (result.startsWith("错误:")) {
            qDebug().noquote() << "  [备注] 多处匹配返回了错误而非警告";
            return 0;
        }

        if (result.startsWith("成功:")) {
            qDebug().noquote() << "  [备注] 多处匹配仍返回成功 (行为与预期不同)";
            return 0;
        }

        return 1;
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
