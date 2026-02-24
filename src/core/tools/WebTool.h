#ifndef WEBTOOL_H
#define WEBTOOL_H

#include <QJsonObject>
#include <QString>

/**
 * @brief Web 工具
 *
 * 提供网页抓取和简单的 HTML 到 Markdown 转换功能。
 */
class WebTool {
public:
    static constexpr const char* WEB_FETCH = "web_fetch";

    /**
     * @brief 抓取网页内容
     * @param input {url, format}
     */
    static QString executeWebFetch(const QJsonObject& input);

    friend class WebToolTest;

private:
    /**
     * @brief 简易 HTML 转 Markdown
     * 极简实现，处理标题、链接、列表和段落
     */
    static QString convertHtmlToMarkdown(QString html);
};

#endif // WEBTOOL_H
