QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = ConversationFinishCoordinatorTest

SOURCES += \
    ConversationFinishCoordinatorTest.cpp \
    ../../src/core/service/conversation/ConversationFinishCoordinator.cpp \
    ../../src/core/service/background/HeartbeatReplyUtils.cpp

HEADERS += \
    ../../src/core/service/include/ConversationFinishCoordinator.h \
    ../../src/core/service/include/HeartbeatReplyUtils.h \
    ../../src/core/model/Message.h \
    ../../src/core/service/include/TurnManager.h

INCLUDEPATH += ../../src ../../src/core/service/include




