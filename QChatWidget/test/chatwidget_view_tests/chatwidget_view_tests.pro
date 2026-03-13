TEMPLATE = app
TARGET = chatwidget_view_tests
QT += testlib core gui widgets
CONFIG += console c++17

INCLUDEPATH += $$PWD/../../src/chatwidget \
    $$PWD/../../3rdparty/md4c

SOURCES += \
    tst_chatwidget_view.cpp \
    $$PWD/../../src/chatwidget/chat_widget_view.cpp \
    $$PWD/../../src/chatwidget/chat_widget_model.cpp \
    $$PWD/../../src/chatwidget/chat_widget_delegate.cpp \
    $$PWD/../../src/chatwidget/chat_widget_markdown_utils.cpp \
    $$PWD/../../3rdparty/md4c/md4c.c \
    $$PWD/../../3rdparty/md4c/md4c-html.c \
    $$PWD/../../3rdparty/md4c/entity.c

HEADERS += \
    $$PWD/../../src/chatwidget/chat_widget_view.h \
    $$PWD/../../src/chatwidget/chat_widget_model.h \
    $$PWD/../../src/chatwidget/chat_widget_delegate.h \
    $$PWD/../../src/chatwidget/chat_widget_markdown_utils.h \
    $$PWD/../../3rdparty/md4c/md4c.h \
    $$PWD/../../3rdparty/md4c/md4c-html.h \
    $$PWD/../../3rdparty/md4c/entity.h
