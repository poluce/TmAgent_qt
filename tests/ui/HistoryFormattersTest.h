#ifndef HISTORYFORMATTERSTEST_H
#define HISTORYFORMATTERSTEST_H

#include <QObject>

class HistoryFormattersTest : public QObject {
    Q_OBJECT

private slots:
    void historyPanelTitle_usesExecutionRecordWording();
    void emptyTexts_explainDerivedRuntimeSemantics();
    void helperTexts_clarifyRuntimeVsAuditBoundary();
    void rawFieldLabels_areLocalizedForUserFacingFields();
    void buildTurnListTitle_prefersClearUserFacingStatus();
    void summarizeEntry_buildsFixedSummaryFields();
    void executionHistoryModel_extractsTimeAndFilters();
    void executionHistoryModel_exposesSchemaDescriptor();
    void executionHistoryModel_mergesToolFollowupRequestsByTurn();
    void buildTurnSummaryText_containsStructuredSectionsAndDisclaimer();
    void rawHint_mentionsContextCompactionLifecycle();
};

#endif // HISTORYFORMATTERSTEST_H
