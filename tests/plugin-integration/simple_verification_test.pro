QT += core testlib
QT -= gui

CONFIG += qt console warn_on depend_includepath testcase c++17
CONFIG -= app_bundle

TEMPLATE = app

# Include SDK
TMAGENT_SDK_PATH = $$PWD/../../tmagent-plugin-sdk
include($$TMAGENT_SDK_PATH/tmagent-plugin-sdk.pri)

SOURCES += \
    simple_verification_test.cpp

TARGET = simple_verification_test
