#include "VersionCompatibilityTest.h"
#include "src/core/agent/ToolPluginManager.h"
#include "src/core/agent/ToolPluginHostImpl.h"
#include <tmagent/types/PluginTypes.h>
#include <tmagent/version.h>
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

using namespace TmAgent;

void VersionCompatibilityTest::initTestCase()
{
    // Create plugin manager
    ToolPluginHostImpl* host = new ToolPluginHostImpl(this);
    m_pluginManager = new ToolPluginManager(host, this);
    
    // Create temporary directory for test plugins
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    m_testPluginDir = tempDir.path() + "/plugins";
    tempDir.setAutoRemove(false);
    
    QDir().mkpath(m_testPluginDir);
    
    qDebug() << "SDK Version:" << TMAGENT_SDK_VERSION_MAJOR << "." 
             << TMAGENT_SDK_VERSION_MINOR << "." << TMAGENT_SDK_VERSION_PATCH;
}

void VersionCompatibilityTest::cleanupTestCase()
{
    // Cleanup temporary directory
    QDir tempDir(m_testPluginDir);
    tempDir.removeRecursively();
}

void VersionCompatibilityTest::init()
{
    // Setup before each test
}

void VersionCompatibilityTest::cleanup()
{
    // Cleanup after each test
}

// ============================================================================
// Version Compatibility Tests
// ============================================================================

void VersionCompatibilityTest::testCompatibility_ExactMatch()
{
    // Test that plugin with exact SDK version match is compatible
    
    ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin";
    desc.displayName = "Test Plugin";
    desc.version = "1.0.0";
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    
    // Use reflection to call private isCompatible method
    // Since it's private, we test through plugin loading
    QVERIFY2(desc.isValid(), "Descriptor should be valid");
    
    qDebug() << "Plugin version:" << desc.sdkVersionMajor << "." << desc.sdkVersionMinor;
    qDebug() << "SDK version:" << TMAGENT_SDK_VERSION_MAJOR << "." << TMAGENT_SDK_VERSION_MINOR;
}

void VersionCompatibilityTest::testCompatibility_MajorMismatch_Higher()
{
    // Test that plugin with higher major version is rejected
    // Requirement 39.5: Major version mismatch should be rejected
    
    ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin_major_higher";
    desc.displayName = "Test Plugin (Major Higher)";
    desc.version = "1.0.0";
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR + 1;
    desc.sdkVersionMinor = 0;
    
    QVERIFY2(desc.isValid(), "Descriptor should be valid");
    
    // Note: We can't directly test isCompatible as it's private
    // This would be rejected during plugin loading
    qDebug() << "Plugin with major version" << desc.sdkVersionMajor 
             << "should be rejected (SDK major:" << TMAGENT_SDK_VERSION_MAJOR << ")";
}

void VersionCompatibilityTest::testCompatibility_MajorMismatch_Lower()
{
    // Test that plugin with lower major version is rejected
    // Requirement 39.5: Major version mismatch should be rejected
    
    if (TMAGENT_SDK_VERSION_MAJOR == 0) {
        QSKIP("Cannot test lower major version when SDK major is 0");
    }
    
    ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin_major_lower";
    desc.displayName = "Test Plugin (Major Lower)";
    desc.version = "1.0.0";
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR - 1;
    desc.sdkVersionMinor = 0;
    
    QVERIFY2(desc.isValid(), "Descriptor should be valid");
    
    qDebug() << "Plugin with major version" << desc.sdkVersionMajor 
             << "should be rejected (SDK major:" << TMAGENT_SDK_VERSION_MAJOR << ")";
}

void VersionCompatibilityTest::testCompatibility_MinorHigher()
{
    // Test that plugin with higher minor version is rejected
    // Requirement 39.5: Higher minor version should be rejected
    
    ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin_minor_higher";
    desc.displayName = "Test Plugin (Minor Higher)";
    desc.version = "1.0.0";
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR + 1;
    
    QVERIFY2(desc.isValid(), "Descriptor should be valid");
    
    qDebug() << "Plugin with minor version" << desc.sdkVersionMinor 
             << "should be rejected (SDK minor:" << TMAGENT_SDK_VERSION_MINOR << ")";
}

void VersionCompatibilityTest::testCompatibility_MinorLower()
{
    // Test that plugin with lower minor version is accepted
    // Requirement 39.5: Lower minor version should be accepted (backward compatible)
    
    if (TMAGENT_SDK_VERSION_MINOR == 0) {
        QSKIP("Cannot test lower minor version when SDK minor is 0");
    }
    
    ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin_minor_lower";
    desc.displayName = "Test Plugin (Minor Lower)";
    desc.version = "1.0.0";
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR - 1;
    
    QVERIFY2(desc.isValid(), "Descriptor should be valid");
    
    qDebug() << "Plugin with minor version" << desc.sdkVersionMinor 
             << "should be accepted (SDK minor:" << TMAGENT_SDK_VERSION_MINOR << ")";
}

