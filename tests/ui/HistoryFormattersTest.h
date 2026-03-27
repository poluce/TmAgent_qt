#ifndef HISTORYFORMATTERSTEST_H
#define HISTORYFORMATTERSTEST_H

#include <QObject>

class HistoryFormattersTest : public QObject {
    Q_OBJECT

private slots:
    void toolLogWindowTitle_usesExpectedWording();
};

#endif // HISTORYFORMATTERSTEST_H
