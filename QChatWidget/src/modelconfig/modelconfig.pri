MODELCONFIG_DIR = $$PWD

INCLUDEPATH += $$MODELCONFIG_DIR

HEADERS += \
    $$MODELCONFIG_DIR/model_config_types.h \
    $$MODELCONFIG_DIR/model_config_manager_page.h

SOURCES += \
    $$MODELCONFIG_DIR/model_config_manager_page.cpp

STYLES_QRC = $$clean_path($$PWD/../../resources/styles.qrc)
isEmpty(QCHAT_STYLES_QRC_INCLUDED) {
    QCHAT_STYLES_QRC_INCLUDED = 1
    RESOURCES += $$STYLES_QRC
}

include($$PWD/../common/qss_utils.pri)
