QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = MemoryToolWriteServiceTest

SOURCES += \
    MemoryToolWriteServiceTest.cpp \
    ../../src/core/service/MemoryToolWriteService.cpp

HEADERS += \
    ../../src/core/service/include/MemoryToolWriteService.h \
    ../../src/core/agent/ToolTypes.h \
    ../../src/core/service/include/TurnManager.h

INCLUDEPATH += ../../src ../../src/core/service/include
