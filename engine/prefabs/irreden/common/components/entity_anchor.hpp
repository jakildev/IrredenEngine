#ifndef ENTITY_ANCHOR_H
#define ENTITY_ANCHOR_H

// How a discrete entity's geometry attaches to its world position
// (`C_LocalTransform.translation_`) — #2563.
//
// The engine-wide convention this enum establishes: a discrete entity's
// position is its GROUND ANCHOR — the center of its footprint in XY and the
// BOTTOM of its body in Z. Iso +Z is down, so "bottom" is the most positive z
// of the body: the ground-contact point. Any consumer (fog reveal, perception
// ranges, arrival radii, spawn placement, feet decals, overhead markers) can
// then read the entity position directly with no per-system half-height
// offsets — anchoring is a property of how geometry attaches to the position,
// not something each consumer re-derives.
//
// Adoption is per-prefab opt-in:
//   - New discrete-entity prefabs anchor GROUND.
//   - Terrain-like / corner-authored content stays CORNER.
//   - CENTER is the legacy `centerAroundOrigin = true` spelling.
// Flipping existing content is a deliberate per-prefab change, never implicit
// — the two legacy modes keep byte-identical placement.
//
// The offsets below are baked into a set's local voxel positions at
// construction time (`C_VoxelSetNew`), so every downstream path — grid
// rebuild, GPU transform prepass, face occupancy, cull extents, picking —
// consumes the anchor through those positions and needs no anchor branch of
// its own. Sites that reconstruct a body's CENTER from `size_` in local or
// world space are the exception: they must ask `anchorLocalCenter()` rather
// than assuming a convention.
//
// Not yet interpreted by `C_ColliderIso3DAABB`, SDF shapes, or
// `C_EntityCanvas`; those migrate per this enum when touched (#2563
// follow-ups). The detached-canvas path does not support GROUND — its
// extent measurement is `abs()`-about-origin and assumes CENTER.

#include <irreden/ir_math.hpp>

#include <cstdint>

namespace IRComponents {

// kFirst / kLast bracket the valid range so binding-layer range checks stay
// automatic when a mode is added — bumping kLast in lockstep keeps every
// validator honest without each call site re-hard-coding the latest
// sentinel. Pattern is documented in `.claude/rules/cpp-lua-enums.md`.
enum class EntityAnchor : std::uint8_t {
    // Legacy default: geometry extends +x/+y/+z from the translation, so the
    // translation is the set's minimum corner.
    CORNER = 0,
    // Legacy `centerAroundOrigin = true`: centered on all three axes.
    CENTER = 1,
    // Center XY, bottom Z. Cell faces span
    // [-sx/2, +sx/2) x [-sy/2, +sy/2) x [-sz, 0) relative to the translation,
    // so the ground-contact face sits exactly at `translation.z`.
    GROUND = 2,

    kFirst = CORNER,
    kLast = GROUND,
};

// The offset baked into local voxel position (0,0,0) for this anchor — i.e.
// the set's local origin. Voxel at authored index `i` sits at
// `vec3(i) + anchorOffset(anchor, size)`.
//
// GROUND's z term is `-(size.z - 0.5)`, not `-(size.z - 1) * 0.5`: it puts the
// LAST cell's far face at z == 0 rather than centering the body. That makes
// the z origin half-integer for EVERY size, unlike CENTER where it depends on
// the axis's parity. The grid path already handles half-integer local origins
// ubiquitously (every even-size CENTER set has them), so this exercises no new
// rounding behavior there — but see the header note about the detached path.
constexpr IRMath::vec3 anchorOffset(EntityAnchor anchor, IRMath::ivec3 size) {
    switch (anchor) {
    case EntityAnchor::CENTER:
        return IRMath::vec3(
            -(size.x - 1) * 0.5f,
            -(size.y - 1) * 0.5f,
            -(size.z - 1) * 0.5f
        );
    case EntityAnchor::GROUND:
        return IRMath::vec3(
            -(size.x - 1) * 0.5f,
            -(size.y - 1) * 0.5f,
            -(static_cast<float>(size.z) - 0.5f)
        );
    case EntityAnchor::CORNER:
        break;
    }
    return IRMath::vec3(0.0f);
}

// The body's CENTER relative to the translation, for consumers that need the
// middle of the box rather than its origin (deformation pivots, particle
// emission points). Equals `anchorOffset(anchor, size) + (size - 1) * 0.5` per
// axis — the center of the occupied CELL CENTERS, matching how the rest of the
// engine measures a set's middle.
constexpr IRMath::vec3 anchorLocalCenter(EntityAnchor anchor, IRMath::ivec3 size) {
    switch (anchor) {
    case EntityAnchor::CENTER:
        return IRMath::vec3(0.0f);
    case EntityAnchor::GROUND:
        return IRMath::vec3(0.0f, 0.0f, -size.z * 0.5f);
    case EntityAnchor::CORNER:
        break;
    }
    return IRMath::vec3((size.x - 1) * 0.5f, (size.y - 1) * 0.5f, (size.z - 1) * 0.5f);
}

} // namespace IRComponents

#endif /* ENTITY_ANCHOR_H */
