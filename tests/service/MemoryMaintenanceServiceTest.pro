QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = MemoryMaintenanceServiceTest

SOURCES += \
    MemoryMaintenanceServiceTest.cpp \
    ../../src/core/service/MemoryMaintenanceService.cpp

HEADERS += \
    ../../src/core/service/include/MemoryMaintenanceService.h \
    ../../src/core/service/include/TurnManager.h

INCLUDEPATH += ../../src ../../src/core/service/include
