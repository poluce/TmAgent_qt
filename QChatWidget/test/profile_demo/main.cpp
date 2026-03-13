#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QPainter>
#include <QPixmap>
#include "profile/profile_widget.h"

static QPixmap buildAvatarPixmap(int size, const QString &text, const QColor &color) {
    QPixmap avatar(size, size);
    avatar.fill(Qt::transparent);

    QPainter painter(&avatar);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRect(0, 0, size, size), 10, 10);

    QFont font;
    font.setBold(true);
    font.setPointSize(20);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(avatar.rect(), Qt::AlignCenter, text);

    return avatar;
}

int main(int argc, char *argv[]) {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QApplication app(argc, argv);

    ProfileWidget profile;
    profile.setWindowTitle("ProfileWidget Playground");
    profile.resize(360, 640);
    profile.applyDefaultStyle();

    profile.setUserName(QStringLiteral("王小明"));
    profile.setTmId(QStringLiteral("foiajfoasj"));
    profile.setAvatar(buildAvatarPixmap(64, "WM", QColor(79, 128, 255)));

    profile.clearDetails();
    profile.addDetailItem(QStringLiteral("岗位"), QStringLiteral("研发一组 / IM 组件"));
    profile.addSeparator();
    profile.addDetailItem(QStringLiteral("角色"), QStringLiteral("是一名专研qt的工程师"));
    profile.addSeparator();
    profile.addDetailItem(QStringLiteral("更多"), QStringLiteral("设置 / 隐私"), true);

    QObject::connect(&profile, &ProfileWidget::messageClicked, []() {
        qDebug() << "Message button clicked";
    });
    QObject::connect(&profile, &ProfileWidget::moreOptionsClicked, []() {
        qDebug() << "More options clicked";
    });
    QObject::connect(&profile, &ProfileWidget::detailItemClicked,
                     [](const QString &title) {
        qDebug() << "Detail item clicked:" << title;
    });

    profile.show();
    return app.exec();
}
