QT += core
CONFIG += console c++17
TEMPLATE = app
TARGET = MessageRouterTest

INCLUDEPATH += ../../src ../../src/core/service/include

SOURCES += \
    MessageRouterTest.cpp \
    ../../src/core/service/conversation/MessageRouter.cpp

HEADERS += \
    ../../src/core/service/include/MessageRouter.h \
    ../../src/core/model/Session.h




