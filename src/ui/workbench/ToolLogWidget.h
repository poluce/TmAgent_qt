#ifndef TOOLLOGWIDGET_H
#define TOOLLOGWIDGET_H

#include "../core/agent/ToolTypes.h"
#include <QSet>
#include <QVector>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTextBrowser;

class ToolLogWidget : public QWidget {
    Q_OBJECT
public:
    explicit ToolLogWidget(QWidget* parent = nullptr);

public slots:
    void logEvent(const ToolExecutionEvent& event);
    void clearLogs();

private slots:
    void onFilterChanged();
    void onTogglePause();
    void onSearchTextChanged(const QString& text);
    void onContextMenu(const QPoint& pos);

private:
    void setupUI();
    void rebuildDisplay();
    void appendEventHtml(const ToolExecutionEvent& event);
    QString formatTimestamp() const;
    bool matchesFilter(const ToolExecutionEvent& event) const;
    QString buildEventHtml(const ToolExecutionEvent& event) const;

    // UI 组件
    QLineEdit* m_searchEdit = nullptr;
    QComboBox* m_toolNameFilter = nullptr;
    QComboBox* m_statusFilter = nullptr;
    QPushButton* m_pauseBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QTextBrowser* m_logDisplay = nullptr;

    // 数据模型
    QVector<ToolExecutionEvent> m_allEvents;
    QVector<ToolExecutionEvent> m_pendingEvents;
    QSet<QString> m_knownToolNames;
    bool m_paused = false;

    static const int kMaxEvents = 2000;
};

#endif // TOOLLOGWIDGET_H
