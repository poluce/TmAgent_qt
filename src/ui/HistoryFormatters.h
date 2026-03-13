#ifndef HISTORYFORMATTERS_H
#define HISTORYFORMATTERS_H

#include <QJsonObject>
#include <QString>

namespace HistoryFormatters {

struct EntrySummary {
    QString listTitle;
    QString kindLabel;
    QString statusLabel;
    QString statusTone;
    QString inputSummary;
    QString outputSummary;
    QString toolSummary;
    QString metaSummary;
    QString errorSummary;
    QString detailText;
};

QString historyPanelTitle(int count);
QString historyPanelIntroText();
QString emptyHistoryText();
QString emptySelectionText();
QString summaryTabTitle();
QString rawTabTitle();
QString rawTabHintText();
QString summaryDetailsTitle();
QString toolLogWindowTitle();
EntrySummary summarizeEntry(const QJsonObject& entry, int row);
QString buildTurnListTitle(const QJsonObject& entry, int row);
QString buildTurnSummaryText(const QJsonObject& entry, int row);

} // namespace HistoryFormatters

#endif // HISTORYFORMATTERS_H
