#ifndef THEME_MANAGER_H
#define THEME_MANAGER_H

#include <QColor>
#include <QFont>
#include <QObject>

class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum Theme {
        Light,
        Dark
    };
    Q_ENUM(Theme)

    static ThemeManager* instance();

    Theme currentTheme() const;
    void setTheme(Theme theme);

    // ChatList 样式预设
    struct ChatListStyle {
        int itemHeight = 72;
        int avatarSize = 50;
        int margin = 12;
        int badgeSize = 18;
        int avatarCornerRadius = 8;

        QColor backgroundColor;
        QColor hoverColor;
        QColor selectedColor;
        QColor nameColor;
        QColor messageColor;
        QColor timeColor;
        QColor separatorColor;
        QColor badgeColor;
        QColor badgeTextColor;

        QFont nameFont;
        QFont messageFont;
        QFont timeFont;
        QFont badgeFont;

        bool showSeparator = true;
        bool showUnreadBadge = true;
    };

    // ChatWidget 样式预设
    struct ChatWidgetStyle {
        int avatarSize = 40;
        int margin = 10;
        int bubblePadding = 10;
        int bubbleRadius = 14;

        QColor myBubbleColor;
        QColor otherBubbleColor;
        QColor myAvatarColor;
        QColor otherAvatarColor;
        QColor myTextColor;
        QColor otherTextColor;
        QColor backgroundColor;

        QFont messageFont;
        QFont avatarFont;
    };

    ChatListStyle chatListStyle() const;
    ChatWidgetStyle chatWidgetStyle() const;

signals:
    void themeChanged(Theme theme);

private:
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override = default;

    void initLightTheme();
    void initDarkTheme();

    Theme m_currentTheme = Light;
    ChatListStyle m_lightChatListStyle;
    ChatListStyle m_darkChatListStyle;
    ChatWidgetStyle m_lightChatWidgetStyle;
    ChatWidgetStyle m_darkChatWidgetStyle;
};

#endif // THEME_MANAGER_H
