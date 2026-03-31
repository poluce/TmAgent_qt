# FileTool 测试项目

QT += core
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = FileToolTest

# 源文件
SOURCES += FileToolTest.cpp \
           ../../src/plugins/tools/workspace/FileTool.cpp

# 包含路径
INCLUDEPATH += ../../src ../../src/core/tools


