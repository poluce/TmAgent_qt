#include <QtTest>
#include <QListView>

#include "chat_widget_view.h"

class ChatWidgetViewTest : public QObject {
    Q_OBJECT

private slots:
    void defaultModel_isNotNull();
    void setModel_usesProvidedModel();
    void scrollToBottom_noCrash();
    void refreshLayout_noCrash();
};

void ChatWidgetViewTest::defaultModel_isNotNull()
{
    ChatWidgetView view;
    QVERIFY(view.model() != nullptr);
}

void ChatWidgetViewTest::setModel_usesProvidedModel()
{
    ChatWidgetView view;
    auto* external = new ChatWidgetModel();

    view.setModel(external);

    QCOMPARE(view.model(), external);

    auto* listView = view.findChild<QListView*>("chatWidgetViewList");
    QVERIFY(listView != nullptr);
    QCOMPARE(listView->model(), static_cast<QAbstractItemModel*>(external));
}

static ChatWidgetView* createViewWithMessage()
{
    auto* view = new ChatWidgetView;
    ChatWidgetMessage message;
    message.content = "hello";
    view->model()->addMessage(message);
    return view;
}

void ChatWidgetViewTest::scrollToBottom_noCrash()
{
    QScopedPointer<ChatWidgetView> view(createViewWithMessage());
    view->scrollToBottom();
    QVERIFY(true);
}

void ChatWidgetViewTest::refreshLayout_noCrash()
{
    QScopedPointer<ChatWidgetView> view(createViewWithMessage());
    view->refreshLayout();
    QVERIFY(true);
}

QTEST_MAIN(ChatWidgetViewTest)
#include "tst_chatwidget_view.moc"
