/**
 * TmAgent 集成测试 - 简化版
 * 测试 Keychain 读写功能
 */
#include <QtTest>
#include <QDebug>

#include "core/utils/KeychainHelper.h"

class KeychainTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    
    void keychain_writeAndRead();
    void keychain_makeKeyRef();
    void keychain_parseKeyRef();

private:
    QString m_testKeychainId;
};

void KeychainTest::initTestCase()
{
    qDebug() << "=== Keychain 测试开始 ===";
    m_testKeychainId = "test:integration-test-key";
}

void KeychainTest::cleanupTestCase()
{
    qDebug() << "=== Keychain 测试结束 ===";
}

void KeychainTest::keychain_writeAndRead()
{
    qDebug() << "[Test] keychain_writeAndRead";
    
    const QString testSecret = "test-secret-12345";
    QString error;
    
    // 写入
    qDebug() << "  尝试写入 keychainId:" << m_testKeychainId;
    bool writeOk = KeychainHelper::writePasswordSync(m_testKeychainId, testSecret, &error);
    if (!writeOk) {
        qWarning() << "  Keychain 写入失败:" << error;
        QSKIP("Keychain 不可用，跳过测试");
    }
    qDebug() << "  写入成功";
    
    // 读取
    bool readOk = false;
    QString readSecret = KeychainHelper::readPasswordSync(m_testKeychainId, &readOk, &error);
    if (!readOk) {
        qWarning() << "  Keychain 读取失败:" << error;
    }
    QVERIFY2(readOk, qPrintable(error));
    QCOMPARE(readSecret, testSecret);
    
    qDebug() << "[Test] keychain_writeAndRead: PASS";
}

void KeychainTest::keychain_makeKeyRef()
{
    QString ref = KeychainHelper::makeKeyRef("provider:model-id");
    QCOMPARE(ref, QString("keychain:provider:model-id"));
    qDebug() << "[Test] keychain_makeKeyRef: PASS";
}

void KeychainTest::keychain_parseKeyRef()
{
    QString entryId;
    
    // 有效的 keychain 引用
    bool ok = KeychainHelper::parseKeyRef("keychain:provider:model-id", &entryId);
    QVERIFY(ok);
    QCOMPARE(entryId, QString("provider:model-id"));
    
    // 无效的引用
    ok = KeychainHelper::parseKeyRef("sk-plain-api-key", &entryId);
    QVERIFY(!ok);
    
    qDebug() << "[Test] keychain_parseKeyRef: PASS";
}

QTEST_MAIN(KeychainTest)
#include "tst_keychain.moc"
