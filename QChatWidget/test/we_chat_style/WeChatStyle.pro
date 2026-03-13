QT       += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# 定义目标名称
TARGET = WeChatStyle
TEMPLATE = app

INCLUDEPATH += ../../src

SOURCES += \
    main.cpp

include(../../src/chatwidget/chat_widget.pri)

HEADERS +=

# 针对高分屏的一些默认适配（可选）
DEFINES += QT_DEPRECATED_WARNINGS
