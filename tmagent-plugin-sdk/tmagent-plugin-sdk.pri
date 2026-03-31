# TmAgent Plugin SDK - qmake Configuration
# Include this file in your plugin project to use the SDK
# Usage: include(/path/to/tmagent-plugin-sdk.pri)

isEmpty(TMAGENT_SDK_ROOT) {
    TMAGENT_SDK_ROOT = $$PWD
}

# Add SDK include path
INCLUDEPATH += $$TMAGENT_SDK_ROOT/include

# SDK Headers - Plugin Interfaces
HEADERS += \
    $$TMAGENT_SDK_ROOT/include/tmagent/plugin/IToolPlugin.h \
    $$TMAGENT_SDK_ROOT/include/tmagent/plugin/IToolProvider.h \
    $$TMAGENT_SDK_ROOT/include/tmagent/plugin/IToolPluginHost.h \
    $$TMAGENT_SDK_ROOT/include/tmagent/plugin/IBackendPlugin.h \
    $$TMAGENT_SDK_ROOT/include/tmagent/plugin/IDelegateBackend.h \
    $$TMAGENT_SDK_ROOT/include/tmagent/plugin/ITeammateBackend.h

# SDK Headers - Data Types
HEADERS += \
    $$TMAGENT_SDK_ROOT/include/tmagent/types/ToolTypes.h \
    $$TMAGENT_SDK_ROOT/include/tmagent/types/PluginTypes.h \
    $$TMAGENT_SDK_ROOT/include/tmagent/types/BackendTypes.h \
    $$TMAGENT_SDK_ROOT/include/tmagent/types/CommonTypes.h

# SDK Headers - Support Utilities
HEADERS += \
    $$TMAGENT_SDK_ROOT/include/tmagent/support/ToolSchemaBuilder.h \
    $$TMAGENT_SDK_ROOT/include/tmagent/support/PluginMacros.h

# SDK Headers - Version
HEADERS += \
    $$TMAGENT_SDK_ROOT/include/tmagent/version.h

# Define SDK version macros
DEFINES += \
    TMAGENT_SDK_VERSION_MAJOR=1 \
    TMAGENT_SDK_VERSION_MINOR=0 \
    TMAGENT_SDK_VERSION_PATCH=0
