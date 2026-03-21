QT += core sql
TEMPLATE = app
TARGET = tmagent-log

CONFIG += c++17 console
CONFIG -= app_bundle

INCLUDEPATH += ../src

SOURCES += \
    main.cpp \
    ../src/core/persistence/DatabaseManager.cpp \
    ../src/core/logging/LogQueryEngine.cpp \
    ../src/core/logging/LogRecordSupport.cpp \
    ../src/core/logging/LogCatalog.cpp \
    ../src/core/logging/LogHealthCheck.cpp \
    ../src/core/logging/LogFollower.cpp

HEADERS += \
    ../src/core/persistence/DatabaseManager.h \
    ../src/core/logging/LogQueryEngine.h \
    ../src/core/logging/LogCatalog.h \
    ../src/core/logging/LogHealthCheck.h \
    ../src/core/logging/LogFollower.h
