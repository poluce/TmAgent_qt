#ifndef TMAGENT_PLUGINMACROS_H
#define TMAGENT_PLUGINMACROS_H

#include <QtCore/QtPlugin>

/**
 * @file PluginMacros.h
 * @brief Plugin interface ID (IID) definitions for Qt plugin system
 * 
 * These macros define the interface identifiers used by Qt's plugin system
 * to identify and load TmAgent plugins at runtime.
 */

namespace TmAgent {

// Forward declarations
class IToolPlugin;
class IBackendPlugin;

} // namespace TmAgent

/**
 * @brief Interface ID for tool plugins
 * 
 * This IID is used by Qt's plugin system to identify tool plugin implementations.
 * Tool plugins provide executable tools that can be called by AI agents.
 * 
 * Format: "org.tmagent.ToolPlugin/MAJOR.MINOR"
 * Version: 1.0 corresponds to SDK version 1.0.x
 */
#define TMAGENT_TOOL_PLUGIN_IID "org.tmagent.ToolPlugin/1.0"

/**
 * @brief Interface ID for backend plugins
 * 
 * This IID is used by Qt's plugin system to identify backend plugin implementations.
 * Backend plugins provide AI model interfaces for delegate and teammate functionality.
 * 
 * Format: "org.tmagent.BackendPlugin/MAJOR.MINOR"
 * Version: 1.0 corresponds to SDK version 1.0.x
 */
#define TMAGENT_BACKEND_PLUGIN_IID "org.tmagent.BackendPlugin/1.0"

/**
 * @brief Declare the IToolPlugin interface to Qt's meta-object system
 * 
 * This macro must be placed after the class definition in the header file.
 * It enables Qt's plugin loader to recognize and instantiate tool plugins.
 */
Q_DECLARE_INTERFACE(TmAgent::IToolPlugin, TMAGENT_TOOL_PLUGIN_IID)

/**
 * @brief Declare the IBackendPlugin interface to Qt's meta-object system
 * 
 * This macro must be placed after the class definition in the header file.
 * It enables Qt's plugin loader to recognize and instantiate backend plugins.
 */
Q_DECLARE_INTERFACE(TmAgent::IBackendPlugin, TMAGENT_BACKEND_PLUGIN_IID)

#endif // TMAGENT_PLUGINMACROS_H
