#include <QtTest>

#include "ConversationEventUiSupportTest.h"
#include "ui/support/ConversationEventUiSupport.h"

using namespace ConversationEventUiSupport;

void ConversationEventUiSupportTest::parseStreamDelta_mapsBasicFields()
{
    const QJsonObject event {
        { QStringLiteral("type"), QStringLiteral("turn_delta") },
        { QStringLiteral("sessionId"), QStringLiteral("session-1") },
        { QStringLiteral("delta"), QStringLiteral("hello") }
    };

    const ParsedEvent parsed = parseConversationEvent(event);
    QCOMPARE(parsed.kind, EventKind::StreamDelta);
    QCOMPARE(parsed.sessionId, QStringLiteral("session-1"));
    QCOMPARE(parsed.delta, QStringLiteral("hello"));
}

void ConversationEventUiSupportTest::parseToolEvent_buildsStructuredToolEvent()
{
    const QJsonObject event {
        { QStringLiteral("type"), QStringLiteral("turn_tool_event") },
        { QStringLiteral("sessionId"), QStringLiteral("session-2") },
        { QStringLiteral("toolEvent"), QJsonObject {
              { QStringLiteral("toolName"), QStringLiteral("delegate_task") },
              { QStringLiteral("toolId"), QStringLiteral("tool-1") },
              { QStringLiteral("status"), QStringLiteral("completed") },
              { QStringLiteral("success"), false },
              { QStringLiteral("rawResult"), QStringLiteral("failed") }
          } }
    };

    const ParsedEvent parsed = parseConversationEvent(event);
    QCOMPARE(parsed.kind, EventKind::ToolEvent);
    QCOMPARE(parsed.toolEvent.toolName, QStringLiteral("delegate_task"));
    QCOMPARE(parsed.toolEvent.toolId, QStringLiteral("tool-1"));
    QCOMPARE(parsed.toolEvent.status, QStringLiteral("completed"));
    QCOMPARE(parsed.toolEvent.success, false);
    QCOMPARE(parsed.toolEvent.rawResult, QStringLiteral("failed"));
}

void ConversationEventUiSupportTest::parseTurnRejected_buildsUserFacingOverflowMessage()
{
    const QJsonObject event {
        { QStringLiteral("type"), QStringLiteral("turn_rejected") },
        { QStringLiteral("sessionId"), QStringLiteral("session-3") },
        { QStringLiteral("reason"), QStringLiteral("queue_overflow") },
        { QStringLiteral("queueDepth"), 11 },
        { QStringLiteral("queueHardLimit"), 20 }
    };

    const ParsedEvent parsed = parseConversationEvent(event);
    QCOMPARE(parsed.kind, EventKind::TurnRejected);
    QCOMPARE(parsed.displayError, QStringLiteral("队列已满（11/20），请稍后重试。"));
}

void ConversationEventUiSupportTest::parseMemoryIndexError_marksRefreshAndDisplayError()
{
    const QJsonObject event {
        { QStringLiteral("type"), QStringLiteral("memory.index.error") },
        { QStringLiteral("sessionId"), QStringLiteral("session-4") },
        { QStringLiteral("error"), QStringLiteral("index unavailable") }
    };

    const ParsedEvent parsed = parseConversationEvent(event);
    QCOMPARE(parsed.kind, EventKind::MemoryNotice);
    QCOMPARE(parsed.refreshHistory, true);
    QCOMPARE(parsed.displayError, QStringLiteral("记忆索引更新失败: index unavailable"));
}

void ConversationEventUiSupportTest::parseUnknown_returnsIgnore()
{
    const QJsonObject event {
        { QStringLiteral("type"), QStringLiteral("something_else") },
        { QStringLiteral("sessionId"), QStringLiteral("session-5") }
    };

    const ParsedEvent parsed = parseConversationEvent(event);
    QCOMPARE(parsed.kind, EventKind::Ignore);
}

QTEST_MAIN(ConversationEventUiSupportTest)
