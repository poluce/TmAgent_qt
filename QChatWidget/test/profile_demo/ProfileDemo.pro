QT += core gui widgets

CONFIG += c++11

TARGET = ProfileDemo
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

INCLUDEPATH += ../../src

SOURCES += \
    main.cpp

include(../../src/profile/profile_widget.pri)
