#include <QDebug>
#include <QCoreApplication>

#include "core/tools/ShellTool.h"

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

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "        ShellTool 测试套件";
    qDebug().noquote() << "════════════════════════════════════════";

    // ========================================
    // 测试 1: isSafeCommand - 白名单通过
    // ========================================
    TEST("isSafeCommand - 白名单通过") {
        QStringList safeCmds = {"ls", "pwd", "git status", "cat file.txt", "python script.py", "make"};
        PRINT_INPUT("commands", safeCmds.join(", "));
        PRINT_EXPECTED("全部返回 true");

        for (const QString& cmd : safeCmds) {
            if (!ShellTool::isSafeCommand(cmd)) {
                return Fail("true", QString("isSafeCommand(\"%1\") = false").arg(cmd));
            }
        }
        PRINT_ACTUAL("✓ 全部通过白名单");
        return 0;
    } END_TEST

    // ========================================
    // 测试 2: isSafeCommand - 白名单拒绝
    // ========================================
    TEST("isSafeCommand - 白名单拒绝") {
        QStringList unsafeCmds = {"node", "npm", "cargo", "go", "java", "docker"};
        PRINT_INPUT("commands", unsafeCmds.join(", "));
        PRINT_EXPECTED("全部返回 false");

        for (const QString& cmd : unsafeCmds) {
            if (ShellTool::isSafeCommand(cmd)) {
                return Fail("false", QString("isSafeCommand(\"%1\") = true").arg(cmd));
            }
        }
        PRINT_ACTUAL("✓ 全部被白名单拒绝");
        return 0;
    } END_TEST

    // ========================================
    // 测试 3: isSafeCommand - 黑名单拒绝
    // ========================================
    TEST("isSafeCommand - 黑名单拒绝") {
        QStringList blacklisted = {"rm -rf /", "format C:", "shutdown /s", "dd if=/dev/zero"};
        PRINT_INPUT("commands", blacklisted.join(", "));
        PRINT_EXPECTED("全部返回 false");

        for (const QString& cmd : blacklisted) {
            if (ShellTool::isSafeCommand(cmd)) {
                return Fail("false", QString("isSafeCommand(\"%1\") = true").arg(cmd));
            }
        }
        PRINT_ACTUAL("✓ 全部被黑名单拒绝");
        return 0;
    } END_TEST

    // ========================================
    // 测试 4: isSafeCommand - 管道绕过 (Bug确认)
    // ========================================
    // BUG: splitSubCommands 只按 && 和 || 分割，不处理管道符 |
    // 因此 "ls | rm -rf /" 被视为单条命令 "ls | rm -rf /"
    // 白名单检查 "ls" 前缀匹配通过，黑名单检查 "rm -rf" 也应命中
    // 但实际上黑名单会检测到 "rm -rf"，所以这里需要用不在黑名单的危险命令
    // 实际测试: "ls | node malicious.js" — node 不在白名单但整条命令以 ls 开头通过
    TEST("isSafeCommand - 管道绕过 (Bug确认)") {
        QString cmd = "ls | rm -rf /";
        PRINT_INPUT("command", cmd);
        // 注意: 虽然包含 "rm -rf"，黑名单会先检查整条命令，所以这条会被黑名单拦截
        // 换一个不在黑名单但不在白名单的命令来演示管道绕过
        // "rm -rf" 在黑名单中，所以会返回 false
        // 用 "ls | node app.js" 来演示真正的管道绕过 bug
        QString bugCmd = "ls | node app.js";
        PRINT_INPUT("bug_command", bugCmd);
        PRINT_EXPECTED("应该拒绝但返回 true (bug: splitSubCommands 不分割管道符 |)");

        bool result = ShellTool::isSafeCommand(bugCmd);
        if (!result) {
            return Fail("true (确认 bug 存在)", "false (bug 已修复?)");
        }
        PRINT_ACTUAL("✓ 返回 true — 确认管道绕过 bug 存在");
        return 0;
    } END_TEST

    // ========================================
    // 测试 5: isSafeCommand - 分号绕过 (Bug确认)
    // ========================================
    // BUG: splitSubCommands 只按 && 和 || 分割，不处理分号 ;
    // "ls; node app.js" 整条命令以 "ls" 开头匹配白名单通过
    TEST("isSafeCommand - 分号绕过 (Bug确认)") {
        QString cmd = "ls; node app.js";
        PRINT_INPUT("command", cmd);
        PRINT_EXPECTED("应该拒绝但返回 true (bug: splitSubCommands 不分割分号 ;)");

        bool result = ShellTool::isSafeCommand(cmd);
        if (!result) {
            return Fail("true (确认 bug 存在)", "false (bug 已修复?)");
        }
        PRINT_ACTUAL("✓ 返回 true — 确认分号绕过 bug 存在");
        return 0;
    } END_TEST

    // ========================================
    // 测试 6: isWriteCommand - 识别写入命令
    // ========================================
    TEST("isWriteCommand - 识别写入命令") {
        QStringList writeCmds = {"mkdir test", "git add .", "git commit -m test", "make"};
        PRINT_INPUT("commands", writeCmds.join(", "));
        PRINT_EXPECTED("全部返回 true");

        for (const QString& cmd : writeCmds) {
            if (!ShellTool::isWriteCommand(cmd)) {
                return Fail("true", QString("isWriteCommand(\"%1\") = false").arg(cmd));
            }
        }
        PRINT_ACTUAL("✓ 全部识别为写入命令");
        return 0;
    } END_TEST

    // ========================================
    // 测试 7: isExecutableCommand - cat file.py 误判 (Bug确认)
    // ========================================
    // BUG: isExecutableCommand 检查命令中是否包含 ".py" 等扩展名
    // "cat file.py" 包含 ".py" 因此被误判为可执行命令
    // 这是一个误报 (false positive) — cat 只是读取文件，不是执行
    TEST("isExecutableCommand - cat file.py 误判 (Bug确认)") {
        QString cmd = "cat file.py";
        PRINT_INPUT("command", cmd);
        PRINT_EXPECTED("应该返回 false 但返回 true (bug: 包含 .py 就误判为可执行)");

        bool result = ShellTool::isExecutableCommand(cmd);
        if (!result) {
            return Fail("true (确认 bug 存在)", "false (bug 已修复?)");
        }
        PRINT_ACTUAL("✓ 返回 true — 确认 cat file.py 误判 bug 存在");
        return 0;
    } END_TEST

    // ========================================
    // 测试 8: executeCommand - 安全命令执行
    // ========================================
    TEST("executeCommand - 安全命令执行") {
        QString cmd = "echo hello";
        PRINT_INPUT("command", cmd);
        PRINT_EXPECTED("输出包含 '退出码: 0' 和 'hello'");

        QString result = ShellTool::executeCommand(cmd);

        bool pass = result.contains("退出码: 0") && result.contains("hello");
        if (!pass) {
            return Fail("包含 '退出码: 0' 和 'hello'", result.left(200));
        }
        PRINT_ACTUAL("✓ 命令执行成功，输出正确");
        return 0;
    } END_TEST

    // ========================================
    // 测试 9: executeCommand - 输出格式验证
    // ========================================
    TEST("executeCommand - 输出格式验证") {
        QString cmd = "pwd";
        PRINT_INPUT("command", cmd);
        PRINT_EXPECTED("输出包含 '退出码:' 和 '标准输出:'");

        QString result = ShellTool::executeCommand(cmd);

        bool pass = result.contains("退出码:") && result.contains("标准输出:");
        if (!pass) {
            return Fail("包含 '退出码:' 和 '标准输出:'", result.left(200));
        }
        PRINT_ACTUAL("✓ 输出格式正确");
        return 0;
    } END_TEST

    // ========================================
    // 测试 10: executeCommand - 危险命令拒绝
    // ========================================
    TEST("executeCommand - 危险命令拒绝") {
        QString cmd = "node server.js";
        PRINT_INPUT("command", cmd);
        PRINT_EXPECTED("返回 '错误: 命令被安全策略拒绝'");

        QString result = ShellTool::executeCommand(cmd);

        bool pass = result.contains("错误: 命令被安全策略拒绝");
        if (!pass) {
            return Fail("错误: 命令被安全策略拒绝", result.left(200));
        }
        PRINT_ACTUAL("✓ 危险命令被正确拒绝");
        return 0;
    } END_TEST

    // ========================================
    // 测试 11: isSafeCommand - 复合命令 && 通过
    // ========================================
    TEST("isSafeCommand - 复合命令 && 通过") {
        QString cmd = "ls && pwd";
        PRINT_INPUT("command", cmd);
        PRINT_EXPECTED("返回 true");

        bool result = ShellTool::isSafeCommand(cmd);
        if (!result) {
            return Fail("true", "false");
        }
        PRINT_ACTUAL("✓ 复合安全命令通过");
        return 0;
    } END_TEST

    // ========================================
    // 测试 12: isSafeCommand - 复合命令 && 拒绝
    // ========================================
    TEST("isSafeCommand - 复合命令 && 拒绝") {
        QString cmd = "ls && node app.js";
        PRINT_INPUT("command", cmd);
        PRINT_EXPECTED("返回 false");

        bool result = ShellTool::isSafeCommand(cmd);
        if (result) {
            return Fail("false", "true");
        }
        PRINT_ACTUAL("✓ 含危险子命令的复合命令被拒绝");
        return 0;
    } END_TEST

    // ========================================
    // 输出结果
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
