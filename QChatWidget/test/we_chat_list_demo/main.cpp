#include <QApplication>
#include <QAction>
#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>
#include "chat_list_widget.h"
#include "chat_list_roles.h"

int main(int argc, char *argv[]) {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("Qt WeChat List Demo (Modular)");
    window.resize(360, 600);

    QVBoxLayout *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    ChatListWidget *chatWidget = new ChatListWidget();
    chatWidget->applyStyleSheetFile("chat_list.qss");
    chatWidget->enableSearchFiltering(true);
    chatWidget->setSearchRoles(QList<int>() << ChatListNameRole << ChatListMessageRole);
    chatWidget->setSearchPlaceholder("搜索联系人/消息");

    chatWidget->addChatItem("文件传输助手", "[图片] IMG_2026.jpg", "17:52", QColor(255, 170, 0), 0);
    chatWidget->addChatItem("腾讯新闻", "Qt 6.8 发布了！", "14:30", QColor(0, 120, 215), 1);
    chatWidget->addChatItem("产品经理", "今晚加班吗？", "12:05", QColor(100, 100, 100), 5);

    for (int i = 0; i < 15; ++i) {
        chatWidget->addChatItem(QString("群聊 %1").arg(i), "收到请回复", "10:00", QColor(Qt::lightGray), 0);
    }

    // Header actions 演示
    chatWidget->addHeaderAction("新建会话", "add");
    chatWidget->addHeaderAction("删除当前会话", "remove");
    chatWidget->addHeaderAction("清空列表", "clear");
    chatWidget->addHeaderAction("显示/隐藏搜索", "toggle_search");

    QObject::connect(chatWidget, &ChatListWidget::headerActionTriggered,
                     [&](QAction *action) {
        qDebug() << "Header action triggered:" << action->text() << action->data();
        const QString key = action->data().toString();
        if (key == "add") {
            chatWidget->addChatItem(QString("新会话 %1").arg(QDateTime::currentDateTime().toString("hh:mm:ss")),
                    "Hello", "刚刚", QColor(0, 153, 255), 0);
        } else if (key == "remove") {
            if (!chatWidget->removeCurrentChat()) {
                qDebug() << "No chat selected to remove";
            }
        } else if (key == "clear") {
            chatWidget->clearChats();
        } else if (key == "toggle_search") {
            chatWidget->setSearchVisible(!chatWidget->searchBar()->isVisible());
        }
    });

    QObject::connect(chatWidget, &ChatListWidget::searchTextChanged, [](const QString &text) {
        qDebug() << "Search text changed:" << text;
    });

    QObject::connect(chatWidget, &ChatListWidget::chatItemActivated,
                     [](const QString &name, const QString &message,
                        const QString &time, const QColor &avatarColor, int unreadCount) {
        qDebug() << "Chat activated:" << name << message << time << avatarColor << unreadCount;
    });

    QObject::connect(chatWidget, &ChatListWidget::clicked, [](const QModelIndex &index) {
        qDebug() << "Clicked row:" << index.row();
    });
    QObject::connect(chatWidget, &ChatListWidget::doubleClicked, [](const QModelIndex &index) {
        qDebug() << "Double clicked row:" << index.row();
    });
    QObject::connect(chatWidget, &ChatListWidget::pressed, [](const QModelIndex &index) {
        qDebug() << "Pressed row:" << index.row();
    });
    QObject::connect(chatWidget, &ChatListWidget::activated, [](const QModelIndex &index) {
        qDebug() << "Activated row:" << index.row();
    });
    QObject::connect(chatWidget, &ChatListWidget::entered, [](const QModelIndex &index) {
        qDebug() << "Entered row:" << index.row();
    });
    QObject::connect(chatWidget, &ChatListWidget::viewportEntered, []() {
        qDebug() << "Viewport entered";
    });
    QObject::connect(chatWidget, &ChatListWidget::currentChanged,
                     [](const QModelIndex &current, const QModelIndex &previous) {
        qDebug() << "Current changed:" << current.row() << "prev:" << previous.row();
    });
    QObject::connect(chatWidget, &ChatListWidget::selectionChanged,
                     [](const QItemSelection &selected, const QItemSelection &deselected) {
        qDebug() << "Selection changed. Selected:" << selected.indexes().size()
                 << "Deselected:" << deselected.indexes().size();
    });

    // 定时演示更新/删除/修改接口
    QTimer::singleShot(1000, [=]() {
        chatWidget->updateChatItem(0, "文件传输助手", "[语音] 2''", "18:10",
                                   QColor(255, 170, 0), 2);
    });
    QTimer::singleShot(1800, [=]() {
        chatWidget->updateChatItemByName("产品经理", ChatListMessageRole,
                                         QString("临时改需求：明早上线"));
    });
    QTimer::singleShot(2600, [=]() {
        chatWidget->removeChatItemByName("腾讯新闻");
    });
    QTimer::singleShot(3400, [=]() {
        chatWidget->addChatItem("新同事", "大家好，我是新来的~", "刚刚",
                                QColor(120, 200, 120), 0);
    });

    layout->addWidget(chatWidget);

    window.show();
    return app.exec();
}
