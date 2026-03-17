# Memory reflection (M4) headless integration test

QT += core network sql
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = MemoryReflectionTest

SOURCES += MemoryReflectionTest.cpp \
           QtLinkStubs.cpp \
           ../../src/core/memory/MemoryDocument.cpp \
           ../../src/core/memory/MemoryManager.cpp \
           ChatPersistenceServiceMinimal.cpp

INCLUDEPATH += ../../src

# Keep this test lightweight: allow dropping unused code paths
QMAKE_CXXFLAGS += -ffunction-sections -fdata-sections
QMAKE_LFLAGS += -Wl,--gc-sections


