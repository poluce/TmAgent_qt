PROFILE_DIR = $$PWD

INCLUDEPATH += $$PROFILE_DIR

SOURCES += \
    $$PROFILE_DIR/profile_widget.cpp

HEADERS += \
    $$PROFILE_DIR/profile_widget.h

STYLES_QRC = $$clean_path($$PWD/../../resources/styles.qrc)
isEmpty(QCHAT_STYLES_QRC_INCLUDED) {
    QCHAT_STYLES_QRC_INCLUDED = 1
    RESOURCES += $$STYLES_QRC
}

include($$PWD/../common/qss_utils.pri)
