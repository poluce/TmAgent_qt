QT += core sql
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = LoggingToolchainTest

INCLUDEPATH += ../../src
include(../../src/core/logging/logging.pri)

SOURCES += \
    LoggingToolchainTest.cpp
