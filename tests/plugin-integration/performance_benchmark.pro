QT += core testlib concurrent
QT -= gui

CONFIG += c++17 console testcase
CONFIG -= app_bundle

TARGET = performance_benchmark
TEMPLATE = app

# Include SDK
TMAGENT_SDK_PATH = ../../tmagent-plugin-sdk
include($TMAGENT_SDK_PATH/tmagent-plugin-sdk.pri)

SOURCES += \
    performance_benchmark.cpp

# Output directory
CONFIG(debug, debug|release) {
    DESTDIR = $$OUT_PWD/debug
} else {
    DESTDIR = $$OUT_PWD/release
}
