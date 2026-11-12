#ifndef LIGHT_OCCLUSION_DOMAIN_H
#define LIGHT_OCCLUSION_DOMAIN_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_light_source.hpp>

#include <cstdint>

namespace IRSystem::detail {

struct LocalLightFrame {
    int maxRadius_ = 0;
    int propagationIterations_ = IRRender::kLightVolumePropagateIterations;
    std::uint32_t eligible_ = 0;
};

struct LocalLightSeed {
    IRMath::ivec3 trueOrigin_{};
    IRMath::ivec3 clampedCell_{};
    bool clampedAxis_[3] = {false, false, false};
    int boundaryDistance_ = 0;
    float residual_ = 0.0f;
    bool wouldSeed_ = false;

    bool clamped() const {
        return boundaryDistance_ > 0;
    }
};

struct LightOcclusionDomain {
    IRMath::ivec3 min_{};
    IRMath::ivec3 max_{};
    bool populated_ = false;

    bool contains(IRMath::ivec3 cell) const {
        return populated_ && cell.x >= min_.x && cell.x <= max_.x && cell.y >= min_.y &&
               cell.y <= max_.y && cell.z >= min_.z && cell.z <= max_.z;
    }
};

inline bool
lightNodeTargetsCanvas(const IREntity::ArchetypeNode *node, IREntity::EntityId currentCanvas) {
    const IREntity::EntityId parent =
        IREntity::getEntityManager().getParentEntityFromArchetype(node->type_);
    return parent == IREntity::kNullEntity || parent == currentCanvas;
}

inline LocalLightFrame localLightFrame(IREntity::EntityId currentCanvas) {
    LocalLightFrame frame;
    const auto include =
        IREntity::getArchetype<IRComponents::C_LightSource, IRComponents::C_WorldTransform>();
    for (auto *node : IREntity::queryArchetypeNodesSimple(include)) {
        if (!lightNodeTargetsCanvas(node, currentCanvas)) {
            continue;
        }
        auto &lights = IREntity::getComponentData<IRComponents::C_LightSource>(node);
        for (int i = 0; i < node->length_; ++i) {
            if (lights[i].type_ == IRComponents::LightType::DIRECTIONAL) {
                continue;
            }
            ++frame.eligible_;
            frame.maxRadius_ = IRMath::max(frame.maxRadius_, static_cast<int>(lights[i].radius_));
        }
    }
    frame.maxRadius_ = IRMath::min(frame.maxRadius_, IRRender::kLightVolumePropagateIterations);
    if (frame.maxRadius_ > 0) {
        frame.propagationIterations_ = frame.maxRadius_;
    }
    return frame;
}

inline LocalLightSeed localLightSeed(
    const IRComponents::C_WorldTransform &transform,
    IRMath::ivec3 volumeOrigin,
    int propagationIterations
) {
    LocalLightSeed seed;
    seed.trueOrigin_ = IRMath::roundVec3HalfUp(transform.translation_);
    const IRMath::ivec3 relative = seed.trueOrigin_ - volumeOrigin;
    IRMath::ivec3 clamped = relative;
    for (int axis = 0; axis < 3; ++axis) {
        const int value = IRMath::clamp(
            relative[axis],
            -IRComponents::kLightVolumeHalfExtent,
            IRComponents::kLightVolumeHalfExtent - 1
        );
        seed.boundaryDistance_ += IRMath::abs(relative[axis] - value);
        seed.clampedAxis_[axis] = value != relative[axis];
        clamped[axis] = value;
    }
    seed.clampedCell_ = volumeOrigin + clamped;
    seed.residual_ = 1.0f - static_cast<float>(seed.boundaryDistance_) /
                                static_cast<float>(propagationIterations);
    seed.wouldSeed_ = seed.residual_ > 0.0f;
    return seed;
}

inline void includeSeedDomain(
    LightOcclusionDomain &domain,
    const LocalLightSeed &seed,
    int propagationIterations,
    IRMath::ivec3 volumeOrigin
) {
    if (!seed.wouldSeed_) {
        return;
    }
    const int reach = seed.clamped() ? 2 * propagationIterations + 1 : propagationIterations + 1;
    const IRMath::ivec3 windowMin =
        volumeOrigin - IRMath::ivec3(IRComponents::kLightVolumeHalfExtent + 1);
    const IRMath::ivec3 windowMax =
        volumeOrigin + IRMath::ivec3(IRComponents::kLightVolumeHalfExtent);
    const IRMath::ivec3 seedMin = IRMath::max(seed.clampedCell_ - IRMath::ivec3(reach), windowMin);
    const IRMath::ivec3 seedMax = IRMath::min(seed.clampedCell_ + IRMath::ivec3(reach), windowMax);
    if (!domain.populated_) {
        domain.min_ = seedMin;
        domain.max_ = seedMax;
        domain.populated_ = true;
        return;
    }
    domain.min_ = IRMath::min(domain.min_, seedMin);
    domain.max_ = IRMath::max(domain.max_, seedMax);
}

inline LightOcclusionDomain
lightOcclusionDomain(IREntity::EntityId currentCanvas, IRMath::ivec3 volumeOrigin) {
    LightOcclusionDomain domain;
    const LocalLightFrame frame = localLightFrame(currentCanvas);
    if (frame.eligible_ == 0) {
        return domain;
    }
    const auto include =
        IREntity::getArchetype<IRComponents::C_LightSource, IRComponents::C_WorldTransform>();
    for (auto *node : IREntity::queryArchetypeNodesSimple(include)) {
        if (!lightNodeTargetsCanvas(node, currentCanvas)) {
            continue;
        }
        auto &lights = IREntity::getComponentData<IRComponents::C_LightSource>(node);
        auto &transforms = IREntity::getComponentData<IRComponents::C_WorldTransform>(node);
        for (int i = 0; i < node->length_; ++i) {
            if (lights[i].type_ == IRComponents::LightType::DIRECTIONAL) {
                continue;
            }
            includeSeedDomain(
                domain,
                localLightSeed(transforms[i], volumeOrigin, frame.propagationIterations_),
                frame.propagationIterations_,
                volumeOrigin
            );
        }
    }
    return domain;
}

} // namespace IRSystem::detail

#endif /* LIGHT_OCCLUSION_DOMAIN_H */
