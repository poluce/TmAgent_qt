QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = HeartbeatDecisionEngineTest

SOURCES += \
    HeartbeatDecisionEngineTest.cpp \
    ../../src/core/service/HeartbeatTypes.cpp \
    ../../src/core/service/background/HeartbeatDecisionEngine.cpp

HEADERS += \
    ../../src/core/service/include/HeartbeatTypes.h \
    ../../src/core/service/include/HeartbeatRuntimeState.h \
    ../../src/core/service/include/HeartbeatDecisionEngine.h

INCLUDEPATH += ../../src ../../src/core/service/include
