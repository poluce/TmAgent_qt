#include <QtTest>
#include <type_traits>

#include "chat_widget.h"
#include "chat_widget_view.h"
#include "chat_widget_model.h"

template <typename T, typename... Args>
class HasAddMessageOverload {
private:
    template <typename U>
    static auto test(int) -> decltype(std::declval<U>().addMessage(std::declval<Args>()...), std::true_type {});

    template <typename>
    static std::false_type test(...);

public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

static_assert(!HasAddMessageOverload<ChatWidget, const QString&, bool, const QString&>::value,
              "remove addMessage(content, isMine, sender) overload");
static_assert(!HasAddMessageOverload<ChatWidget, const QString&, const QString&>::value,
              "remove addMessage(content, senderId) overload");
static_assert(!HasAddMessageOverload<ChatWidget, const QString&, const QString&, const QString&, const QString&>::value,
              "remove addMessage(content, senderId, displayName, avatarPath) overload");

class ChatWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void defaultViewAndModel_notNull();
    void modelMatchesView();
    void addMessage_params_withoutSenderId_usesIsMine();
    void addMessage_params_withSenderId_usesCurrentUser();
};

void ChatWidgetTest::defaultViewAndModel_notNull()
{
    ChatWidget widget;
    QVERIFY(widget.view() != nullptr);
    QVERIFY(widget.model() != nullptr);
}

void ChatWidgetTest::modelMatchesView()
{
    ChatWidget widget;
    QCOMPARE(widget.model(), widget.view()->model());
}

void ChatWidgetTest::addMessage_params_withoutSenderId_usesIsMine()
{
    ChatWidget widget;

    ChatWidget::MessageParams params;
    params.content = "hello";
    params.displayName = "Alice";
    params.isMine = true;

    widget.addMessage(params);

    QCOMPARE(widget.model()->rowCount(), 1);
    QModelIndex idx = widget.model()->index(0, 0);
    QCOMPARE(idx.data(ChatWidgetModel::ChatWidgetSenderRole).toString(), QString("Alice"));
    QCOMPARE(idx.data(ChatWidgetModel::ChatWidgetIsMineRole).toBool(), true);
}

void ChatWidgetTest::addMessage_params_withSenderId_usesCurrentUser()
{
    ChatWidget widget;
    widget.setCurrentUser("user-1");

    ChatWidget::MessageParams params;
    params.content = "hi";
    params.senderId = "user-1";
    params.displayName = "Me";

    widget.addMessage(params);

    QCOMPARE(widget.model()->rowCount(), 1);
    QModelIndex idx = widget.model()->index(0, 0);
    QCOMPARE(idx.data(ChatWidgetModel::ChatWidgetSenderIdRole).toString(), QString("user-1"));
    QCOMPARE(idx.data(ChatWidgetModel::ChatWidgetIsMineRole).toBool(), true);
    QCOMPARE(idx.data(ChatWidgetModel::ChatWidgetSenderRole).toString(), QString("Me"));
}

QTEST_MAIN(ChatWidgetTest)
#include "tst_chatwidget.moc"
