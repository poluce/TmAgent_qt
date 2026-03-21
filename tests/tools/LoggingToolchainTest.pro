QT += core sql
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = LoggingToolchainTest

SOURCES += \
    LoggingToolchainTest.cpp \
    ../../src/core/logging/LogQueryEngine.cpp \
    ../../src/core/logging/LogRecordSupport.cpp \
    ../../src/core/logging/LogCatalog.cpp \
    ../../src/core/logging/LogFollower.cpp \
    ../../src/core/logging/LogHealthCheck.cpp \
    ../../src/core/logging/LogIndex.cpp \
    ../../src/core/observability/AlertManager.cpp \
    ../../src/core/observability/MetricsCollector.cpp

HEADERS += \
    ../../src/core/logging/LogFollower.h \
    ../../src/core/observability/AlertManager.h

INCLUDEPATH += ../../src
