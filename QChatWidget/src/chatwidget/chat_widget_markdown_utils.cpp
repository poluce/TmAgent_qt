#include "chat_widget_markdown_utils.h"
#include "md4c-html.h"
#include <QByteArray>
#include <QDebug>

static void process_output(const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    QString* out = static_cast<QString*>(userdata);
    out->append(QString::fromUtf8(text, size));
}

QString ChatWidgetMarkdownUtils::renderMarkdown(const QString& input)
{
    if (input.isEmpty()) {
        return QString();
    }

    QByteArray ba = input.toUtf8();
    QString output;

    // GFM Dialect + Tables + Underline + Strikethrough
    unsigned parser_flags = MD_DIALECT_GITHUB | MD_FLAG_UNDERLINE | MD_FLAG_STRIKETHROUGH | MD_FLAG_TABLES | MD_FLAG_TASKLISTS;
    unsigned renderer_flags = MD_HTML_FLAG_DEBUG;

    int ret = md_html(ba.data(), ba.size(), process_output, &output, parser_flags, renderer_flags);

    if (ret != 0) {
        qWarning() << "Markdown parsing failed";
        return input; // Fallback to raw text
    }

    return output;
}
