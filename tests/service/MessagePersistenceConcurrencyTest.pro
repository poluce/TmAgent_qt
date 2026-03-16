# Message 持久化并发测试项目

QT += core sql
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = MessagePersistenceConcurrencyTest

SOURCES += MessagePersistenceConcurrencyTest.cpp \
           ../../src/core/persistence/ChatPersistenceService.cpp \
           ../../src/core/persistence/DatabaseManager.cpp \
           ../../src/core/model/IdentityProfile.cpp

HEADERS += ../../src/core/persistence/ChatPersistenceService.h \
           ../../src/core/persistence/DatabaseManager.h \
           ../../src/core/model/IdentityProfile.h \
           ../../src/core/model/Message.h

INCLUDEPATH += ../../src
