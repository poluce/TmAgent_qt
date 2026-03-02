QT += core
TEMPLATE = app
TARGET = tmagent-log

CONFIG += c++17 console
CONFIG -= app_bundle

INCLUDEPATH += ../src

SOURCES += \
    main.cpp \
    ../src/core/logging/LogQueryEngine.cpp

HEADERS += \
    ../src/core/logging/LogQueryEngine.h
