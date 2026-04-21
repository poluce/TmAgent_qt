#ifndef VERSIONCOMPATIBILITYTEST_H
#define VERSIONCOMPATIBILITYTEST_H

#include <QObject>
#include <QtTest>

/**
 * @brief Integration tests for SDK version compatibility
 * 
 * Tests requirement 39.5:
 * - Plugins with mismatched major version are rejected
 * - Plugins with higher minor version are rejected
 * - Plugins with lower minor version are accepted
 */
class VersionCompatibilityTest : public QObject
{
    Q_OBJECT

private slots:
    // Test setup and cleanup
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Version compatibility tests
    void testCompatibility_ExactMatch();
    void testCompatibility_MajorMismatch_Higher();
    void testCompatibility_MajorMismatch_Lower();
    void testCompatibility_MinorHigher();
    void testCompatibility_MinorLower();
    void testCompatibility_MinorSame_PatchDifferent();
    
    // Edge cases
    void testCompatibility_ZeroVersion();
    void testCompatibility_LegacyPlugin();
    void testCompatibility_FutureVersion();
    
    // Real plugin loading tests
    void testLoadPlugin_CompatibleVersion();
    void testLoadPlugin_IncompatibleMajor();
    void testLoadPlugin_IncompatibleMinor();
    
    // Multiple version scenarios
    void testLoadMultiplePlugins_MixedVersions();
    void testLoadMultiplePlugins_AllCompatible();
    void testLoadMultiplePlugins_SomeIncompatible();
    
    // Version reporting
    void testVersionReporting_PluginDescriptor();
    void testVersionReporting_FailedPlugins();
    void testVersionReporting_LoadedPlugins();

private:
    class ToolPluginManager* m_pluginManager;
    QString m_testPluginDir;
};

#endif // VERSIONCOMPATIBILITYTEST_H
