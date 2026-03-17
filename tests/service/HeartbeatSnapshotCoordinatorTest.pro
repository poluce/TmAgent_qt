QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = HeartbeatSnapshotCoordinatorTest

SOURCES += \
    HeartbeatSnapshotCoordinatorTest.cpp \
    ../../src/core/service/background/HeartbeatSnapshotCoordinator.cpp

HEADERS += \
    ../../src/core/service/include/HeartbeatSnapshotCoordinator.h

INCLUDEPATH += ../../src ../../src/core/service/include




