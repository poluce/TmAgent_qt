#include <QDebug>
#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUrl>
#include <QUrlQuery>

#include "core/tools/ExternalSearchTool.h"

static int g_testCount = 0;
static int g_passCount = 0;

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

// 用于 friend 访问 ExternalSearchTool 私有方法
class ExternalSearchToolTest {
public:
    static QString searchDuckDuckGo(const QString& query) {
        return ExternalSearchTool::searchDuckDuckGo(query);
    }
    static QString parseSearchResults(const QString& html) {
        return ExternalSearchTool::parseSearchResults(html);
    }
    static QString stripHtmlTags(const QString& text) {
        return ExternalSearchTool::stripHtmlTags(text);
    }
    static QString extractRealUrl(const QString& rawUrl) {
        return ExternalSearchTool::extractRealUrl(rawUrl);
    }
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "    ExternalSearchTool 测试套件 (DuckDuckGo)";
    qDebug().noquote() << "════════════════════════════════════════";

    // ========================================
    // 测试 1: 工具名称常量
    // ========================================
    TEST("工具名称常量") {
        QString webSearch = ExternalSearchTool::WEBSEARCH;
        PRINT_INPUT("WEBSEARCH", webSearch);
        PRINT_EXPECTED("WEBSEARCH='websearch'");

        if (webSearch != "websearch") {
            return Fail("websearch", webSearch);
        }

        PRINT_ACTUAL("✓ 常量值正确");
        return 0;
    } END_TEST

    // ========================================
    // 测试 2: executeWebSearch - 空查询
    // ========================================
    TEST("executeWebSearch - 空查询返回错误") {
        QJsonObject emptyInput;
        PRINT_INPUT("input", "{}");
        PRINT_EXPECTED("返回 '错误: 未提供搜索关键词 (query)'");

        QString result = ExternalSearchTool::executeWebSearch(emptyInput);

        if (!result.startsWith("错误:")) {
            return Fail("以 '错误:' 开头", result.left(60));
        }

        PRINT_ACTUAL(QString("✓ 返回: '%1'").arg(result));
        return 0;
    } END_TEST

    // ========================================
    // 测试 3: executeWebSearch - 空白查询
    // ========================================
    TEST("executeWebSearch - 空白字符串查询返回错误") {
        QJsonObject input;
        input["query"] = "   ";
        PRINT_INPUT("query", "'   ' (纯空格)");
        PRINT_EXPECTED("返回错误 (trimmed 后为空)");

        QString result = ExternalSearchTool::executeWebSearch(input);

        if (!result.startsWith("错误:")) {
            return Fail("以 '错误:' 开头", result.left(60));
        }

        PRINT_ACTUAL(QString("✓ 返回: '%1'").arg(result));
        return 0;
    } END_TEST

    // ========================================
    // 测试 4: stripHtmlTags - 去除标签
    // ========================================
    TEST("stripHtmlTags - 去除 HTML 标签") {
        QString html = "<b>Hello</b> <a href=\"url\">World</a> &amp; <i>Qt</i>";
        PRINT_INPUT("html", html);
        PRINT_EXPECTED("'Hello World & Qt'");

        QString result = ExternalSearchToolTest::stripHtmlTags(html);

        if (result != "Hello World & Qt") {
            return Fail("Hello World & Qt", result);
        }

        PRINT_ACTUAL(QString("✓ '%1'").arg(result));
        return 0;
    } END_TEST

    // ========================================
    // 测试 5: stripHtmlTags - HTML 实体解码
    // ========================================
    TEST("stripHtmlTags - HTML 实体解码") {
        QString html = "&lt;div&gt; &quot;test&quot; &#39;ok&#39; &nbsp;space";
        PRINT_INPUT("html", html);
        PRINT_EXPECTED("'<div> \"test\" 'ok' space'");

        QString result = ExternalSearchToolTest::stripHtmlTags(html);

        if (!result.contains("<div>") || !result.contains("\"test\"") || !result.contains("'ok'")) {
            return Fail("包含解码后的实体", result);
        }

        PRINT_ACTUAL(QString("✓ '%1'").arg(result));
        return 0;
    } END_TEST

    // ========================================
    // 测试 6: extractRealUrl - DDG 重定向 URL
    // ========================================
    TEST("extractRealUrl - DuckDuckGo 重定向 URL") {
        QString rawUrl = "//duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.com%2Fpage&rut=abc123";
        PRINT_INPUT("rawUrl", rawUrl);
        PRINT_EXPECTED("'https://example.com/page'");

        QString result = ExternalSearchToolTest::extractRealUrl(rawUrl);

        if (result != "https://example.com/page") {
            return Fail("https://example.com/page", result);
        }

        PRINT_ACTUAL(QString("✓ '%1'").arg(result));
        return 0;
    } END_TEST

    // ========================================
    // 测试 7: extractRealUrl - 直接 URL
    // ========================================
    TEST("extractRealUrl - 直接 HTTP URL 不变") {
        QString rawUrl = "https://doc.qt.io/qt-6/index.html";
        PRINT_INPUT("rawUrl", rawUrl);
        PRINT_EXPECTED("原样返回");

        QString result = ExternalSearchToolTest::extractRealUrl(rawUrl);

        if (result != rawUrl) {
            return Fail(rawUrl, result);
        }

        PRINT_ACTUAL(QString("✓ '%1'").arg(result));
        return 0;
    } END_TEST

