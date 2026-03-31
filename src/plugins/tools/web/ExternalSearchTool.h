#ifndef EXTERNALSEARCHTOOL_H
#define EXTERNALSEARCHTOOL_H

#include "core/agent/ToolTypes.h"
#include <QJsonObject>
#include <QString>

/**
 * @brief 外部搜索工具
 *
 * 通过 DuckDuckGo HTML 搜索提供免费的网页搜索能力。
 * 无需 API Key，无需付费。
 */
class ExternalSearchTool {
public:
    static constexpr const char* WEBSEARCH = "websearch";
    static Tool toolSchema();

    /**
     * @brief 执行网页搜索
     * @param input {query: "搜索关键词"}
     * @return 格式化的搜索结果文本
     */
    static QString executeWebSearch(const QJsonObject& input);

    friend class ExternalSearchToolTest;

private:
    /**
     * @brief 通过 DuckDuckGo HTML 版本执行搜索
     */
    static QString searchDuckDuckGo(const QString& query);

    /**
     * @brief 解析 DuckDuckGo HTML 搜索结果页
     *
     * DuckDuckGo HTML 版本的结果结构：
     *   <div class="result results_links results_links_deep web-result">
     *     <a class="result__a" href="...">标题</a>
     *     <a class="result__snippet">摘要</a>
     *   </div>
     */
    static QString parseSearchResults(const QString& html);

    /**
     * @brief 去除 HTML 标签，保留纯文本
     */
    static QString stripHtmlTags(QString text);

    /**
     * @brief 从 DuckDuckGo 重定向 URL 中提取实际链接
     *
     * DDG 的链接格式可能是:
     *   //duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.com&rut=...
     * 需要提取 uddg 参数的值
     */
    static QString extractRealUrl(const QString& rawUrl);
};

#endif // EXTERNALSEARCHTOOL_H
