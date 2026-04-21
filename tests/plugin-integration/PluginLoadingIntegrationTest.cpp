#include "PluginLoadingIntegrationTest.h"
#include "src/core/agent/PluginManager.h"
#include <tmagent/plugin/IToolPlugin.h>
#include <tmagent/types/PluginTypes.h>
#include <QDir>
#include <QTemporaryDir>
#include <QPluginLoader>

void PluginLoadingIntegrationTest::initTestCase()
{
    // Create temporary directory for test plugins
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    m_tempDir = tempDir.path();
    tempDir.setAutoRemove(false);
    
    m_testPluginDir = m_tempDir + "/plugins";
    QDir().mkpath(m_testPluginDir + "/tools");
    QDir().mkpath(m_testPluginDir + "/backends");
    
    qDebug() << "Test plugin directory:" << m_testPluginDir;
}

void PluginLoadingIntegrationTest::cleanupTestCase()
{
    // Clean up temporary directory
    QDir tempDir(m_tempDir);
    tempDir.removeRecursively();
}

void PluginLoadingIntegrationTest::init()
{
    // Setup before each test
}

void PluginLoadingIntegrationTest::cleanup()
{
    // Cleanup after each test
}

void PluginLoadingIntegrationTest::testLoadSdkPlugin()
{
    // Test loading a plugin that implements the SDK interface
    PluginManager manager;
    
    // Use the minimal tool plugin from examples
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
        QSKIP("Minimal tool plugin not built, skipping test");
    }
    
    bool loaded = manager.loadPlugin(libPath);
    QVERIFY2(loaded, "Failed to load SDK plugin");
    
    // Verify plugin is in loaded list
    QList<TmAgent::ToolPluginDescriptor> plugins = manager.listPlugins();
    QVERIFY(plugins.size() > 0);
    
    // Find our plugin
    bool found = false;
    for (const auto& desc : plugins) {
        if (desc.pluginId == "minimal_tool") {
            found = true;
            QCOMPARE(desc.displayName, QString("最小工具插件"));
            QCOMPARE(desc.category, QString("example"));
            QVERIFY(desc.toolNames.contains("echo"));
            QCOMPARE(desc.sdkVersionMajor, TMAGENT_SDK_VERSION_MAJOR);
            QCOMPARE(desc.sdkVersionMinor, TMAGENT_SDK_VERSION_MINOR);
            break;
        }
    }
    QVERIFY2(found, "Plugin not found in loaded plugins list");
}

void PluginLoadingIntegrationTest::testLoadLegacyPlugin()
{
    // Test loading a plugin that implements the legacy interface
    // This tests backward compatibility
    
    PluginManager manager;
    
    // Try to load an existing plugin that uses legacy interface
    // (if any exist in the project)
    QString pluginPath = QCoreApplication::applicationDirPath() + "/plugins/tools";
    
    QDir pluginDir(pluginPath);
    if (!pluginDir.exists()) {
        QSKIP("No legacy plugins directory found");
    }
    
    QStringList filters;
    #ifdef Q_OS_WIN
        filters << "*.dll";
    #elif defined(Q_OS_MAC)
        filters << "*.dylib";
    #else
        filters << "*.so";
    #endif
    
    QStringList plugins = pluginDir.entryList(filters, QDir::Files);
    if (plugins.isEmpty()) {
        QSKIP("No legacy plugins found");
    }
    
    // Try to load the first plugin
    QString libPath = pluginDir.absoluteFilePath(plugins.first());
    bool loaded = manager.loadPlugin(libPath);
    
    // Should either load successfully or fail gracefully
    if (loaded) {
        qDebug() << "Successfully loaded legacy plugin:" << plugins.first();
    } else {
        qDebug() << "Failed to load legacy plugin (expected if not compatible):" << plugins.first();
    }
}

void PluginLoadingIntegrationTest::testLoadMultiplePlugins()
{
    // Test loading multiple plugins simultaneously
    PluginManager manager;
    
    QString examplesPath = QCoreApplication::applicationDirPath() + 
                          "/../tmagent-plugin-sdk/examples";
    
    QStringList pluginPaths;
    pluginPaths << examplesPath + "/minimal-tool-plugin";
    pluginPaths << examplesPath + "/minimal-backend-plugin";
    
    int loadedCount = 0;
    for (const QString& path : pluginPaths) {
        #ifdef Q_OS_WIN
            QString libPath = path + "/" + QFileInfo(path).fileName() + ".dll";
        #elif defined(Q_OS_MAC)
            QString libPath = path + "/lib" + QFileInfo(path).fileName() + ".dylib";
        #else
            QString libPath = path + "/lib" + QFileInfo(path).fileName() + ".so";
        #endif
        
        if (QFile::exists(libPath)) {
            if (manager.loadPlugin(libPath)) {
                loadedCount++;
            }
        }
    }
    
    if (loadedCount == 0) {
        QSKIP("No example plugins built");
    }
    
    QVERIFY(loadedCount > 0);
    qDebug() << "Loaded" << loadedCount << "plugins";
}

