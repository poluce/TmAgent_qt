#include <QDebug>
#include <QCoreApplication>
#include <QJsonObject>

#include "core/tools/WebTool.h"

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

// 友元访问包装类
class WebToolTest {
public:
    static QString convert(QString html) {
        return WebTool::convertHtmlToMarkdown(html);
    }
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "        WebTool 测试套件";
    qDebug().noquote() << "════════════════════════════════════════";

    // ========================================
    // 测试 1: 标题转换 h1-h6
    // ========================================
    TEST("convertHtmlToMarkdown - 标题转换 h1-h6") {
        QString input = "<h1>Title1</h1><h2>Title2</h2><h3>Title3</h3>";
        PRINT_INPUT("html", input);

        QString expected = "包含 '# Title1', '## Title2', '### Title3'";
        PRINT_EXPECTED(expected);

        QString result = WebToolTest::convert(input);

        bool pass = result.contains("# Title1") &&
                    result.contains("## Title2") &&
                    result.contains("### Title3");

        if (!pass) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 标题正确转换");
        return 0;
    } END_TEST

    // ========================================
    // 测试 2: 链接转换
    // ========================================
    TEST("convertHtmlToMarkdown - 链接转换") {
        QString input = "<a href=\"https://example.com\">Example</a>";
        PRINT_INPUT("html", input);

        QString expected = "包含 '[Example](https://example.com)'";
        PRINT_EXPECTED(expected);

        QString result = WebToolTest::convert(input);

        bool pass = result.contains("[Example](https://example.com)");

        if (!pass) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 链接正确转换");
        return 0;
    } END_TEST

    // ========================================
    // 测试 3: 加粗和斜体
    // ========================================
    TEST("convertHtmlToMarkdown - 加粗和斜体") {
        QString input = "<b>bold</b> and <strong>strong</strong> and <em>italic</em> and <i>ital</i>";
        PRINT_INPUT("html", input);

        QString expected = "包含 '**bold**', '**strong**', '*italic*', '*ital*'";
        PRINT_EXPECTED(expected);

        QString result = WebToolTest::convert(input);

        bool pass = result.contains("**bold**") &&
                    result.contains("**strong**") &&
                    result.contains("*italic*") &&
                    result.contains("*ital*");

        if (!pass) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 加粗和斜体正确转换");
        return 0;
    } END_TEST

    // ========================================
    // 测试 4: 列表转换
    // ========================================
    TEST("convertHtmlToMarkdown - 列表转换") {
        QString input = "<ul><li>item1</li><li>item2</li></ul>";
        PRINT_INPUT("html", input);

        QString expected = "包含 '- item1' 和 '- item2'";
        PRINT_EXPECTED(expected);

        QString result = WebToolTest::convert(input);

        bool pass = result.contains("- item1") &&
                    result.contains("- item2");

        if (!pass) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 列表正确转换");
        return 0;
    } END_TEST

    // ========================================
    // 测试 5: script/style/nav/footer 移除
    // ========================================
    TEST("convertHtmlToMarkdown - script/style/nav/footer 移除") {
        QString input = "<div>keep</div><script>alert('x')</script><style>.x{}</style><nav>nav</nav><footer>foot</footer>";
        PRINT_INPUT("html", input);

        QString expected = "包含 'keep'，不包含 'alert', '.x{}', 'nav', 'foot'";
        PRINT_EXPECTED(expected);

        QString result = WebToolTest::convert(input);

        bool pass = result.contains("keep") &&
                    !result.contains("alert") &&
                    !result.contains(".x{}") &&
                    !result.contains("nav") &&
                    !result.contains("foot");

        if (!pass) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 无关标签正确移除");
        return 0;
    } END_TEST

    // ========================================
    // 测试 6: table 不支持 (Bug确认)
    // ========================================
    TEST("convertHtmlToMarkdown - table 不支持 (Bug确认)") {
        // 确认 bug: table 结构会丢失，仅保留文本内容
        QString input = "<table><tr><td>cell1</td><td>cell2</td></tr></table>";
        PRINT_INPUT("html", input);

        QString expected = "标签被剥离后仅剩 'cell1cell2' (无表格格式)";
        PRINT_EXPECTED(expected);

        QString result = WebToolTest::convert(input);

        bool pass = result.contains("cell1") &&
                    result.contains("cell2") &&
                    !result.contains("|"); // 无 markdown 表格分隔符

        if (!pass) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 确认 bug: table 结构丢失，仅保留文本");
        return 0;
    } END_TEST

    // ========================================
    // 测试 7: code/pre 不支持 (Bug确认)
    // ========================================
    TEST("convertHtmlToMarkdown - code/pre 不支持 (Bug确认)") {
        // 确认 bug: code/pre 标签被剥离，无反引号格式
        QString input = "<pre><code>int x = 1;</code></pre>";
        PRINT_INPUT("html", input);

        QString expected = "仅剩 'int x = 1;'，无 '```' 或 '`'";
        PRINT_EXPECTED(expected);

        QString result = WebToolTest::convert(input);

        bool pass = result.contains("int x = 1;") &&
                    !result.contains("```") &&
                    !result.contains("`");

        if (!pass) {
            PRINT_ACTUAL(result);
            return 1;
        }
        PRINT_ACTUAL("✓ 确认 bug: code 格式丢失");
        return 0;
    } END_TEST

    // ========================================
    // 测试 8: HTML 实体未解码 (Bug确认)
    // ========================================
    TEST("convertHtmlToMarkdown - HTML 实体未解码 (Bug确认)") {
        // 确认 bug: HTML 实体不会被解码
        QString input = "<p>A &amp; B &lt; C &gt; D &quot;E&quot;</p>";
        PRINT_INPUT("html", input);

        QString expected = "仍包含 '&amp;' (未解码为 '&')";
        PRINT_EXPECTED(expected);

        QString result = WebToolTest::convert(input);

        bool pass = result.contains("&amp;");

        if (!pass) {
            return Fail("包含 '&amp;'", result);
        }
        PRINT_ACTUAL("✓ 确认 bug: HTML 实体未解码");
        return 0;
    } END_TEST

    // ========================================
    // 测试 9: executeWebFetch - 空 URL
    // ========================================
    TEST("executeWebFetch - 空 URL") {
        QJsonObject input;
        input["url"] = "";
        PRINT_INPUT("url", "\"\"");

        QString expected = "返回 '错误: 未提供 URL'";
        PRINT_EXPECTED(expected);

        QString result = WebTool::executeWebFetch(input);

        if (result != "错误: 未提供 URL") {
            return Fail("错误: 未提供 URL", result);
        }
        PRINT_ACTUAL("✓ 正确返回空 URL 错误");
        return 0;
    } END_TEST

    // ========================================
    // 测试 10: executeWebFetch - 网络请求 (可选)
    // ========================================
    TEST("executeWebFetch - 网络请求 (可选, 依赖网络)") {
        // 此测试依赖网络连接，网络不可用时不视为失败
        QJsonObject input;
        input["url"] = "https://httpbin.org/html";
        PRINT_INPUT("url", "https://httpbin.org/html");

        QString expected = "非空结果 (网络不可用时跳过)";
        PRINT_EXPECTED(expected);

        QString result = WebTool::executeWebFetch(input);

        if (result.startsWith("抓取失败")) {
            PRINT_ACTUAL("⚠ 网络不可用，跳过: " + result.left(60));
            // 网络不可用不算失败
            return 0;
        }

        if (result.isEmpty()) {
            return Fail("非空结果", "空字符串");
        }
        PRINT_ACTUAL("✓ 成功抓取，内容长度: " + QString::number(result.length()));
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
