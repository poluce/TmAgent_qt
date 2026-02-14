#ifndef TOOLLOGWIDGET_H
#define TOOLLOGWIDGET_H

#include "../core/agent/ToolTypes.h"
#include <QWidget>

class QPushButton;
class QTextBrowser;

class ToolLogWidget : public QWidget {
    Q_OBJECT
public:
    explicit ToolLogWidget(QWidget *parent = nullptr);

public slots:
    void logEvent(const ToolExecutionEvent& event);
    void clearLogs();

private:
    void setupUI();
    QString formatTimestamp();

    QTextBrowser *m_logDisplay = nullptr;
    QPushButton *m_clearBtn = nullptr;
};

#endif // TOOLLOGWIDGET_H
