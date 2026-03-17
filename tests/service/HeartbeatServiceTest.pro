# HeartbeatService 测试项目

QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = HeartbeatServiceTest

SOURCES += HeartbeatServiceTest.cpp \
           ChatPersistenceServiceHeartbeatMinimal.cpp \
           ../../src/core/service/background/HeartbeatService.cpp \
           ../../src/core/service/background/HeartbeatWake.cpp

HEADERS += ../../src/core/service/include/HeartbeatService.h \
           ../../src/core/service/include/HeartbeatWake.h \
           ../../src/core/persistence/ChatPersistenceService.h

INCLUDEPATH += ../../src ../../src/core/service/include




