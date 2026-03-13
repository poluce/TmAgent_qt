QT       += core gui widgets

CONFIG += c++17

TARGET = ModelImportTest
TEMPLATE = app

# 包含路径
INCLUDEPATH += ../../src

# 包含模块
include(../../src/modelconfig/modelconfig.pri)

RESOURCES += ../../resources/styles.qrc

SOURCES += main.cpp