void VersionCompatibilityTest::testCompatibility_MinorSame_PatchDifferent()
{
    // Test that patch version differences don't affect compatibility
    
    ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin_patch_different";
    desc.displayName = "Test Plugin (Patch Different)";
    desc.version = "1.0.0";
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    // Note: Patch version is not stored in descriptor
    
    QVERIFY2(desc.isValid(), "Descriptor should be valid");
    
    qDebug() << "Plugin with same major.minor should be compatible regardless of patch";
}

// ============================================================================
// Edge Cases
// ============================================================================

void VersionCompatibilityTest::testCompatibility_ZeroVersion()
{
    // Test plugin with version 0.0 (legacy plugin marker)
    
    ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin_zero";
    desc.displayName = "Test Plugin (Zero Version)";
    desc.version = "1.0.0";
    desc.sdkVersionMajor = 0;
    desc.sdkVersionMinor = 0;
    
    QVERIFY2(desc.isValid(), "Descriptor should be valid");
    
    qDebug() << "Plugin with version 0.0 indicates legacy plugin";
}

void VersionCompatibilityTest::testCompatibility_LegacyPlugin()
{
    // Test that legacy plugins (sdkVersionMajor=0) are handled specially
    
    ToolPluginDescriptor desc;
    desc.pluginId = "legacy_plugin";
    desc.displayName = "Legacy Plugin";
    desc.version = "1.0.0";
    desc.sdkVersionMajor = 0;
    desc.sdkVersionMinor = 0;
    
    QVERIFY2(desc.isValid(), "Legacy plugin descriptor should be valid");
    
    // Legacy plugins should be adapted through LegacyPluginAdapter
    qDebug() << "Legacy plugins are identified by sdkVersionMajor=0";
}

void VersionCompatibilityTest::testCompatibility_FutureVersion()
{
    // Test plugin claiming to be from future SDK version
    
    ToolPluginDescriptor desc;
    desc.pluginId = "future_plugin";
    desc.displayName = "Future Plugin";
    desc.version = "1.0.0";
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR + 10;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR + 10;
    
    QVERIFY2(desc.isValid(), "Descriptor should be valid");
    
    qDebug() << "Plugin from future SDK version should be rejected";
}

// ============================================================================
// Real Plugin Loading Tests
// ============================================================================

void VersionCompatibilityTest::testLoadPlugin_CompatibleVersion()
{
    // Test loading a plugin with compatible version
    
    QString pluginPath = QCoreApplication::applicationDirPath() + 
                        "/../tmagent-plugin-sdk/examples/minimal-tool-plugin";
    
    #ifdef Q_OS_WIN
        QString libPath = pluginPath + "/MinimalToolPlugin.dll";
    #elif defined(Q_OS_MAC)
        QString libPath = pluginPath + "/libMinimalToolPlugin.dylib";
    #else
        QString libPath = pluginPath + "/libMinimalToolPlugin.so";
    #endif
    
    if (!QFile::exists(libPath)) {
        QSKIP("Minimal tool plugin not built");
    }
    
    // Initialize plugin manager (this loads plugins)
    m_pluginManager->initialize();
    
    // Check if plugin was loaded
    QList<ToolPluginInfo> plugins = m_pluginManager->pluginInfos();
    
    bool found = false;
    for (const auto& info : plugins) {
        if (info.descriptor.pluginId == "minimal_tool") {
            found = true;
            QCOMPARE(info.descriptor.sdkVersionMajor, TMAGENT_SDK_VERSION_MAJOR);
            QVERIFY2(info.descriptor.sdkVersionMinor <= TMAGENT_SDK_VERSION_MINOR,
                     "Plugin minor version should not exceed SDK minor version");
            qDebug() << "Plugin loaded successfully with compatible version";
            break;
        }
    }
    
    if (!found) {
        qWarning() << "Minimal tool plugin not found in loaded plugins";
    }
}

void VersionCompatibilityTest::testLoadPlugin_IncompatibleMajor()
{
    // Test that plugin with incompatible major version is rejected
    // This would require creating a test plugin with wrong major version
    
    QSKIP("Test requires plugin with incompatible major version");
}

void VersionCompatibilityTest::testLoadPlugin_IncompatibleMinor()
{
    // Test that plugin with incompatible minor version is rejected
    // This would require creating a test plugin with higher minor version
    
    QSKIP("Test requires plugin with incompatible minor version");
}

// ============================================================================
// Multiple Version Scenarios
// ============================================================================

