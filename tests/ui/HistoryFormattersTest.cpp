#include <QtTest>

#include "HistoryFormattersTest.h"
#include "ui/workbench/HistoryFormatters.h"

void HistoryFormattersTest::toolLogWindowTitle_usesExpectedWording()
{
    QCOMPARE(HistoryFormatters::toolLogWindowTitle(), QStringLiteral("工具执行日志 - 原始事件"));
}

QTEST_MAIN(HistoryFormattersTest)
