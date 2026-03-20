#include "MainWindow.h"
#include "ApplicationServices.h"
#include "core/tools/ShellTool.h"
#include <QApplication>
#include <QMessageBox>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);

    // 将 shell 执行确认桥接放在 GUI 入口层，而不是具体窗口内部。
    ShellTool::setConfirmCallback([](const QString& command, const QString& workDir) -> bool {
        return QMessageBox::question(
                   nullptr,
                   QObject::tr("执行确认"),
                   QObject::tr("Agent 请求执行以下命令：\n\n%1\n\n工作目录：%2\n\n是否允许执行？")
                       .arg(command, workDir),
                   QMessageBox::Yes | QMessageBox::No,
                   QMessageBox::No)
            == QMessageBox::Yes;
    });

    auto* services = new ApplicationServices(&a);
    services->initialize();

    // 在事件循环启动后再创建 MainWindow，避免在 QWidget 基类构造时阻塞（Qt 平台/样式未就绪）。
    QTimer::singleShot(0, &a, [services]() {
        MainWindow* w = new MainWindow(*services);
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    });

    return a.exec();
}
