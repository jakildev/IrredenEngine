#ifndef COMPONENT_FLOW_FIELD_AGENT_H
#define COMPONENT_FLOW_FIELD_AGENT_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

struct C_FlowFieldAgent {
    int fieldId_{0};
    IRMath::ivec3 goalCell_{0, 0, 0};
    IRMath::ivec3 groupCenterCell_{0, 0, 0};
    IRMath::vec3 immediateDirection_{0.0f};
    bool waitingForField_{true};
    bool active_{false};
    bool exactGoal_{false};
    int noProgressFrames_{0};
    int unitBlockedFrames_{0};
    float bestGoalDist_{-1.0f};

    C_FlowFieldAgent() = default;

    C_FlowFieldAgent(
        int fieldId,
        IRMath::ivec3 goalCell,
        IRMath::vec3 immediateDirection,
        bool exactGoal = false
    )
        : fieldId_{fieldId}
        , goalCell_{goalCell}
        , groupCenterCell_{goalCell}
        , immediateDirection_{immediateDirection}
        , waitingForField_{true}
        , active_{true}
        , exactGoal_{exactGoal} {}

    C_FlowFieldAgent(
        int fieldId,
        IRMath::ivec3 goalCell,
        IRMath::ivec3 groupCenterCell,
        IRMath::vec3 immediateDirection,
        bool exactGoal = false
    )
        : fieldId_{fieldId}
        , goalCell_{goalCell}
        , groupCenterCell_{groupCenterCell}
        , immediateDirection_{immediateDirection}
        , waitingForField_{true}
        , active_{true}
        , exactGoal_{exactGoal} {}

    bool hasField() const {
        return active_ && fieldId_ != 0;
    }

    void clear() {
        fieldId_ = 0;
        goalCell_ = IRMath::ivec3(0, 0, 0);
        groupCenterCell_ = IRMath::ivec3(0, 0, 0);
        immediateDirection_ = IRMath::vec3(0.0f);
        waitingForField_ = true;
        active_ = false;
        exactGoal_ = false;
        noProgressFrames_ = 0;
        unitBlockedFrames_ = 0;
        bestGoalDist_ = -1.0f;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_FLOW_FIELD_AGENT_H */
