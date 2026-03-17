QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = AgentPulseRegistryTest

SOURCES += \
    AgentPulseRegistryTest.cpp \
    ../../src/core/service/background/AgentPulse.cpp

HEADERS += \
    ../../src/core/service/include/AgentPulse.h \
    ../../src/core/service/include/AgentPulseRegistry.h \
    ../../src/core/service/include/ChatCoordinatorSupport.h \
    ../../src/core/agent/ToolTypes.h

INCLUDEPATH += ../../src ../../src/core/service/include




