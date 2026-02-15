# ExternalSearchTool 测试项目

QT += core network
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = ExternalSearchToolTest

# 源文件
SOURCES += ExternalSearchToolTest.cpp

# 包含路径
INCLUDEPATH += ../../src
