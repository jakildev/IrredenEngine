#ifndef LOD_TIER_SNAPSHOT_H
#define LOD_TIER_SNAPSHOT_H

#include <irreden/ir_entity.hpp>
#include <irreden/render/components/component_active_lod_level.hpp>
#include <irreden/render/components/component_lod_tier_override.hpp>
#include <irreden/render/lod_utils.hpp>

#include <unordered_map>
#include <vector>

namespace IRPrefab::Lod {

// One tick's view of the LOD tier state: the active tier plus every pinned
// entity. A tier consumer owns one as a system member, calls `capture()` in
// `beginTick` and `resolve()` per entity, so the per-entity path is a map probe
// only while some entity is pinned and never a component lookup.
struct TierSnapshot {
    // LOD_4 with no C_ActiveLodLevel row: a creation that never registers
    // LOD_UPDATE keeps every default-band entity drawn.
    IRRender::LodLevel active_ = IRRender::LodLevel::LOD_4;
    std::unordered_map<IREntity::EntityId, IRComponents::C_LodTierOverride> overrides_;

    void capture() {
        const auto *lod = IREntity::singletonOrNull<IRComponents::C_ActiveLodLevel>();
        active_ = lod != nullptr ? lod->current_ : IRRender::LodLevel::LOD_4;

        overrides_.clear();
        for (IREntity::ArchetypeNode *node : IREntity::queryArchetypeNodesSimple(
                 IREntity::getArchetype<IRComponents::C_LodTierOverride>()
             )) {
            const std::vector<IRComponents::C_LodTierOverride> &pinned =
                IREntity::getComponentData<IRComponents::C_LodTierOverride>(node);
            for (int i = 0; i < node->length_; ++i) {
                overrides_[node->entities_[i]] = pinned[i];
            }
        }
    }

    IRRender::LodLevel resolve(IREntity::EntityId entity) const {
        if (overrides_.empty()) {
            return active_;
        }
        const auto pinned = overrides_.find(entity);
        return IRRender::resolveEntityLod(
            active_,
            pinned != overrides_.end() ? &pinned->second : nullptr
        );
    }
};

} // namespace IRPrefab::Lod

#endif /* LOD_TIER_SNAPSHOT_H */
