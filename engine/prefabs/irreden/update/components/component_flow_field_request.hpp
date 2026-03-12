#ifndef COMPONENT_FLOW_FIELD_REQUEST_H
#define COMPONENT_FLOW_FIELD_REQUEST_H

#include <irreden/ir_math.hpp>

#include <algorithm>
#include <vector>

namespace IRComponents {

struct FlowFieldRequestEntry {
    int fieldId_{0};
    IRMath::ivec3 goalCell_{0, 0, 0};
    float agentClearance_{0.5f};
    bool dirty_{true};
};

struct C_FlowFieldRequest {
    std::vector<FlowFieldRequestEntry> requests_;
    int nextFieldId_{1};

    FlowFieldRequestEntry *findRequestByGoal(
        IRMath::ivec3 goalCell,
        float agentClearance
    ) {
        for (auto &request : requests_) {
            if (request.goalCell_ == goalCell &&
                request.agentClearance_ == agentClearance) {
                return &request;
            }
        }
        return nullptr;
    }

    FlowFieldRequestEntry *findRequestByFieldId(int fieldId) {
        for (auto &request : requests_) {
            if (request.fieldId_ == fieldId) return &request;
        }
        return nullptr;
    }

    int getOrCreateField(IRMath::ivec3 goalCell, float agentClearance) {
        if (auto *request = findRequestByGoal(goalCell, agentClearance)) {
            return request->fieldId_;
        }

        int fieldId = nextFieldId_++;
        requests_.push_back({fieldId, goalCell, agentClearance, true});
        return fieldId;
    }

    void markDirty(int fieldId) {
        if (auto *request = findRequestByFieldId(fieldId)) {
            request->dirty_ = true;
        }
    }

    void pruneInactive(const std::vector<int> &activeFieldIds) {
        requests_.erase(
            std::remove_if(
                requests_.begin(),
                requests_.end(),
                [&activeFieldIds](const FlowFieldRequestEntry &request) {
                    return std::find(
                               activeFieldIds.begin(),
                               activeFieldIds.end(),
                               request.fieldId_) == activeFieldIds.end();
                }
            ),
            requests_.end()
        );
    }
};

} // namespace IRComponents

#endif /* COMPONENT_FLOW_FIELD_REQUEST_H */
