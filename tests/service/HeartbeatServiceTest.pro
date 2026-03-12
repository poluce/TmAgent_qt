# HeartbeatService 测试项目

QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = HeartbeatServiceTest

SOURCES += HeartbeatServiceTest.cpp \
           ChatPersistenceServiceHeartbeatMinimal.cpp \
           ../../src/core/service/HeartbeatService.cpp \
           ../../src/core/service/HeartbeatWake.cpp

HEADERS += ../../src/core/service/HeartbeatService.h \
           ../../src/core/service/HeartbeatWake.h \
           ../../src/core/persistence/ChatPersistenceService.h

INCLUDEPATH += ../../src