void PluginLoadingIntegrationTest::testLoadPluginWithInvalidPath()
{
    // Test loading a plugin with invalid path
    PluginManager manager;
    
    bool loaded = manager.loadPlugin("/nonexistent/path/plugin.so");
    QVERIFY2(!loaded, "Should fail to load plugin with invalid path");
    
    // Verify plugin is in failed list
    QList<PluginManager::FailedPluginInfo> failed = manager.getFailedPlugins();
    bool found = false;
    for (const auto& info : failed) {
        if (info.path.contains("nonexistent")) {
            found = true;
            QVERIFY(!info.error.isEmpty());
            break;
        }
    }
    QVERIFY2(found, "Failed plugin should be in failed list");
}

void PluginLoadingIntegrationTest::testLoadPluginWithMissingInterface()
{
    // Test loading a library that doesn't implement plugin interface
    // This would be a regular Qt library, not a plugin
    
    PluginManager manager;
    
    // Try to load Qt Core library (not a plugin)
    #ifdef Q_OS_WIN
        QString qtCorePath = "Qt5Core.dll";
    #elif defined(Q_OS_MAC)
        QString qtCorePath = "QtCore.framework/QtCore";
    #else
        QString qtCorePath = "libQt5Core.so.5";
    #endif
    
    bool loaded = manager.loadPlugin(qtCorePath);
    QVERIFY2(!loaded, "Should fail to load non-plugin library");
}

void PluginLoadingIntegrationTest::testVersionCompatibility_MatchingMajor()
{
    // Test that plugins with matching major version are accepted
    PluginManager manager;
    
    TmAgent::ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin";
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    
    bool compatible = manager.isCompatible(desc);
    QVERIFY2(compatible, "Plugin with matching major version should be compatible");
}

void PluginLoadingIntegrationTest::testVersionCompatibility_MismatchedMajor()
{
    // Test that plugins with mismatched major version are rejected
    PluginManager manager;
    
    TmAgent::ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin";
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR + 1;
    desc.sdkVersionMinor = 0;
    
    bool compatible = manager.isCompatible(desc);
    QVERIFY2(!compatible, "Plugin with mismatched major version should be rejected");
}

void PluginLoadingIntegrationTest::testVersionCompatibility_HigherMinor()
{
    // Test that plugins with higher minor version are rejected
    PluginManager manager;
    
    TmAgent::ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin";
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR + 1;
    
    bool compatible = manager.isCompatible(desc);
    QVERIFY2(!compatible, "Plugin with higher minor version should be rejected");
}

void PluginLoadingIntegrationTest::testVersionCompatibility_LowerMinor()
{
    // Test that plugins with lower minor version are accepted (backward compatible)
    PluginManager manager;
    
    TmAgent::ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin";
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = (TMAGENT_SDK_VERSION_MINOR > 0) ? 
                          TMAGENT_SDK_VERSION_MINOR - 1 : 0;
    
    bool compatible = manager.isCompatible(desc);
    QVERIFY2(compatible, "Plugin with lower minor version should be accepted");
}

void PluginLoadingIntegrationTest::testPluginMetadata_Valid()
{
    // Test validation of valid plugin metadata
    TmAgent::ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin";
    desc.displayName = "Test Plugin";
    desc.version = "1.0.0";
    desc.description = "A test plugin";
    desc.category = "test";
    desc.toolNames = QStringList{"tool1", "tool2"};
    desc.sdkVersionMajor = 1;
    desc.sdkVersionMinor = 0;
    
    QVERIFY2(desc.isValid(), "Valid descriptor should pass validation");
}

void PluginLoadingIntegrationTest::testPluginMetadata_EmptyPluginId()
{
    // Test validation of plugin metadata with empty plugin ID
    TmAgent::ToolPluginDescriptor desc;
    desc.pluginId = "";
    desc.displayName = "Test Plugin";
    desc.version = "1.0.0";
    
    QVERIFY2(!desc.isValid(), "Descriptor with empty plugin ID should fail validation");
}

