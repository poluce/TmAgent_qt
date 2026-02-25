#ifndef LOGSUMMARIZER_H
#define LOGSUMMARIZER_H

#include <QJsonObject>
#include <QString>

namespace LogSummarizer {

QString summarizeEvent(const QJsonObject& obj);
QString summarizeMessage(const QJsonObject& obj);
QString clip(const QString& text, int maxChars);

} // namespace LogSummarizer

#endif // LOGSUMMARIZER_H
