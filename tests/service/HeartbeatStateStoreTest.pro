QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = HeartbeatStateStoreTest

SOURCES += \
    HeartbeatStateStoreTest.cpp \
    ../../src/core/service/HeartbeatTypes.cpp

HEADERS += \
    ../../src/core/service/include/HeartbeatTypes.h \
    ../../src/core/service/include/HeartbeatRuntimeState.h \
    ../../src/core/service/include/HeartbeatStateStore.h

INCLUDEPATH += ../../src ../../src/core/service/include




