#ifndef PLUGINLOADINGINTEGRATIONTEST_H
#define PLUGINLOADINGINTEGRATIONTEST_H

#include <QObject>
#include <QtTest>

/**
 * @brief Integration tests for plugin loading flow
 * 
 * Tests requirements 39.3 and 39.4:
 * - Plugin loading with new SDK interface
 * - Plugin loading with legacy interface
 * - Version compatibility checking
 * - Plugin metadata validation
 */
class PluginLoadingIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    // Test setup and cleanup
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Test cases for plugin loading
    void testLoadSdkPlugin();
    void testLoadLegacyPlugin();
    void testLoadMultiplePlugins();
    void testLoadPluginWithInvalidPath();
    void testLoadPluginWithMissingInterface();
    
    // Test cases for version compatibility
    void testVersionCompatibility_MatchingMajor();
    void testVersionCompatibility_MismatchedMajor();
    void testVersionCompatibility_HigherMinor();
    void testVersionCompatibility_LowerMinor();
    
    // Test cases for metadata validation
    void testPluginMetadata_Valid();
    void testPluginMetadata_EmptyPluginId();
    void testPluginMetadata_MissingToolNames();
    void testPluginMetadata_InvalidVersion();
    
    // Test cases for plugin discovery
    void testPluginDiscovery_ApplicationDirectory();
    void testPluginDiscovery_UserDirectory();
    void testPluginDiscovery_Priority();
    
    // Test cases for error handling
    void testLoadPlugin_CorruptedFile();
    void testLoadPlugin_MissingDependency();
    void testLoadPlugin_DuplicatePluginId();

private:
    QString m_testPluginDir;
    QString m_tempDir;
};

#endif // PLUGINLOADINGINTEGRATIONTEST_H