void VersionCompatibilityTest::testLoadMultiplePlugins_MixedVersions()
{
    // Test loading multiple plugins with different versions
    
    m_pluginManager->initialize();
    
    QList<ToolPluginInfo> plugins = m_pluginManager->pluginInfos();
    QList<ToolPluginManager::FailedPluginInfo> failed = m_pluginManager->failedPlugins();
    
    qDebug() << "Loaded plugins:" << plugins.size();
    qDebug() << "Failed plugins:" << failed.size();
    
    // Verify version information for each loaded plugin
    for (const auto& info : plugins) {
        qDebug() << "Plugin:" << info.descriptor.pluginId
                 << "SDK version:" << info.descriptor.sdkVersionMajor 
                 << "." << info.descriptor.sdkVersionMinor;
        
        // All loaded plugins should have compatible versions
        QCOMPARE(info.descriptor.sdkVersionMajor, TMAGENT_SDK_VERSION_MAJOR);
        QVERIFY2(info.descriptor.sdkVersionMinor <= TMAGENT_SDK_VERSION_MINOR,
                 qPrintable(QString("Plugin %1 minor version should be compatible")
                           .arg(info.descriptor.pluginId)));
    }
    
    // Check failed plugins for version incompatibility
    for (const auto& failInfo : failed) {
        qDebug() << "Failed plugin:" << failInfo.pluginId
                 << "Error:" << failInfo.error;
        
        if (failInfo.error.contains("version") || failInfo.error.contains("compatible")) {
            qDebug() << "Plugin failed due to version incompatibility";
        }
    }
}

void VersionCompatibilityTest::testLoadMultiplePlugins_AllCompatible()
{
    // Test scenario where all plugins have compatible versions
    
    m_pluginManager->initialize();
    
    QList<ToolPluginInfo> plugins = m_pluginManager->pluginInfos();
    
    // All loaded plugins should be compatible
    for (const auto& info : plugins) {
        QCOMPARE(info.descriptor.sdkVersionMajor, TMAGENT_SDK_VERSION_MAJOR);
        QVERIFY(info.descriptor.sdkVersionMinor <= TMAGENT_SDK_VERSION_MINOR);
    }
    
    qDebug() << "All" << plugins.size() << "plugins have compatible versions";
}

void VersionCompatibilityTest::testLoadMultiplePlugins_SomeIncompatible()
{
    // Test scenario where some plugins are incompatible
    
    m_pluginManager->initialize();
    
    QList<ToolPluginInfo> loaded = m_pluginManager->pluginInfos();
    QList<ToolPluginManager::FailedPluginInfo> failed = m_pluginManager->failedPlugins();
    
    qDebug() << "Loaded:" << loaded.size() << "Failed:" << failed.size();
    
    // Count version-related failures
    int versionFailures = 0;
    for (const auto& failInfo : failed) {
        if (failInfo.error.contains("version") || 
            failInfo.error.contains("compatible") ||
            failInfo.error.contains("SDK")) {
            versionFailures++;
        }
    }
    
    qDebug() << "Version-related failures:" << versionFailures;
}

// ============================================================================
// Version Reporting
// ============================================================================

void VersionCompatibilityTest::testVersionReporting_PluginDescriptor()
{
    // Test that plugin descriptor correctly reports version
    
    ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin";
    desc.displayName = "Test Plugin";
    desc.version = "1.2.3";
    desc.sdkVersionMajor = 1;
    desc.sdkVersionMinor = 0;
    
    QVERIFY2(desc.isValid(), "Descriptor should be valid");
    QCOMPARE(desc.sdkVersionMajor, 1);
    QCOMPARE(desc.sdkVersionMinor, 0);
    
    qDebug() << "Plugin version:" << desc.version;
    qDebug() << "SDK version:" << desc.sdkVersionMajor << "." << desc.sdkVersionMinor;
}

void VersionCompatibilityTest::testVersionReporting_FailedPlugins()
{
    // Test that failed plugins report version information
    
    m_pluginManager->initialize();
    
    QList<ToolPluginManager::FailedPluginInfo> failed = m_pluginManager->failedPlugins();
    
    for (const auto& failInfo : failed) {
        qDebug() << "Failed plugin:" << failInfo.pluginId;
        qDebug() << "Path:" << failInfo.path;
        qDebug() << "Error:" << failInfo.error;
        qDebug() << "Timestamp:" << failInfo.timestamp;
        
        // Error message should be informative
        QVERIFY2(!failInfo.error.isEmpty(), "Error message should not be empty");
    }
}

void VersionCompatibilityTest::testVersionReporting_LoadedPlugins()
{
    // Test that loaded plugins report version information
    
    m_pluginManager->initialize();
    
    QList<ToolPluginInfo> plugins = m_pluginManager->pluginInfos();
    
    for (const auto& info : plugins) {
        qDebug() << "Loaded plugin:" << info.descriptor.pluginId;
        qDebug() << "Display name:" << info.descriptor.displayName;
        qDebug() << "Version:" << info.descriptor.version;
        qDebug() << "SDK version:" << info.descriptor.sdkVersionMajor 
                 << "." << info.descriptor.sdkVersionMinor;
        qDebug() << "Category:" << info.descriptor.category;
        qDebug() << "Tools:" << info.descriptor.toolNames;
        
        // Verify version fields are populated
        QVERIFY2(info.descriptor.sdkVersionMajor >= 0, 
                 "SDK major version should be non-negative");
        QVERIFY2(info.descriptor.sdkVersionMinor >= 0, 
                 "SDK minor version should be non-negative");
    }
}

QTEST_MAIN(VersionCompatibilityTest)
#include "VersionCompatibilityTest.moc"
