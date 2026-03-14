#ifndef EXECUTIONRECORDWINDOW_H
#define EXECUTIONRECORDWINDOW_H

#include "HistoryFormatters.h"
#include "core/service/ExecutionHistoryModel.h"
#include <QVector>
#include <QWidget>

class QComboBox;
class QFrame;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QVBoxLayout;

class ExecutionRecordWindow : public QWidget {
    Q_OBJECT
public:
    explicit ExecutionRecordWindow(QWidget* parent = nullptr);

    void setSessionTitle(const QString& title);
    void setHistoryState(const QVector<ExecutionHistory::Record>& records,
                         const QVector<int>& visibleIndexes,
                         int currentVisibleRow,
                         ExecutionHistory::FilterMode filterMode,
                         int recentLimit);

signals:
    void filterModeChanged(ExecutionHistory::FilterMode mode);
    void recentLimitChanged(int limit);
    void visibleRowChanged(int row);
    void clearHistoryRequested();

private slots:
    void onTurnSelectionChanged(int row);

private:
    void setupUi();
    void setStatusBadge(const QString& text, const QString& tone);
    void setSceneState(const ExecutionHistory::Record* record, bool hasHistory);
    void populateInsights(const ExecutionHistory::Record* record, bool hasHistory);
    void populateToolProcess(const QVector<ExecutionHistory::ToolActivity>& toolActivities);
    void populateMetrics(const ExecutionHistory::Record& record);
    void populateDecision(const ExecutionHistory::Record& record);
    void populateDiagnosis(const ExecutionHistory::Record& record);
    void populateTimeline(const ExecutionHistory::Record& record);
    void renderEvidenceLayer(QPlainTextEdit* view, const QString& title, const QJsonObject& layer);
    void applyRecord(const ExecutionHistory::Record& record);
    void resetRecord(bool hasHistory);
    void updateDetailsForRow(int row);
    void refreshTurnCardStyles();
    void addFlowCard(QVBoxLayout* layout,
                     const QString& indexText,
                     const QString& title,
                     const QString& detail,
                     const QString& tone,
                     const QString& badgeText,
                     bool isLast);
    QString inferCurrentStage(const ExecutionHistory::Record& record) const;
    QString inferRootCause(const ExecutionHistory::Record& record) const;
    QString inferReliability(const ExecutionHistory::Record& record) const;
    QString inferSuggestedAction(const ExecutionHistory::Record& record) const;
    QString inferBlocker(const ExecutionHistory::Record& record) const;
    QString inferNextStep(const ExecutionHistory::Record& record) const;

    QVector<ExecutionHistory::Record> m_records;
    QVector<int> m_visibleIndexes;
    bool m_syncingState = false;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_sessionLabel = nullptr;
    QLabel* m_turnCountBadge = nullptr;
    QLabel* m_introLabel = nullptr;
    QLabel* m_sceneTitleLabel = nullptr;
    QLabel* m_sceneHelperLabel = nullptr;
    QLabel* m_sceneSuccessChip = nullptr;
    QLabel* m_sceneWaitingChip = nullptr;
    QLabel* m_sceneFailureChip = nullptr;
    QComboBox* m_filterCombo = nullptr;
    QComboBox* m_recentCombo = nullptr;
    QPushButton* m_clearHistoryBtn = nullptr;
    QListWidget* m_turnList = nullptr;
    QTabWidget* m_evidenceTabs = nullptr;
    QVBoxLayout* m_timelineLayout = nullptr;
    QVBoxLayout* m_toolLayout = nullptr;
    QPlainTextEdit* m_summaryLayerView = nullptr;
    QPlainTextEdit* m_eventLayerView = nullptr;
    QPlainTextEdit* m_interactionLayerView = nullptr;
    QPlainTextEdit* m_auditLayerView = nullptr;
    QLabel* m_historySummaryTypeValue = nullptr;
    QLabel* m_historySummaryStatusBadge = nullptr;
    QLabel* m_historySummaryTimeValue = nullptr;
    QLabel* m_historySummaryMetaValue = nullptr;
    QLabel* m_historyRawHintLabel = nullptr;
    QLabel* m_metricStatusValue = nullptr;
    QLabel* m_metricDurationValue = nullptr;
    QLabel* m_metricToolCountValue = nullptr;
    QLabel* m_metricExceptionValue = nullptr;
    QLabel* m_metricStageValue = nullptr;
    QLabel* m_decisionConclusionValue = nullptr;
    QLabel* m_decisionBlockerValue = nullptr;
    QLabel* m_decisionNextValue = nullptr;
    QLabel* m_diagnosisRootCauseValue = nullptr;
    QLabel* m_diagnosisReliabilityValue = nullptr;
    QLabel* m_diagnosisActionValue = nullptr;
    QLabel* m_quickResultValue = nullptr;
    QLabel* m_quickBlockerValue = nullptr;
    QLabel* m_quickCauseValue = nullptr;
    QLabel* m_quickActionValue = nullptr;
    QLabel* m_historySummaryInputValue = nullptr;
    QLabel* m_historySummaryOutputValue = nullptr;
    QLabel* m_historySummaryToolValue = nullptr;
    QLabel* m_historySummaryErrorValue = nullptr;
};

#endif // EXECUTIONRECORDWINDOW_H
