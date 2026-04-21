#include "ExternalSearchTool.h"
#include <tmagent/support/ToolSchemaSupport.h>

#include <QDebug>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

TmAgent::Tool ExternalSearchTool::toolSchema()
{
    return makeToolSchema(
        QString::fromLatin1(WEBSEARCH),
        QStringLiteral("搜索互联网获取实时信息。"),
        QJsonObject {
            { QStringLiteral("query"), makePropertySchema(QStringLiteral("string"), QStringLiteral("搜索关键词")) }
        },
        QStringList { QStringLiteral("query") });
}

QString ExternalSearchTool::executeWebSearch(const QJsonObject& input)
{
    QString query = input["query"].toString().trimmed();
    if (query.isEmpty()) {
        return "错误: 未提供搜索关键词 (query)";
    }
    return searchDuckDuckGo(query);
}

QString ExternalSearchTool::searchDuckDuckGo(const QString& query)
{
    // 1. 构造搜索 URL
    QUrl url("https://html.duckduckgo.com/html/");
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("q", query);
    url.setQuery(urlQuery);

    // 2. 发送 HTTP GET 请求
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                                                        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "text/html");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");

    QNetworkReply* reply = manager.get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timer.start(20000); // 20秒超时
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        return "搜索超时: 请检查网络连接";
    }
    timer.stop();

    if (reply->error() != QNetworkReply::NoError) {
        QString err = "搜索失败: " + reply->errorString();
        reply->deleteLater();
        return err;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QString html = QString::fromUtf8(data);

    // 3. 解析搜索结果
    return parseSearchResults(html);
}

QString ExternalSearchTool::parseSearchResults(const QString& html)
{
    QStringList results;
    const int maxResults = 10;

    // 提取每个搜索结果块
    QRegularExpression resultRe(
        R"RE(<a[^>]*class="result__a"[^>]*href="([^"]*)"[^>]*>(.*?)</a>)RE",
        QRegularExpression::DotMatchesEverythingOption);

    QRegularExpression snippetRe(
        R"RE(<a[^>]*class="result__snippet"[^>]*>(.*?)</a>)RE",
        QRegularExpression::DotMatchesEverythingOption);

    // 收集所有标题+链接
    QRegularExpressionMatchIterator titleIt = resultRe.globalMatch(html);
    QRegularExpressionMatchIterator snippetIt = snippetRe.globalMatch(html);

    int count = 0;
    while (titleIt.hasNext() && count < maxResults) {
        QRegularExpressionMatch titleMatch = titleIt.next();
        QString rawUrl = titleMatch.captured(1);
        QString title = stripHtmlTags(titleMatch.captured(2)).trimmed();

        // DuckDuckGo 的链接可能是重定向 URL，提取实际 URL
        QString link = extractRealUrl(rawUrl);

        // 获取对应的摘要
        QString snippet;
        if (snippetIt.hasNext()) {
            QRegularExpressionMatch snippetMatch = snippetIt.next();
            snippet = stripHtmlTags(snippetMatch.captured(1)).trimmed();
        }

        if (title.isEmpty() || link.isEmpty())
            continue;

        count++;
        QString entry = QString("%1. %2\n   %3").arg(count).arg(title).arg(link);
        if (!snippet.isEmpty()) {
            entry += QString("\n   %1").arg(snippet);
        }
        results.append(entry);
    }

    if (results.isEmpty()) {
        return "未找到相关搜索结果。";
    }

    return QString("搜索结果 (共 %1 条):\n\n%2")
        .arg(results.size())
        .arg(results.join("\n\n"));
}

QString ExternalSearchTool::stripHtmlTags(QString text)
{
    text.remove(QRegularExpression("<[^>]*>"));
    // 解码常见 HTML 实体
    text.replace("&amp;", "&");
    text.replace("&lt;", "<");
    text.replace("&gt;", ">");
    text.replace("&quot;", "\"");
    text.replace("&#39;", "'");
    text.replace("&nbsp;", " ");
    return text;
}

QString ExternalSearchTool::extractRealUrl(const QString& rawUrl)
{
    if (rawUrl.contains("uddg=")) {
        QUrl duckUrl(rawUrl.startsWith("//") ? "https:" + rawUrl : rawUrl);
        QUrlQuery query(duckUrl);
        QString realUrl = query.queryItemValue("uddg", QUrl::FullyDecoded);
        if (!realUrl.isEmpty())
            return realUrl;
    }
    // 如果不是重定向格式，直接返回
    if (rawUrl.startsWith("http"))
        return rawUrl;
    if (rawUrl.startsWith("//"))
        return "https:" + rawUrl;
    return rawUrl;
}
