#ifndef COMPONENT_PREFAB_PARTS_H
#define COMPONENT_PREFAB_PARTS_H

#include <irreden/ir_entity.hpp>
#include <irreden/render/lod_level.hpp>

#include <memory>
#include <vector>

namespace IRPrefab::Prefab {
// Parsed `parts` list of one spawned prefab; opaque outside prefab_api.cpp
// because it holds the parts' Lua `components` tables.
struct PartsManifest;
} // namespace IRPrefab::Prefab

namespace IRComponents {

// One manifest part of a composite prefab root. The band [lodMax_ .. lodMin_]
// is inclusive at both ends, indexed like every LOD band (LOD_0 finest).
struct PrefabPartSlot {
    // kNullEntity while the part does not exist.
    IREntity::EntityId entity_ = IREntity::kNullEntity;
    // A resident part's content entities (the part and its SHAPES children),
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

// Root of a schema-v2 prefab with a `parts` list. PREFAB_LOD_PARTS keeps the
// set of live parts equal to the parts whose band holds the root's resolved
// tier once that tier has been stable for kPrefabPartsTierSettleTicks.
struct C_PrefabParts {
    std::shared_ptr<const IRPrefab::Prefab::PartsManifest> manifest_;
    // Indexed like the manifest's parts.
    std::vector<PrefabPartSlot> slots_;
    // The tier the live parts realize.
    IRRender::LodLevel tier_ = IRRender::LodLevel::LOD_4;
    // The most recently resolved tier and how many consecutive ticks it held.
    IRRender::LodLevel candidateTier_ = IRRender::LodLevel::LOD_4;
    int candidateTicks_ = 0;
};

} // namespace IRComponents

#endif /* COMPONENT_PREFAB_PARTS_H */