    // ========================================
    // 测试 8: extractRealUrl - // 开头的 URL
    // ========================================
    TEST("extractRealUrl - 协议相对 URL 补全 https") {
        QString rawUrl = "//example.com/path";
        PRINT_INPUT("rawUrl", rawUrl);
        PRINT_EXPECTED("'https://example.com/path'");

        QString result = ExternalSearchToolTest::extractRealUrl(rawUrl);

        if (result != "https://example.com/path") {
            return Fail("https://example.com/path", result);
        }

        PRINT_ACTUAL(QString("✓ '%1'").arg(result));
        return 0;
    } END_TEST

    // ========================================
    // 测试 9: parseSearchResults - 模拟 DDG HTML
    // ========================================
    TEST("parseSearchResults - 解析模拟 DuckDuckGo HTML") {
        QString mockHtml = R"(
<div class="result results_links results_links_deep web-result">
  <a class="result__a" href="//duckduckgo.com/l/?uddg=https%3A%2F%2Fdoc.qt.io%2Fqt-6%2F&rut=1">Qt 6 Documentation</a>
  <a class="result__snippet">Official Qt 6 documentation and API reference.</a>
</div>
<div class="result results_links results_links_deep web-result">
  <a class="result__a" href="https://stackoverflow.com/questions/123">Qt Network Example</a>
  <a class="result__snippet">How to use QNetworkAccessManager in Qt.</a>
</div>
)";
        PRINT_INPUT("html", "模拟 2 条 DuckDuckGo 搜索结果");
        PRINT_EXPECTED("解析出 2 条结果，包含标题、链接、摘要");

        QString result = ExternalSearchToolTest::parseSearchResults(mockHtml);

        bool hasHeader = result.contains("搜索结果");
        bool hasTitle1 = result.contains("Qt 6 Documentation");
        bool hasLink1 = result.contains("doc.qt.io");
        bool hasSnippet1 = result.contains("Official Qt 6 documentation");
        bool hasTitle2 = result.contains("Qt Network Example");
        bool hasLink2 = result.contains("stackoverflow.com");

        if (!hasHeader || !hasTitle1 || !hasLink1 || !hasSnippet1 || !hasTitle2 || !hasLink2) {
            PRINT_ACTUAL(result.left(300));
            return Fail("包含 2 条完整结果", "缺少部分内容");
        }

        PRINT_ACTUAL("✓ 正确解析出 2 条结果");
        qDebug().noquote() << "  [备注] 结果预览:\n" << result.left(300);
        return 0;
    } END_TEST

    // ========================================
    // 测试 10: parseSearchResults - 空 HTML
    // ========================================
    TEST("parseSearchResults - 空 HTML 返回未找到") {
        QString emptyHtml = "<html><body>No results</body></html>";
        PRINT_INPUT("html", "无搜索结果的 HTML");
        PRINT_EXPECTED("返回 '未找到相关搜索结果。'");

        QString result = ExternalSearchToolTest::parseSearchResults(emptyHtml);

        if (result != "未找到相关搜索结果。") {
            return Fail("未找到相关搜索结果。", result);
        }

        PRINT_ACTUAL(QString("✓ '%1'").arg(result));
        return 0;
    } END_TEST

    // ========================================
    // 测试 11: parseSearchResults - 结果数量限制
    // ========================================
    TEST("parseSearchResults - 最多返回 10 条结果") {
        // 构造 15 条模拟结果
        QString mockHtml;
        for (int i = 1; i <= 15; i++) {
            mockHtml += QString(
                R"(<a class="result__a" href="https://example.com/%1">Title %1</a>)"
                R"(<a class="result__snippet">Snippet %1</a>)"
            ).arg(i);
        }
        PRINT_INPUT("html", "15 条模拟搜索结果");
        PRINT_EXPECTED("最多返回 10 条");

        QString result = ExternalSearchToolTest::parseSearchResults(mockHtml);

        // 检查包含 "10." 但不包含 "11."
        bool has10 = result.contains("10. Title 10");
        bool has11 = result.contains("11. Title 11");

        if (!has10 || has11) {
            return Fail("包含第10条，不包含第11条",
                QString("has10=%1, has11=%2").arg(has10).arg(has11));
        }

        PRINT_ACTUAL("✓ 正确限制为 10 条结果");
        return 0;
    } END_TEST

    // ========================================
    // 测试 12: 真实网络搜索 (可选，需网络)
    // ========================================
    TEST("searchDuckDuckGo - 真实网络搜索 (需网络)") {
        PRINT_INPUT("query", "Qt framework C++");
        PRINT_EXPECTED("返回非空结果，不崩溃 (网络依赖，允许搜索失败)");

        QString result = ExternalSearchToolTest::searchDuckDuckGo("Qt framework C++");

        if (result.isEmpty()) {
            return Fail("非空字符串", "空字符串");
        }

        // 只要不崩溃、返回非空就算通过
        bool isError = result.startsWith("搜索失败:") || result.startsWith("搜索超时:");
        bool isResult = result.contains("搜索结果") || result.contains("未找到");

        if (!isError && !isResult) {
            return Fail("搜索结果或错误信息", result.left(80));
        }

        PRINT_ACTUAL(QString("✓ 返回: '%1'").arg(result.left(120)));
        if (result.contains("搜索结果")) {
            qDebug().noquote() << "  [备注] 搜索成功，返回了实际结果";
        } else {
            qDebug().noquote() << "  [备注] 搜索未返回结果 (可能是网络问题):" << result.left(80);
        }
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
