#ifndef IR_VOXEL_EDITOR_MOTION_PREVIEW_H
#define IR_VOXEL_EDITOR_MOTION_PREVIEW_H

#include "editor_picking.hpp"
#include "entity_scene.hpp"

#include <irreden/common/components/component_auto_spin.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace IRVoxelEditor {

// Samples the per-update C_AutoSpin contract from an immutable rest pose.
// The preview tick is explicit so pausing, seeking, and resetting are repeatable.
inline IRComponents::C_LocalTransform sampleSpinPreview(
    IRComponents::C_LocalTransform rest,
    const std::optional<IRComponents::C_AutoSpin> &spin,
    int tick
) {
    if (!spin || tick <= 0 || spin->radiansPerFrame_ == 0.0f ||
        !IRMath::isFinite(spin->radiansPerFrame_) || !IRMath::isFinite(spin->axis_.x) ||
        !IRMath::isFinite(spin->axis_.y) || !IRMath::isFinite(spin->axis_.z) ||
        !IRMath::isFinite(IRMath::dot(spin->axis_, spin->axis_)) ||
        IRMath::dot(spin->axis_, spin->axis_) == 0.0f)
        return rest;
    const float angle = spin->radiansPerFrame_ * static_cast<float>(tick);
    if (!IRMath::isFinite(angle))
        return rest;
    rest.rotation_ = IRMath::quatMul(
        IRMath::quatAxisAngle(spin->axis_, IRMath::wrapAnglePi(angle)),
        rest.rotation_
    );
    return rest;
}

// Owns rendering copies only. Module systems and component factories never run
// here, and no authored entity is modified or included in this ownership tree.
class MotionPreview {
  public:
    bool active() const {
        return m_root != IREntity::kNullEntity;
    }
    int tick() const {
        return m_tick;
    }
    IREntity::EntityId root() const {
        return m_root;
    }

    IREntity::EntityId part(const std::string &id) const {
        const auto found = m_parts.find(id);
        return found == m_parts.end() ? IREntity::kNullEntity : found->second;
    }

    EntitySceneResult begin(const EntityScene &source, IRMath::vec3 stageOffset) {
        if (active())
            return {false, "A motion preview is already open."};
        if (!source.active() || source.parts().empty())
            return {false, "Create an assembly before opening motion preview."};
        for (const auto &part : source.parts()) {
            if (!part.parentId_.empty() && !source.findPart(part.parentId_))
                return {false, "The assembly contains a missing parent."};
        }
        auto rootRest = IREntity::getComponent<IRComponents::C_LocalTransform>(source.root());
        rootRest.translation_ += stageOffset;
        const auto rootSpin = spinFor(source.root());
        m_root = IREntity::createEntity(rootRest, IRComponents::C_EditorReference{});
        m_poses.push_back({m_root, rootRest, rootSpin});
        for (const auto &part : source.parts()) {
            const auto snapshot = source.snapshotPart(part.entity_);
            if (!snapshot) {
                stop();
                return {false, "An assembly part is unavailable."};
            }
            const auto spin = spinFor(part.entity_);
            std::optional<IRComponents::C_LodTierOverride> tier;
            if (const auto value =
                    IREntity::getComponentOptional<IRComponents::C_LodTierOverride>(part.entity_))
                tier = **value;
            IREntity::EntityId entity;
            if (part.kind_ == EditorPartKind::SHAPE) {
                entity = IREntity::createEntity(
                    snapshot->transform_,
                    std::get<IRComponents::C_ShapeDescriptor>(snapshot->payload_),
                    IRComponents::C_RotationMode{part.mode_},
                    IRComponents::C_EditorReference{}
                );
            } else {
                auto set = IRPrefab::DenseVoxel::toComponent(
                    std::get<IRAsset::DenseVoxelSet>(snapshot->payload_),
                    snapshot->voxelAnchor_
                );
                set.lodMin_ = part.lodMin_;
                set.lodMax_ = part.lodMax_;
                entity = IREntity::createEntity(
                    snapshot->transform_,
                    std::move(set),
                    IRComponents::C_RotationMode{part.mode_},
                    IRComponents::C_EditorReference{}
                );
                const auto records =
                    IREntity::getComponent<IRComponents::C_VoxelSetNew>(part.entity_)
                        .authoredRecords();
                auto &copy = IREntity::getComponent<IRComponents::C_VoxelSetNew>(entity);
                auto destination = copy.mutableAuthoredRecords();
                if (destination.size() != records.size()) {
                    IREntity::destroyEntity(entity);
                    stop();
                    return {false, "Not enough voxel storage to open motion preview."};
                }
                std::copy(records.begin(), records.end(), destination.begin());
                copy.resyncAfterAuthoredEdits();
            }
            IREntity::setParent(entity, m_root);
            if (tier)
                IREntity::setComponent(entity, *tier);
            m_parts.emplace(part.id_, entity);
            m_poses.push_back({entity, snapshot->transform_, spin});
            if (part.mode_ != IRComponents::RotationMode::GRID) {
                const auto mode = part.mode_;
                const auto canvasSize = part.canvasSize_;
                IREntity::getEntityManager().stageStructuralChange([entity, mode, canvasSize]() {
                    if (IREntity::entityExists(entity) &&
                        !IREntity::getEntityManager().isMarkedForDeletion(entity))
                        IRPrefab::RotationMode::setMode(entity, mode, {}, canvasSize);
                });
            }
        }
        for (const auto &part : source.parts()) {
            if (!part.parentId_.empty())
                IREntity::setParent(m_parts.at(part.id_), m_parts.at(part.parentId_));
        }
        m_tick = 0;
        return {true, {}};
    }

    void seek(int tick) {
        if (!active())
            return;
        m_tick = IRMath::max(tick, 0);
        for (const auto &pose : m_poses) {
            if (IREntity::entityExists(pose.entity_))
                IREntity::getComponent<IRComponents::C_LocalTransform>(pose.entity_) =
                    sampleSpinPreview(pose.rest_, pose.spin_, m_tick);
        }
    }

    void stop() {
        if (active() && IREntity::entityExists(m_root))
            IREntity::destroyTree(m_root);
        m_root = IREntity::kNullEntity;
        m_tick = 0;
        m_parts.clear();
        m_poses.clear();
    }

  private:
    struct Pose {
        IREntity::EntityId entity_;
        IRComponents::C_LocalTransform rest_;
        std::optional<IRComponents::C_AutoSpin> spin_;
    };

    static std::optional<IRComponents::C_AutoSpin> spinFor(IREntity::EntityId entity) {
        const auto spin = IREntity::getComponentOptional<IRComponents::C_AutoSpin>(entity);
        return spin ? std::optional<IRComponents::C_AutoSpin>{**spin} : std::nullopt;
    }

    IREntity::EntityId m_root = IREntity::kNullEntity;
    int m_tick = 0;
    std::unordered_map<std::string, IREntity::EntityId> m_parts;
    std::vector<Pose> m_poses;
};

} // namespace IRVoxelEditor

#endif
