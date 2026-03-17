# HeartbeatReplyUtils 测试项目

QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = HeartbeatReplyUtilsTest

SOURCES += HeartbeatReplyUtilsTest.cpp \
           ../../src/core/service/background/HeartbeatReplyUtils.cpp

HEADERS += ../../src/core/service/include/HeartbeatReplyUtils.h

INCLUDEPATH += ../../src ../../src/core/service/include




