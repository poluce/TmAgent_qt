QT += core sql
TEMPLATE = app
TARGET = tmagent-log

CONFIG += c++17 console
CONFIG -= app_bundle

INCLUDEPATH += ..
include(../core/logging/logging.pri)
include(../core/persistence/persistence-base.pri)

SOURCES += \
    main.cpp
