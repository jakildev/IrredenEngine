#ifndef COMPONENT_FLOW_FIELD_H
#define COMPONENT_FLOW_FIELD_H

#include <irreden/ir_math.hpp>
#include <irreden/math/nav_types.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

namespace IRComponents {

struct FlowFieldDirection {
    int8_t x_{0};
    int8_t y_{0};
    int8_t z_{0};

    FlowFieldDirection() = default;

    FlowFieldDirection(int x, int y, int z)
        : x_{static_cast<int8_t>(x)}
        , y_{static_cast<int8_t>(y)}
        , z_{static_cast<int8_t>(z)} {}

    bool isZero() const {
        return x_ == 0 && y_ == 0 && z_ == 0;
    }

    IRMath::ivec3 toDelta() const {
        return IRMath::ivec3(
            static_cast<int>(x_),
            static_cast<int>(y_),
            static_cast<int>(z_)
        );
    }
};

struct FlowFieldFrontierNode {
    float cost_{0.0f};
    IRMath::ivec3 cell_{0, 0, 0};

    bool operator>(const FlowFieldFrontierNode &other) const {
        return cost_ > other.cost_;
    }
};

struct FlowFieldChunkData {
    std::vector<float> costs_;
    std::vector<FlowFieldDirection> directions_;

    void resize(IRMath::ivec3 chunkSize) {
        size_t total = static_cast<size_t>(chunkSize.x * chunkSize.y * chunkSize.z);
        costs_.assign(total, std::numeric_limits<float>::infinity());
        directions_.assign(total, FlowFieldDirection{});
    }
};

struct FlowFieldState {
    int fieldId_{0};
    IRMath::ivec3 goalCell_{0, 0, 0};
    float agentClearance_{0.5f};
    bool dirty_{true};
    bool complete_{false};
    std::priority_queue<
        FlowFieldFrontierNode,
        std::vector<FlowFieldFrontierNode>,
        std::greater<FlowFieldFrontierNode>> frontier_;
    std::unordered_map<int64_t, FlowFieldChunkData> chunks_;

    FlowFieldState() = default;

    FlowFieldState(int fieldId, IRMath::ivec3 goalCell, float agentClearance)
        : fieldId_{fieldId}
        , goalCell_{goalCell}
        , agentClearance_{agentClearance} {}
};

struct C_FlowField {
    std::vector<FlowFieldState> fields_;
    int maxExpansionsPerFrame_{4096};

    FlowFieldState *findField(int fieldId) {
        for (auto &field : fields_) {
            if (field.fieldId_ == fieldId) return &field;
        }
        return nullptr;
    }

    const FlowFieldState *findField(int fieldId) const {
        for (const auto &field : fields_) {
            if (field.fieldId_ == fieldId) return &field;
        }
        return nullptr;
    }

    FlowFieldState &ensureField(int fieldId, IRMath::ivec3 goalCell, float agentClearance) {
        FlowFieldState *field = findField(fieldId);
        if (field) {
            if (field->goalCell_ != goalCell || field->agentClearance_ != agentClearance) {
                field->goalCell_ = goalCell;
                field->agentClearance_ = agentClearance;
                field->dirty_ = true;
                field->complete_ = false;
                field->chunks_.clear();
                field->frontier_ = {};
            }
            return *field;
        }

        fields_.emplace_back(fieldId, goalCell, agentClearance);
        return fields_.back();
    }

    void pruneInactive(const std::vector<int> &activeFieldIds) {
        fields_.erase(
            std::remove_if(
                fields_.begin(),
                fields_.end(),
                [&activeFieldIds](const FlowFieldState &field) {
                    return std::find(
                               activeFieldIds.begin(),
                               activeFieldIds.end(),
                               field.fieldId_) == activeFieldIds.end();
                }
            ),
            fields_.end()
        );
    }

    FlowFieldChunkData &ensureChunk(
        FlowFieldState &field,
        IRMath::ChunkCoord chunkCoord,
        IRMath::ivec3 chunkSize
    ) {
        int64_t key = IRMath::chunkCoordToKey(chunkCoord);
        auto it = field.chunks_.find(key);
        if (it != field.chunks_.end()) return it->second;

        FlowFieldChunkData chunk;
        chunk.resize(chunkSize);
        auto inserted = field.chunks_.emplace(key, std::move(chunk));
        return inserted.first->second;
    }

    const FlowFieldChunkData *getChunk(
        const FlowFieldState &field,
        IRMath::ChunkCoord chunkCoord
    ) const {
        auto it = field.chunks_.find(IRMath::chunkCoordToKey(chunkCoord));
        return (it != field.chunks_.end()) ? &it->second : nullptr;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_FLOW_FIELD_H */
