#ifndef WEBTOOL_H
#define WEBTOOL_H

#include <QDebug>
#include <QEventLoop>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QRegularExpression>
#include <QTimer>

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
    static QString executeWebFetch(const QJsonObject& input)
    {
        QString urlStr = input["url"].toString();
        QString format = input["format"].toString("markdown"); // 默认 markdown

        if (urlStr.isEmpty())
            return "错误: 未提供 URL";

        QNetworkAccessManager manager;
        QNetworkRequest request;
        request.setUrl(QUrl(urlStr));
        request.setHeader(QNetworkRequest::UserAgentHeader, "TmAgent/1.0 (Qt; Windows)");

        QNetworkReply* reply = manager.get(request);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

        timer.start(15000); // 15秒超时
        loop.exec();

        if (reply->error() != QNetworkReply::NoError) {
            QString error = "抓取失败: " + reply->errorString();
            reply->deleteLater();
            return error;
        }

        QByteArray data = reply->readAll();
        QString content = QString::fromUtf8(data);
        QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        reply->deleteLater();

        if (format == "markdown" && contentType.contains("text/html")) {
            return convertHtmlToMarkdown(content);
        }

        return content;
    }

    friend class WebToolTest;

private:
    /**
     * @brief 简易 HTML 转 Markdown
     * 极简实现，处理标题、链接、列表和段落
     */
    static QString convertHtmlToMarkdown(QString html)
    {
        // 1. 移除 script, style, head 等无关标签
        html.remove(QRegularExpression("<script[^>]*>.*?</script>", QRegularExpression::DotMatchesEverythingOption));
        html.remove(QRegularExpression("<style[^>]*>.*?</style>", QRegularExpression::DotMatchesEverythingOption));
        html.remove(QRegularExpression("<head[^>]*>.*?</head>", QRegularExpression::DotMatchesEverythingOption));
        html.remove(QRegularExpression("<nav[^>]*>.*?</nav>", QRegularExpression::DotMatchesEverythingOption));
        html.remove(QRegularExpression("<footer[^>]*>.*?</footer>", QRegularExpression::DotMatchesEverythingOption));

        // 2. 标题转换 (h1-h6)
        for (int i = 1; i <= 6; ++i) {
            QString hashes(i, '#');
            QRegularExpression re(QString("<h%1[^>]*>(.*?)</h%1>").arg(i), QRegularExpression::DotMatchesEverythingOption);
            html.replace(re, "\n\n" + hashes + " \\1\n\n");
        }

        // 3. 链接 [text](url)
        html.replace(QRegularExpression("<a[^>]*href=\"([^\"]*)\"[^>]*>(.*?)</a>"), "[\\2](\\1)");

        // 4. 加粗和斜体
        html.replace(QRegularExpression("<b[^>]*>(.*?)</b>"), "**\\1**");
        html.replace(QRegularExpression("<strong[^>]*>(.*?)</strong>"), "**\\1**");
        html.replace(QRegularExpression("<i[^>]*>(.*?)</i>"), "*\\1*");
        html.replace(QRegularExpression("<em[^>]*>(.*?)</em>"), "*\\1*");

        // 5. 段落和换行
        html.replace(QRegularExpression("<p[^>]*>(.*?)</p>"), "\n\n\\1\n\n");
        html.replace(QRegularExpression("<br[^>]*>"), "\n");

        // 6. 列表
        html.replace(QRegularExpression("<li[^>]*>(.*?)</li>"), "\n- \\1");
        html.remove(QRegularExpression("</?ul[^>]*>"));
        html.remove(QRegularExpression("</?ol[^>]*>"));

        // 7. 去除剩余所有 HTML 标签
        html.remove(QRegularExpression("<[^>]*>"));

        // 8. 清理多余换行
        html.replace(QRegularExpression("\n{3,}"), "\n\n");

        return html.trimmed();
    }
};

#endif // WEBTOOL_H
