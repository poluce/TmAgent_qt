# TaskStateService 测试项目

QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = TaskStateServiceTest

SOURCES += TaskStateServiceTest.cpp \
           ChatPersistenceServiceTaskStateMinimal.cpp \
           ../../src/core/service/TaskStateService.cpp

HEADERS += ../../src/core/service/TaskStateService.h \
           ../../src/core/persistence/ChatPersistenceService.h

INCLUDEPATH += ../../src
