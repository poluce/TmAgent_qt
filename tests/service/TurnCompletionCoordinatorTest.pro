QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = TurnCompletionCoordinatorTest

SOURCES += \
    TurnCompletionCoordinatorTest.cpp \
    ../../src/core/service/conversation/TurnCompletionCoordinator.cpp \
    ../../src/core/service/background/HeartbeatReplyUtils.cpp

HEADERS += \
    ../../src/core/service/include/TurnCompletionCoordinator.h \
    ../../src/core/service/include/HeartbeatReplyUtils.h \
    ../../src/core/model/Message.h \
    ../../src/core/service/include/TurnManager.h

INCLUDEPATH += ../../src ../../src/core/service/include




