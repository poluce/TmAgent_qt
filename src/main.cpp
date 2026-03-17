#include "MainWindow.h"
#include <QApplication>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);

    // 在事件循环启动后再创建 MainWindow，避免在 QWidget 基类构造时阻塞（Qt 平台/样式未就绪）。
    QTimer::singleShot(0, &a, []() {
        MainWindow* w = new MainWindow();
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    });

    return a.exec();
}
