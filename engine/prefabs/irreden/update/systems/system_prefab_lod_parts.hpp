#ifndef SYSTEM_PREFAB_LOD_PARTS_H
#define SYSTEM_PREFAB_LOD_PARTS_H

#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/lod_tier_snapshot.hpp>
#include <irreden/script/prefab_api.hpp>
#include <irreden/update/components/component_prefab_parts.hpp>

#include <cstddef>

namespace IRSystem {

// Keeps a composite prefab root's live parts equal to the parts whose band
// holds the root's resolved tier. A tier must hold for
// kPrefabPartsTierSettleTicks before the parts follow it; then a part entering
// its band spawns (at most kPrefabPartSpawnBudgetPerTick per tick, the rest on
// later ticks) and a part leaving it is destroyed, or, when resident, kept and
// hidden by pinning its content to the root's tier. Register in UPDATE after
// LOD_UPDATE. A creation that runs its own part policy leaves it out; parts
// then stay as Prefab.spawn created them.
template <> struct System<PREFAB_LOD_PARTS> {
    // Writes other entities' tier pins and reserves entity ids, so it must not
    // share a pipeline group with a system reading those pins.
    static constexpr Concurrency kConcurrency = Concurrency::MAIN_THREAD;

    IRPrefab::Lod::TierSnapshot lod_;
    int spawnBudget_ = 0;

    void beginTick() {
        lod_.capture();
        spawnBudget_ = IRConstants::kPrefabPartSpawnBudgetPerTick;
    }

    void tick(IREntity::EntityId root, IRComponents::C_PrefabParts &parts) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);
        const IRRender::LodLevel tier = lod_.resolve(root);
        if (tier != parts.candidateTier_) {
            parts.candidateTier_ = tier;
            parts.candidateTicks_ = 0;
        }
        if (parts.candidateTicks_ < IRConstants::kPrefabPartsTierSettleTicks) {
            ++parts.candidateTicks_;
        }
        if (parts.candidateTicks_ < IRConstants::kPrefabPartsTierSettleTicks) {
            return;
        }

        const bool tierChanged = tier != parts.tier_;
        parts.tier_ = tier;
        for (std::size_t i = 0; i < parts.slots_.size(); ++i) {
            IRComponents::PrefabPartSlot &slot = parts.slots_[i];
            const bool inBand = slot.inBand(tier);
            if (slot.entity_ == IREntity::kNullEntity) {
                if (inBand && spawnBudget_ > 0) {
                    --spawnBudget_;
                    IRPrefab::Prefab::stagePartSpawn(root, parts, i);
                }
                continue;
            }
            if (slot.resident_) {
                if (tierChanged) {
                    IRPrefab::Prefab::pinResidentPart(slot, tier);
                }
            } else if (!inBand) {
                IRPrefab::Prefab::despawnPart(slot);
            }
        }
    }

    static SystemId create() {
        return registerSystem<PREFAB_LOD_PARTS, IRComponents::C_PrefabParts>("PrefabLodParts");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PREFAB_LOD_PARTS_H */
