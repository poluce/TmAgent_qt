#include <QtTest>

#include "chat_widget_model.h"

class ChatWidgetModelTest : public QObject {
    Q_OBJECT

private slots:
    void setMessages_sortsAndDedupesById();
    void appendMessages_dedupesById();
};

static ChatWidgetMessage makeMessage(const QString& id, const QDateTime& timestamp)
{
    ChatWidgetMessage message;
    message.messageId = id;
    message.timestamp = timestamp;
    message.content = id;
    return message;
}

void ChatWidgetModelTest::setMessages_sortsAndDedupesById()
{
    ChatWidgetModel model;

    QList<ChatWidgetMessage> messages;
    messages << makeMessage("1", QDateTime(QDate(2024, 1, 1), QTime(10, 0)))
             << makeMessage("2", QDateTime(QDate(2024, 1, 1), QTime(9, 0)))
             << makeMessage("1", QDateTime(QDate(2024, 1, 1), QTime(11, 0)));

    model.setMessages(messages);

    QCOMPARE(model.rowCount(), 2);

    QModelIndex first = model.index(0, 0);
    QModelIndex second = model.index(1, 0);
    QCOMPARE(first.data(ChatWidgetModel::ChatWidgetMessageIdRole).toString(), QString("2"));
    QCOMPARE(second.data(ChatWidgetModel::ChatWidgetMessageIdRole).toString(), QString("1"));
    QCOMPARE(second.data(ChatWidgetModel::ChatWidgetTimestampRole).toDateTime(),
             QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
}

void ChatWidgetModelTest::appendMessages_dedupesById()
{
    ChatWidgetModel model;

    QList<ChatWidgetMessage> initial;
    initial << makeMessage("1", QDateTime(QDate(2024, 1, 1), QTime(9, 0)))
            << makeMessage("2", QDateTime(QDate(2024, 1, 1), QTime(10, 0)));
    model.setMessages(initial);

    QList<ChatWidgetMessage> appended;
    appended << makeMessage("2", QDateTime(QDate(2024, 1, 1), QTime(10, 30)))
             << makeMessage("3", QDateTime(QDate(2024, 1, 1), QTime(11, 0)));

    model.appendMessages(appended);

    QCOMPARE(model.rowCount(), 3);

    QModelIndex last = model.index(2, 0);
    QCOMPARE(last.data(ChatWidgetModel::ChatWidgetMessageIdRole).toString(), QString("3"));
}

QTEST_MAIN(ChatWidgetModelTest)
#include "tst_chatwidget_model.moc"
