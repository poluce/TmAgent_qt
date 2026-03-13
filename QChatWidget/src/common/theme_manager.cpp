#include "theme_manager.h"

#include <QGuiApplication>

static QFont defaultFont(int pointSize, int weight = -1)
{
    QFont f = QGuiApplication::font();
    f.setPointSize(pointSize);
    if (weight >= 0)
        f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

ThemeManager* ThemeManager::instance()
{
    static ThemeManager s_instance;
    return &s_instance;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
    initLightTheme();
    initDarkTheme();
}

ThemeManager::Theme ThemeManager::currentTheme() const
{
    return m_currentTheme;
}

void ThemeManager::setTheme(Theme theme)
{
    if (m_currentTheme != theme) {
        m_currentTheme = theme;
        emit themeChanged(theme);
    }
}

ThemeManager::ChatListStyle ThemeManager::chatListStyle() const
{
    return (m_currentTheme == Light) ? m_lightChatListStyle : m_darkChatListStyle;
}

ThemeManager::ChatWidgetStyle ThemeManager::chatWidgetStyle() const
{
    return (m_currentTheme == Light) ? m_lightChatWidgetStyle : m_darkChatWidgetStyle;
}

void ThemeManager::initLightTheme()
{
    // ChatList Light 主题
    m_lightChatListStyle.backgroundColor = Qt::white;
    m_lightChatListStyle.hoverColor = QColor(236, 238, 242);
    m_lightChatListStyle.selectedColor = QColor(220, 224, 230);
    m_lightChatListStyle.nameColor = QColor(25, 25, 25);
    m_lightChatListStyle.messageColor = QColor(150, 150, 150);
    m_lightChatListStyle.timeColor = QColor(180, 180, 180);
    m_lightChatListStyle.separatorColor = QColor(230, 230, 230);
    m_lightChatListStyle.badgeColor = QColor(250, 81, 81);
    m_lightChatListStyle.badgeTextColor = Qt::white;
    m_lightChatListStyle.nameFont = defaultFont(14);
    m_lightChatListStyle.messageFont = defaultFont(12);
    m_lightChatListStyle.timeFont = defaultFont(10);
    m_lightChatListStyle.badgeFont = defaultFont(9, QFont::Bold);

    // ChatWidget Light 主题
    m_lightChatWidgetStyle.backgroundColor = QColor(245, 245, 245);
    m_lightChatWidgetStyle.myBubbleColor = QColor(242, 244, 247);
    m_lightChatWidgetStyle.otherBubbleColor = QColor(242, 244, 247);
    m_lightChatWidgetStyle.bubbleRadius = 14;
    m_lightChatWidgetStyle.myAvatarColor = QColor(0, 120, 215);
    m_lightChatWidgetStyle.otherAvatarColor = QColor(200, 200, 200);
    m_lightChatWidgetStyle.myTextColor = QColor(25, 25, 25);
    m_lightChatWidgetStyle.otherTextColor = QColor(25, 25, 25);
    m_lightChatWidgetStyle.messageFont = defaultFont(9);
    m_lightChatWidgetStyle.avatarFont = defaultFont(10, QFont::Bold);
}

void ThemeManager::initDarkTheme()
{
    // ChatList Dark 主题
    m_darkChatListStyle.backgroundColor = QColor(30, 30, 30);
    m_darkChatListStyle.hoverColor = QColor(50, 50, 50);
    m_darkChatListStyle.selectedColor = QColor(70, 70, 70);
    m_darkChatListStyle.nameColor = QColor(230, 230, 230);
    m_darkChatListStyle.messageColor = QColor(150, 150, 150);
    m_darkChatListStyle.timeColor = QColor(120, 120, 120);
    m_darkChatListStyle.separatorColor = QColor(60, 60, 60);
    m_darkChatListStyle.badgeColor = QColor(220, 60, 60);
    m_darkChatListStyle.badgeTextColor = Qt::white;
    m_darkChatListStyle.nameFont = defaultFont(14);
    m_darkChatListStyle.messageFont = defaultFont(12);
    m_darkChatListStyle.timeFont = defaultFont(10);
    m_darkChatListStyle.badgeFont = defaultFont(9, QFont::Bold);

    // ChatWidget Dark 主题
    m_darkChatWidgetStyle.backgroundColor = QColor(25, 25, 25);
    m_darkChatWidgetStyle.myBubbleColor = QColor(74, 137, 52);
    m_darkChatWidgetStyle.otherBubbleColor = QColor(50, 50, 50);
    m_darkChatWidgetStyle.bubbleRadius = 14;
    m_darkChatWidgetStyle.myAvatarColor = QColor(0, 90, 170);
    m_darkChatWidgetStyle.otherAvatarColor = QColor(100, 100, 100);
    m_darkChatWidgetStyle.myTextColor = QColor(230, 230, 230);
    m_darkChatWidgetStyle.otherTextColor = QColor(230, 230, 230);
    m_darkChatWidgetStyle.messageFont = defaultFont(9);
    m_darkChatWidgetStyle.avatarFont = defaultFont(10, QFont::Bold);
}
