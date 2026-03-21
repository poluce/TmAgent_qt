QT += core sql
TEMPLATE = app
TARGET = tmagent-log

CONFIG += c++17 console
CONFIG -= app_bundle

INCLUDEPATH += src
include(src/core/logging/logging.pri)
include(src/core/persistence/persistence-base.pri)

SOURCES += \
    src/logcli/main.cpp
