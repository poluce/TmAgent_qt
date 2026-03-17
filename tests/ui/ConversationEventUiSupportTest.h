#ifndef CONVERSATIONEVENTUISUPPORTTEST_H
#define CONVERSATIONEVENTUISUPPORTTEST_H

#include <QObject>

class ConversationEventUiSupportTest : public QObject {
    Q_OBJECT

private slots:
    void parseStreamDelta_mapsBasicFields();
    void parseToolEvent_buildsStructuredToolEvent();
    void parseTurnRejected_buildsUserFacingOverflowMessage();
    void parseMemoryIndexError_marksRefreshAndDisplayError();
    void parseUnknown_returnsIgnore();
};

#endif // CONVERSATIONEVENTUISUPPORTTEST_H
