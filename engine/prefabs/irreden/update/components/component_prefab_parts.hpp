#ifndef COMPONENT_PREFAB_PARTS_H
#define COMPONENT_PREFAB_PARTS_H

#include <irreden/ir_entity.hpp>
#include <irreden/render/lod_level.hpp>

#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

namespace IRPrefab::Prefab {
// Parsed `parts` list of one spawned prefab; opaque outside prefab_api.cpp
// because it holds the parts' Lua `components` tables.
struct PartsManifest;
} // namespace IRPrefab::Prefab

namespace IRComponents {

constexpr std::size_t kPrefabRootParent = std::numeric_limits<std::size_t>::max();

// One manifest part of a composite prefab root. The band [lodMax_ .. lodMin_]
// is inclusive at both ends, indexed like every LOD band (LOD_0 finest).
struct PrefabPartSlot {
    std::size_t parentIndex_ = kPrefabRootParent;
    // Recomputed from own visibility and descendants at the settled tier.
    bool needed_ = false;
    // kNullEntity while the part does not exist.
    IREntity::EntityId entity_ = IREntity::kNullEntity;
    // The part's content entities (the part and its SHAPES children),
    // each carrying a C_LodTierOverride that mirrors the root's settled tier.
    std::vector<IREntity::EntityId> pinned_;
    IRRender::LodLevel lodMin_ = IRRender::LodLevel::LOD_4;
    IRRender::LodLevel lodMax_ = IRRender::LodLevel::LOD_0;
    // Out of band, a resident part stays alive and hidden instead of being
    // destroyed.
    bool resident_ = false;

    bool inBand(IRRender::LodLevel tier) const {
        return lodMax_ <= tier && tier <= lodMin_;
    }
};

// Root of a composite prefab. Live parts include in-band content, resident
// parts, and their transform ancestors once the root tier has settled.
struct C_PrefabParts {
    std::shared_ptr<const IRPrefab::Prefab::PartsManifest> manifest_;
    // Indexed like the manifest's parts.
    std::vector<PrefabPartSlot> slots_;
    // Parent-before-child order; slot indices retain manifest order.
    std::vector<std::size_t> hierarchyOrder_;
    // The tier the live parts realize.
    IRRender::LodLevel tier_ = IRRender::LodLevel::LOD_4;
    // The most recently resolved tier and how many consecutive ticks it held.
    IRRender::LodLevel candidateTier_ = IRRender::LodLevel::LOD_4;
    int candidateTicks_ = 0;

    void updateNeeded(IRRender::LodLevel tier) {
        for (PrefabPartSlot &slot : slots_) {
            slot.needed_ =
                slot.inBand(tier) || (slot.resident_ && slot.entity_ != IREntity::kNullEntity);
        }
        for (auto it = hierarchyOrder_.rbegin(); it != hierarchyOrder_.rend(); ++it) {
            const PrefabPartSlot &slot = slots_[*it];
            if (slot.needed_ && slot.parentIndex_ != kPrefabRootParent) {
                slots_[slot.parentIndex_].needed_ = true;
            }
        }
    }
};

} // namespace IRComponents

#endif /* COMPONENT_PREFAB_PARTS_H */
