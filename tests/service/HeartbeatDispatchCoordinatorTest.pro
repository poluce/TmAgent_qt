QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = HeartbeatDispatchCoordinatorTest

SOURCES += \
    HeartbeatDispatchCoordinatorTest.cpp \
    ../../src/core/service/background/HeartbeatDispatchCoordinator.cpp

HEADERS += \
    ../../src/core/service/include/HeartbeatDispatchCoordinator.h

INCLUDEPATH += ../../src ../../src/core/service/include




