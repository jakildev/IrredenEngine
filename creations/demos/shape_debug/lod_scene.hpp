#ifndef LOD_SCENE_H
#define LOD_SCENE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/script/prefab_api.hpp>
#include <irreden/update/components/component_prefab_parts.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <array>

namespace IRShapeDebug {

class LodScene {
  public:
    static constexpr IRVideo::AutoScreenshotShot shots_[] = {
        {1.0f, IRMath::vec2(0), 0.0f, "lod_scene_coarse"},
        {4.0f, IRMath::vec2(0), IRMath::kPi * 0.25f, "lod_scene_medium_yaw45"},
        {16.0f, IRMath::vec2(0), IRMath::kHalfPi, "lod_scene_fine_yaw90"},
        {4.0f, IRMath::vec2(0), IRMath::kPi, "lod_scene_return_yaw180"},
        {1.0f, IRMath::vec2(0), IRMath::kPi * 1.5f, "lod_scene_coarse_yaw270"},
        {1.0f, IRMath::vec2(0), 0.0f, "lod_scene_restored"},
    };
    static_assert(
        shots_[0].zoom_ == shots_[std::size(shots_) - 1].zoom_ &&
            shots_[0].yawRadians_ == shots_[std::size(shots_) - 1].yawRadians_ &&
            shots_[0].cameraIso_.x == shots_[std::size(shots_) - 1].cameraIso_.x &&
            shots_[0].cameraIso_.y == shots_[std::size(shots_) - 1].cameraIso_.y,
        "The restored LOD capture must return to the starting camera pose"
    );

    void initialize(IRScript::LuaScript &lua) {
        using namespace IRComponents;
        using namespace IRMath;
        IRPrefab::Prefab::registerPrefab("lod_scene_lazy", "assets/prefabs/flower.prefab.lua");
        IRPrefab::Prefab::registerPrefab("lod_scene_resident", "configs/lod_resident.prefab.lua");
        for (std::size_t i = 0; i < m_roots.size(); ++i) {
            const bool resident = i >= m_roots.size() / 2;
            const vec3 position{
                static_cast<float>(i % 3) * 12.0f - 12.0f,
                resident ? 8.0f : -8.0f,
                0.0f
            };
            const auto spawned = IRPrefab::Prefab::spawnPrefab(
                lua,
                resident ? "lod_scene_resident" : "lod_scene_lazy",
                position
            );
            IR_ASSERT(spawned.entity_ != IREntity::kNullEntity, "LOD scene prefab must spawn");
            m_roots[i] = spawned.entity_;
        }
        IREntity::setComponent(m_roots.back(), C_LodTierOverride{IRRender::LodLevel::LOD_0});
        IREntity::createEntity(
            C_LocalTransform{vec3(0.0f, 2.0f, 5.0f)},
            C_LightBlocker{false, false, 0.0f},
            C_ShapeDescriptor{
                SDF::ShapeType::BOX,
                vec4(52.0f, 44.0f, 2.0f, 0.0f),
                Color{120, 125, 135, 255}
            }
        );
        for (int step = 0; step < 3; ++step) {
            IREntity::createEntity(
                C_LocalTransform{vec3(-24.0f + 8.0f * step, 18.0f, 3.0f - step)},
                C_VoxelSetNew{
                    ivec3(8, 4, step + 1),
                    Color{115, 145, 105, 255},
                    EntityAnchor::CORNER
                }
            );
        }
        m_denseRoot =
            IREntity::createEntity(C_LocalTransform{vec3(16, 18, -2)}, C_WorldTransform{});
        for (int variant = 0; variant < 2; ++variant) {
            const auto entity = IREntity::createEntity(
                C_LocalTransform{vec3(0)},
                C_WorldTransform{},
                C_VoxelSetNew{
                    ivec3(6),
                    variant == 0 ? Color{215, 155, 80, 255} : Color{80, 165, 215, 255},
                    EntityAnchor::CENTER
                }
            );
            IREntity::setParent(entity, m_denseRoot);
            auto &set = IREntity::getComponent<C_VoxelSetNew>(entity);
            set.lodMax_ = variant == 0 ? IRRender::LodLevel::LOD_3 : IRRender::LodLevel::LOD_0;
            set.lodMin_ = variant == 0 ? IRRender::LodLevel::LOD_4 : IRRender::LodLevel::LOD_2;
            if (variant == 1) {
                set.carve([](vec3 position) { return length(position) > 3.0f; });
            }
            m_dense[variant] = entity;
        }
        IR_LOG_INFO(
            "LOD scene: six prefab roots share loaded assets; DENSE variants own distinct "
            "pool spans. Tiers follow zoom, not world distance; automatic captures move parents."
        );
        setPhase(0);
    }

