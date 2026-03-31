#ifndef TEAMMATERUNTIMEACCESS_H
#define TEAMMATERUNTIMEACCESS_H

#include "Teammate.h"

class TeammateRuntimeAccess {
public:
    static void setThreadId(Teammate* mate, const QString& threadId);
    static void setActiveTurnId(Teammate* mate, const QString& turnId);
    static void setStatus(Teammate* mate, Teammate::Status status);
    static void setLastError(Teammate* mate, const QString& error);
    static void incrementTurnCount(Teammate* mate);
    static void touchLastActive(Teammate* mate);
};

#endif // TEAMMATERUNTIMEACCESS_H
