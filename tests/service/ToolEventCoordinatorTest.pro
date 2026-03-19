QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = ToolEventCoordinatorTest

SOURCES += \
    ToolEventCoordinatorTest.cpp \
    ../../src/core/service/conversation/ToolEventCoordinator.cpp

HEADERS += \
    ../../src/core/service/include/ToolEventCoordinator.h \
    ../../src/core/agent/ToolTypes.h \
    ../../src/core/service/include/CoordinatorContext.h \
    ../../src/core/model/Message.h \
    ../../src/core/service/include/TurnManager.h

INCLUDEPATH += ../../src ../../src/core/service/include




