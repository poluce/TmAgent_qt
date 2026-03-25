#ifndef HEARTBEATDECISIONENGINE_H
#define HEARTBEATDECISIONENGINE_H

#include "HeartbeatRuntimeState.h"
#include "HeartbeatTypes.h"

class HeartbeatDecisionEngine {
public:
    HeartbeatCycleResult evaluate(const HeartbeatPolicy& policy,
                                  const HeartbeatTicket& ticket,
                                  const HeartbeatSnapshot& snapshot,
                                  const HeartbeatRuntimeState& runtimeState) const;
};

#endif // HEARTBEATDECISIONENGINE_H
