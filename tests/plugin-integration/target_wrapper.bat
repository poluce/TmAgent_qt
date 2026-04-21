@echo off
SetLocal EnableDelayedExpansion
(set PATH=E:\Qt\Qt5.14.2\5.14.2\mingw73_32\bin;!PATH!)
if defined QT_PLUGIN_PATH (
    set QT_PLUGIN_PATH=E:\Qt\Qt5.14.2\5.14.2\mingw73_32\plugins;!QT_PLUGIN_PATH!
) else (
    set QT_PLUGIN_PATH=E:\Qt\Qt5.14.2\5.14.2\mingw73_32\plugins
)
%*
EndLocal
