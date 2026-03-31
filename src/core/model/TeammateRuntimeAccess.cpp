#include "TeammateRuntimeAccess.h"

void TeammateRuntimeAccess::setThreadId(Teammate* mate, const QString& threadId)
{
    if (mate)
        mate->setThreadId(threadId);
}

void TeammateRuntimeAccess::setActiveTurnId(Teammate* mate, const QString& turnId)
{
    if (mate)
        mate->setActiveTurnId(turnId);
}

void TeammateRuntimeAccess::setStatus(Teammate* mate, Teammate::Status status)
{
    if (mate)
        mate->setStatus(status);
}

void TeammateRuntimeAccess::setLastError(Teammate* mate, const QString& error)
{
    if (mate)
        mate->setLastError(error);
}

void TeammateRuntimeAccess::incrementTurnCount(Teammate* mate)
{
    if (mate)
        mate->incrementTurnCount();
}

void TeammateRuntimeAccess::touchLastActive(Teammate* mate)
{
    if (mate)
        mate->touchLastActive();
}