void PluginLoadingIntegrationTest::testPluginMetadata_MissingToolNames()
{
    // Test that plugin can have empty tool names (valid for backend plugins)
    TmAgent::ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin";
    desc.displayName = "Test Plugin";
    desc.version = "1.0.0";
    desc.toolNames = QStringList();  // Empty
    
    QVERIFY2(desc.isValid(), "Descriptor with empty tool names should still be valid");
}

void PluginLoadingIntegrationTest::testPluginMetadata_InvalidVersion()
{
    // Test plugin metadata with invalid version format
    // Note: Current implementation doesn't validate version format
    TmAgent::ToolPluginDescriptor desc;
    desc.pluginId = "test_plugin";
    desc.displayName = "Test Plugin";
    desc.version = "invalid.version";
    
    // Should still be valid as version format is not strictly validated
    QVERIFY(desc.isValid());
}

void PluginLoadingIntegrationTest::testPluginDiscovery_ApplicationDirectory()
{
    // Test plugin discovery in application directory
    PluginManager manager;
    
    QString appPluginDir = QCoreApplication::applicationDirPath() + "/plugins";
    QDir dir(appPluginDir);
    
    if (!dir.exists()) {
        QSKIP("Application plugins directory does not exist");
    }
    
    // Scan for plugins
    manager.scanPluginDirectory(appPluginDir);
    
    // Should have found some plugins
    QList<TmAgent::ToolPluginDescriptor> plugins = manager.listPlugins();
    qDebug() << "Found" << plugins.size() << "plugins in application directory";
}

void PluginLoadingIntegrationTest::testPluginDiscovery_UserDirectory()
{
    // Test plugin discovery in user directory
    PluginManager manager;
    
    QString userPluginDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + 
                           "/TmAgent/plugins";
    QDir dir;
    dir.mkpath(userPluginDir);
    
    QVERIFY(dir.exists(userPluginDir));
    
    // Scan for plugins (should work even if empty)
    manager.scanPluginDirectory(userPluginDir);
}

void PluginLoadingIntegrationTest::testPluginDiscovery_Priority()
{
    // Test that plugins are loaded according to priority
    // Application plugins > User plugins > System plugins
    
    PluginManager manager;
    
    // This test would require setting up duplicate plugins in different directories
    // For now, just verify the search order is correct
    
    QStringList searchPaths = manager.getPluginSearchPaths();
    QVERIFY(searchPaths.size() >= 2);
    
    // First path should be application directory
    QVERIFY(searchPaths[0].contains("plugins"));
    
    qDebug() << "Plugin search paths:" << searchPaths;
}

void PluginLoadingIntegrationTest::testLoadPlugin_CorruptedFile()
{
    // Test loading a corrupted plugin file
    PluginManager manager;
    
    // Create a fake plugin file with invalid content
    QString fakePath = m_testPluginDir + "/corrupted.so";
    QFile file(fakePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("This is not a valid plugin file");
    file.close();
    
    bool loaded = manager.loadPlugin(fakePath);
    QVERIFY2(!loaded, "Should fail to load corrupted plugin file");
    
    // Verify error is recorded
    QList<PluginManager::FailedPluginInfo> failed = manager.getFailedPlugins();
    bool found = false;
    for (const auto& info : failed) {
        if (info.path == fakePath) {
            found = true;
            QVERIFY(!info.error.isEmpty());
            qDebug() << "Corrupted plugin error:" << info.error;
            break;
        }
    }
    QVERIFY2(found, "Corrupted plugin should be in failed list");
}

void PluginLoadingIntegrationTest::testLoadPlugin_MissingDependency()
{
    // Test loading a plugin with missing dependencies
    // This is difficult to test without creating a plugin with missing deps
    // For now, just document the expected behavior
    
    QSKIP("Test requires plugin with missing dependencies");
}

void PluginLoadingIntegrationTest::testLoadPlugin_DuplicatePluginId()
{
    // Test loading two plugins with the same plugin ID
    // The second one should be rejected or override the first
    
    PluginManager manager;
    
    // This test would require creating two plugins with same ID
    // For now, just verify the behavior is defined
    
    QSKIP("Test requires two plugins with same ID");
}

QTEST_MAIN(PluginLoadingIntegrationTest)
#include "PluginLoadingIntegrationTest.moc"