    void capture(int shotIndex) {
        using namespace IRComponents;
        IR_ASSERT(
            shotIndex >= 0 && shotIndex < static_cast<int>(std::size(shots_)),
            "LOD capture index must belong to the LOD shot table"
        );
        const auto tier = IRRender::computeLodLevel(shots_[shotIndex].zoom_);
        std::size_t liveParts = 0;
        std::size_t visibleParts = 0;
        std::size_t retainedOutsideBand = 0;
        for (std::size_t rootIndex = 0; rootIndex < m_roots.size(); ++rootIndex) {
            const auto root = m_roots[rootIndex];
            const bool resident = rootIndex >= m_roots.size() / 2;
            const auto &parts = IREntity::getComponent<C_PrefabParts>(root);
            const auto rootTier = root == m_roots.back() ? IRRender::LodLevel::LOD_0 : tier;
            IR_ASSERT(parts.tier_ == rootTier, "LOD scene parts must settle to capture tier");
            IR_ASSERT(
                parts.slots_.size() == m_residentParts[rootIndex].size(),
                "LOD flower must have silhouette, petals and stamen slots"
            );
            for (std::size_t slotIndex = 0; slotIndex < parts.slots_.size(); ++slotIndex) {
                const auto &slot = parts.slots_[slotIndex];
                const bool live = slot.entity_ != IREntity::kNullEntity;
                const bool visible = slot.inBand(rootTier);
                IR_ASSERT(slot.resident_ == resident, "LOD row must use its authored residency");
                IR_ASSERT(!visible || live, "Every visible LOD part must exist");
                if (resident) {
                    auto &retained = m_residentParts[rootIndex][slotIndex];
                    // Residency retains a part after its first in-band use; it does not
                    // eagerly materialize parts whose band has never been visited.
                    if (retained == IREntity::kNullEntity && live) {
                        retained = slot.entity_;
                    }
                    IR_ASSERT(
                        retained == slot.entity_,
                        "Resident parts must retain entity identity after first use"
                    );
                } else {
                    IR_ASSERT(live == visible, "Lazy parts must exist exactly in their band");
                }
                if (!live) {
                    continue;
                }
                IR_ASSERT(
                    IREntity::entityExists(slot.entity_),
                    "Materialized prefab parts must refer to live entities"
                );
                ++liveParts;
                visibleParts += visible ? 1 : 0;
                retainedOutsideBand += visible ? 0 : 1;
                verifyAttachment(slot.entity_, root);
            }
        }
        if (shotIndex > 0) {
            IR_ASSERT(
                retainedOutsideBand > 0,
                "Visited LOD bands must leave hidden resident parts"
            );
        }
        const auto &coarse = IREntity::getComponent<C_VoxelSetNew>(m_dense[0]);
        const auto &fine = IREntity::getComponent<C_VoxelSetNew>(m_dense[1]);
        IR_ASSERT(coarse.renders() != fine.renders(), "Exactly one DENSE LOD variant must render");
        IR_ASSERT(
            coarse.renders() == (tier >= IRRender::LodLevel::LOD_3),
            "DENSE LOD selection must match the settled camera tier"
        );
        IR_ASSERT(
            coarse.numVoxels_ > 0 && fine.numVoxels_ > 0 &&
                (coarse.voxelStartIdx_ + coarse.numVoxels_ <= fine.voxelStartIdx_ ||
                 fine.voxelStartIdx_ + fine.numVoxels_ <= coarse.voxelStartIdx_),
            "DENSE variants must retain nonempty, disjoint voxel pool spans"
        );
        for (const auto child : m_dense) {
            verifyAttachment(child, m_denseRoot);
        }
        IR_LOG_INFO(
            "LOD scene shot={} tier={} live_parts={} visible_parts={} resident_hidden={} "
            "dense_allocated={} dense_selected_span={} motion_phase={}",
            shotIndex,
            IRRender::toUnderlying(tier),
            liveParts,
            visibleParts,
            retainedOutsideBand,
            coarse.numVoxels_ + fine.numVoxels_,
            coarse.renders() ? coarse.numVoxels_ : fine.numVoxels_,
            shotIndex
        );
        // Transform changes follow this frame's render and settle before the next capture.
        if (shotIndex + 1 < static_cast<int>(std::size(shots_))) {
            setPhase(shotIndex + 1);
        }
    }

  private:
    static void verifyAttachment(IREntity::EntityId child, IREntity::EntityId parentEntity) {
        using namespace IRComponents;
        using namespace IRMath;
        const auto &parent = IREntity::getComponent<C_WorldTransform>(parentEntity);
        const auto &local = IREntity::getComponent<C_LocalTransform>(child);
        const auto &world = IREntity::getComponent<C_WorldTransform>(child);
        const vec3 translation =
            parent.translation_ +
            rotateVectorByQuat(parent.scale_ * local.translation_, parent.rotation_);
        const vec4 rotation = quatMul(parent.rotation_, local.rotation_);
        IR_ASSERT(
            length(world.translation_ - translation) < 0.0001f,
            "LOD child must follow its moving parent translation"
        );
        IR_ASSERT(
            length(world.scale_ - parent.scale_ * local.scale_) < 0.0001f,
            "LOD child must inherit parent scale"
        );
        IR_ASSERT(
            length(world.rotation_ - rotation) < 0.0001f ||
                length(world.rotation_ + rotation) < 0.0001f,
            "LOD child must inherit parent rotation"
        );
    }

    void setPhase(int phase) {
        using namespace IRMath;
        const bool restored = phase + 1 == static_cast<int>(std::size(shots_));
        const float offset = phase == 0 || restored ? 0.0f : static_cast<float>(phase);
        auto &flower = IREntity::getComponent<IRComponents::C_LocalTransform>(m_roots[0]);
        flower.translation_ = vec3(-12.0f + offset, -8.0f, -offset * 0.5f);
        auto &dense = IREntity::getComponent<IRComponents::C_LocalTransform>(m_denseRoot);
        dense.translation_ = vec3(16.0f - offset, 18.0f, -2.0f - offset * 0.5f);
    }

    std::array<IREntity::EntityId, 6> m_roots{};
    std::array<std::array<IREntity::EntityId, 3>, 6> m_residentParts{};
    std::array<IREntity::EntityId, 2> m_dense{};
    IREntity::EntityId m_denseRoot = IREntity::kNullEntity;
};

} // namespace IRShapeDebug

#endif /* LOD_SCENE_H */
