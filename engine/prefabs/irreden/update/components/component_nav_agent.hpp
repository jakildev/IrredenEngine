#ifndef COMPONENT_NAV_AGENT_H
#define COMPONENT_NAV_AGENT_H

#include <irreden/ir_math.hpp>

#include <vector>

namespace IRComponents {

struct C_NavAgent {
    std::vector<IRMath::ivec3> path_;
    int pathIndex_;
    float moveSpeed_;
    float agentClearance_;
    float planningClearanceMultiplier_;
    bool partialPath_;
    IRMath::ivec3 finalTarget_;
    int stuckFrames_{0};

    C_NavAgent()
        : pathIndex_{0}
        , moveSpeed_{40.0f}
        , agentClearance_{0.5f}
        , planningClearanceMultiplier_{1.0f}
        , partialPath_{false}
        , finalTarget_{0, 0, 0} {}

    explicit C_NavAgent(float agentClearance)
        : pathIndex_{0}
        , moveSpeed_{40.0f}
        , agentClearance_{agentClearance}
        , planningClearanceMultiplier_{1.0f}
        , partialPath_{false}
        , finalTarget_{0, 0, 0} {}

    C_NavAgent(float agentClearance, float planningClearanceMultiplier)
        : pathIndex_{0}
        , moveSpeed_{40.0f}
        , agentClearance_{agentClearance}
        , planningClearanceMultiplier_{planningClearanceMultiplier}
        , partialPath_{false}
        , finalTarget_{0, 0, 0} {}

    float planningClearance() const {
        return agentClearance_ * planningClearanceMultiplier_;
    }

    bool hasPath() const {
        return pathIndex_ >= 0 && pathIndex_ < static_cast<int>(path_.size());
    }

    void clearPath() {
        path_.clear();
        pathIndex_ = 0;
        partialPath_ = false;
        stuckFrames_ = 0;
    }

    void setPath(const std::vector<IRMath::ivec3> &path, IRMath::ivec3 goal) {
        path_ = path;
        pathIndex_ = 0;
        finalTarget_ = goal;
        partialPath_ = !path.empty() && path.back() != goal;
        stuckFrames_ = 0;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_NAV_AGENT_H */
