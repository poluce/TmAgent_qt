QT += core
CONFIG += console c++17
TEMPLATE = app
TARGET = MessageRouterTest

INCLUDEPATH += ../../src

SOURCES += \
    MessageRouterTest.cpp \
    ../../src/core/service/MessageRouter.cpp

HEADERS += \
    ../../src/core/service/MessageRouter.h \
    ../../src/core/model/Session.h
