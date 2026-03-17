# SchedulerService 测试项目

QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = SchedulerServiceTest

SOURCES += SchedulerServiceTest.cpp \
           ChatPersistenceServiceSchedulerMinimal.cpp \
           ../../src/core/service/background/SchedulerService.cpp

HEADERS += ../../src/core/service/include/SchedulerService.h \
           ../../src/core/persistence/ChatPersistenceService.h

INCLUDEPATH += ../../src ../../src/core/service/include




