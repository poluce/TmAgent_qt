QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = ConversationToolEventCoordinatorTest

SOURCES += \
    ConversationToolEventCoordinatorTest.cpp \
    ../../src/core/service/conversation/ConversationToolEventCoordinator.cpp

HEADERS += \
    ../../src/core/service/include/ConversationToolEventCoordinator.h \
    ../../src/core/agent/ToolTypes.h \
    ../../src/core/service/include/TurnManager.h

INCLUDEPATH += ../../src ../../src/core/service/include




