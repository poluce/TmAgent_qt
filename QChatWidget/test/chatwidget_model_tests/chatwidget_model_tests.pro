TEMPLATE = app
TARGET = chatwidget_model_tests
QT += testlib core
CONFIG += console c++17

INCLUDEPATH += $$PWD/../../src/chatwidget

SOURCES += \
    tst_chatwidget_model.cpp \
    $$PWD/../../src/chatwidget/chat_widget_model.cpp

HEADERS += \
    $$PWD/../../src/chatwidget/chat_widget_model.h
