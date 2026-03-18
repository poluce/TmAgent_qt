#!/bin/sh
PATH=/E/Qt/Qt5.14.2/5.14.2/mingw73_32/bin:$PATH
export PATH
QT_PLUGIN_PATH=/E/Qt/Qt5.14.2/5.14.2/mingw73_32/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}
export QT_PLUGIN_PATH
exec "$@"
