#ifndef LOD_UTILS_H
#define LOD_UTILS_H

// Zoom-to-LOD mapping and the band filter shared by LOD_UPDATE, SHAPES_TO_TRIXEL
// and GATE_VOXEL_SETS_BY_LOD. Design rationale lives in
// docs/design/lod-strategy.md.
//
// Tier index goes DOWN as detail goes UP: LOD_0 is the highest-detail tier
// (zoom-in close-up), LOD_4 is the coarsest silhouette tier (always
// rendered). A shape or DENSE voxel set carries a LOD band [lodMax_ .. lodMin_]: lodMin_ is the
// coarsest tier (largest index) it still draws at, lodMax_ the finest tier
// (smallest index). The filter draws iff lodMax_ <= activeLod <= lodMin_.
// Defaults (lodMin_ = LOD_4, lodMax_ = LOD_0) span the whole range so unmarked
// content renders at every zoom; disjoint bands across co-located variants
// give exclusive (swap, not stack) LOD. See docs/design/lod-strategy.md.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/components/component_active_lod_level.hpp>
#include <irreden/render/components/component_lod_tier_override.hpp>

#include <cstdint>

namespace IRRender {

inline constexpr std::uint32_t toUnderlying(LodLevel lodLevel) {
    return static_cast<std::uint32_t>(lodLevel);
}

// Maps the active camera zoom to the discrete LOD tier the renderer should
// use this frame. Thresholds chosen against the engine's actual zoom range
// (`kTrixelCanvasZoomMin..kTrixelCanvasZoomMax` = [1.0, 64.0], snapped to
// powers of two by render_manager.cpp): each tier doubles the previous
// zoom step so the per-tier ranges align with power-of-2 zoom snaps.
inline LodLevel computeLodLevel(float zoomLevel) {
    if (zoomLevel >= 16.0f)
        return LodLevel::LOD_0;
    if (zoomLevel >= 8.0f)
        return LodLevel::LOD_1;
    if (zoomLevel >= 4.0f)
        return LodLevel::LOD_2;
    if (zoomLevel >= 2.0f)
        return LodLevel::LOD_3;
    return LodLevel::LOD_4;
}

// The tier LOD_UPDATE wrote this tick, or LOD_4 when no creation registered it
// (the singleton's own default, so every default-band entity still draws). A
// singleton-cache probe; C++ tier consumers take it once per tick through
// IRPrefab::Lod::TierSnapshot rather than per entity.
inline LodLevel getActiveLodLevel(
    IREntity::EntityManager &entityManager,
    IREntity::ComponentId activeType
) {
    const IREntity::EntityId lodEntity =
        entityManager.getSingletonByComponentIdOrNull(activeType);
    const IREntity::EntityRecord *lodRecord =
        lodEntity != IREntity::kNullEntity ? entityManager.findRecord(lodEntity) : nullptr;
    if (lodRecord == nullptr || lodRecord->archetypeNode == nullptr) {
        return LodLevel::LOD_4;
    }
    const auto found = lodRecord->archetypeNode->components_.find(activeType);
    if (found == lodRecord->archetypeNode->components_.end()) {
        return LodLevel::LOD_4;
    }
    const auto *lod = IREntity::castComponentDataPointer<IRComponents::C_ActiveLodLevel>(
        found->second.get()
    );
    return lod->dataVector[lodRecord->row].current_;
}

inline LodLevel getActiveLodLevel() {
    IREntity::EntityManager &entityManager = IREntity::getEntityManager();
    return getActiveLodLevel(
        entityManager,
        entityManager.getComponentType<IRComponents::C_ActiveLodLevel>()
    );
}

// getActiveLodLevel() as the integer Lua sees (0 finest .. 4 coarsest). Backs
// both the `IRRender.getActiveLodTier` binding and its CODEGEN intrinsic, so an
// EVAL and a CODEGEN system read the same value.
inline std::int32_t getActiveLodTier() {
    return static_cast<std::int32_t>(getActiveLodLevel());
}

// The tier one entity resolves to: its pinned tier when it carries a
// C_LodTierOverride, else the frame's zoom-derived @p active tier. Every tier
// consumer resolves through this so an override means the same thing to all of
// them. @p override is null for an entity without the component; callers pass
// values captured once per tick (IRPrefab::Lod::TierSnapshot), never a
// per-entity lookup.
inline LodLevel resolveEntityLod(LodLevel active, const IRComponents::C_LodTierOverride *override) {
    return override != nullptr ? override->tier_ : active;
}

// True when a shape occupying the inclusive LOD band [@p lodMax (finest tier,
// smallest index) .. @p lodMin (coarsest tier, largest index)] should be culled
// at the current @p activeLod. The shape draws only when activeLod lands inside
// the band; it is skipped when the camera is coarser than the band's low-detail
// floor (activeLod > lodMin) or finer than its high-detail ceiling
// (activeLod < lodMax — a finer variant has taken over).
//
// Defaults (lodMin = LOD_4, lodMax = LOD_0) span the whole tier range, so an
// unmarked shape is never culled (every activeLod satisfies LOD_0 <= activeLod
// <= LOD_4) — byte-identical to the pre-band single-sided filter. Authoring
// co-located variants with disjoint bands yields exclusive LOD: exactly one
// renders per zoom, so they swap rather than stack (the additive-overlap
// visibility glitch). The coarsest variant keeps lodMin = LOD_4 to persist at
// min zoom; the finest keeps lodMax = LOD_0 to persist past its threshold.
inline bool shouldSkipAtLod(LodLevel lodMin, LodLevel lodMax, LodLevel activeLod) {
    const std::uint32_t active = toUnderlying(activeLod);
    return active > toUnderlying(lodMin) || active < toUnderlying(lodMax);
}

} // namespace IRRender

#endif /* LOD_UTILS_H */
