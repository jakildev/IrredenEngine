// Detached re-voxelize world-depth composite — CPU↔GPU GRID-equivalence
// (#1576 P4b-1). Verifies the architect's Q4 invariant: a voxel placed at a
// known world cell via the opt-in WORLD-PLACED detached path composites to the
// SAME framebuffer depth as the same world cell rendered through GRID.
//
// The framebuffer composite depth is `normalizeDistance(rawDist + distanceOffset)`
// (f_trixel_to_framebuffer.{glsl,metal}), monotonic in `rawDist + distanceOffset`,
// so two cells share a composite depth iff they share `rawDist + distanceOffset`.
// This test asserts that integer-exact equality — no GL/Metal context needed.
//
//   GRID path        rawDist = pos3DtoDistance(round(worldCell)),  distanceOffset = 0
//                    worldCell = GridRotation::worldCellForGridVoxel(local, off, wt)
//                    (the value REBUILD_GRID_VOXELS writes for a world voxel).
//
//   detached path    rawDist = pos3DtoDistance(round(modelCell)),  modelCell is the
//                    POOL-CENTERED rotated cell (c_revoxelize_detached: translation
//                    zeroed). VOXEL_TO_TRIXEL_STAGE_1 keeps voxelDepthAxis = (1,1,1)
//                    for re-voxelize, so its rawDist is exactly pos3DtoDistance.
//                    distanceOffset = pos3DtoDistance(roundVec3HalfUp(translation))
//                    — the value ENTITY_CANVAS_TO_FRAMEBUFFER sets when worldPlaced_.
//
// Because pos3DtoDistance is linear (x+y+z) and the entity translation is an
// integer world cell, distanceOffset + modelCell-depth == worldCell-depth EXACTLY,
// at any rotation. (Fractional translations are out of the exactness guarantee —
// no single per-entity offset can reproduce per-voxel cell rounding of a
// fractional sum; see the design doc Q4.)

#include <gtest/gtest.h>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/voxel/grid_rotation.hpp>

#include <array>

namespace {

using IRComponents::C_WorldTransform;
using IRMath::ivec3;
using IRMath::vec3;
using IRMath::vec4;
using IRPrefab::GridRotation::worldCellForGridVoxel;

// The rawDist a world voxel writes into the shared distance texture, mirroring
// VOXEL_TO_TRIXEL_STAGE_1's `pos3DtoDistance(ivec3(round(voxelPosition)))`.
int worldVoxelRawDist(vec3 worldCell) {
    return IRMath::pos3DtoDistance(IRMath::roundVec3ToIVec3(worldCell));
}

// The distanceOffset ENTITY_CANVAS_TO_FRAMEBUFFER writes for a world-placed
// detached canvas — kept in lockstep with the system's expression.
int worldPlacedDistanceOffset(vec3 translation) {
    return IRMath::pos3DtoDistance(IRMath::roundVec3HalfUp(translation));
}

C_WorldTransform makeTransform(vec3 translation, vec4 rotation) {
    C_WorldTransform wt;
    wt.translation_ = translation;
    wt.rotation_ = rotation;
    wt.scale_ = vec3(1.0f); // re-voxelize is scale-free (c_revoxelize_detached)
    return wt;
}

vec4 quatAxisAngle(vec3 axis, float radians) {
    return IRMath::quatAxisAngle(IRMath::normalize(axis), radians);
}

constexpr vec4 kIdentityQuat{0.0f, 0.0f, 0.0f, 1.0f};

// Composite depth of one re-voxelize voxel: its pool-centered model cell (the
// SAME GridRotation math the GPU scatter mirrors, with translation zeroed) plus
// the world-placed entity offset. This is exactly what reaches gl_FragDepth's
// `rawDist + distanceOffset` for the WORLD-PLACED detached path.
int detachedWorldPlacedDepth(vec3 local, vec3 offset, const C_WorldTransform &wt) {
    const C_WorldTransform poolFrame = makeTransform(vec3(0.0f), wt.rotation_);
    const vec3 modelCell = worldCellForGridVoxel(local, offset, poolFrame);
    return worldVoxelRawDist(modelCell) + worldPlacedDistanceOffset(wt.translation_);
}

// GRID depth of the SAME voxel at the SAME entity transform.
int gridDepth(vec3 local, vec3 offset, const C_WorldTransform &wt) {
    return worldVoxelRawDist(worldCellForGridVoxel(local, offset, wt));
}

// A spread of pool-local voxel positions (centered around the pool origin) +
// per-voxel offsets, exercising every octant.
const std::array<std::pair<vec3, vec3>, 7> kVoxels = {{
    {vec3(0.0f, 0.0f, 0.0f), vec3(0.0f)},
    {vec3(3.0f, -2.0f, 5.0f), vec3(0.0f)},
    {vec3(-4.0f, 6.0f, -1.0f), vec3(0.0f)},
    {vec3(5.0f, 5.0f, 5.0f), vec3(0.0f)},
    {vec3(-6.0f, -6.0f, -6.0f), vec3(0.0f)},
    {vec3(2.0f, -3.0f, 4.0f), vec3(1.0f, 0.0f, -1.0f)}, // with squash-stretch offset
    {vec3(-1.0f, 7.0f, 2.0f), vec3(0.0f, -2.0f, 1.0f)},
}};

// Integer entity world cells (the exactness domain).
const std::array<vec3, 5> kTranslations = {{
    vec3(0.0f, 0.0f, 0.0f),
    vec3(0.0f, 0.0f, 42.0f),
    vec3(0.0f, 0.0f, -42.0f),
    vec3(10.0f, -7.0f, 3.0f),
    vec3(-15.0f, 20.0f, -8.0f),
}};

// Cardinal poses: identity + the three Z cardinals. A cube's integer cells map
// to integer coords under these (no half-integer rounding ambiguity), so the
// composite depth is BIT-EXACT against GRID — the architect's Q4 "a voxel placed
// at a known world cell composites to the same depth as GRID" guarantee.
const std::array<vec4, 4> kCardinalRotations = {{
    kIdentityQuat,
    quatAxisAngle(vec3(0, 0, 1), IRMath::kHalfPi),
    quatAxisAngle(vec3(0, 0, 1), IRMath::kPi),
    quatAxisAngle(vec3(0, 0, 1), -IRMath::kHalfPi),
}};

// Arbitrary off-cardinal poses. re-voxelize keeps voxelDepthAxis=(1,1,1) (the
// rotation lives in the cells), so model rawDist is the exact world iso depth of
// the pool-centered cell and the constant offset shifts it into the world band.
// The only residual is a ±1-rawDist sub-cell quantization wobble at exact
// half-integer post-rotation coordinates: GRID rounds `translation + rotated`
// while the detached canvas rounds the model `rotated` then the composite adds
// the integer offset, and floating-point `floor(x + 0.5)` can tip the other way
// when the magnitude shift changes the representable precision. ±1 of 131070
// total depth codes is depth-negligible (the same sub-cell class as re-voxelize's
// accepted round-to-cell speckle) — the solid still sorts exactly into its world
// depth band.
const std::array<vec4, 4> kArbitraryRotations = {{
    quatAxisAngle(vec3(1, 0, 0), IRMath::kQuarterPi),
    quatAxisAngle(vec3(1, 1, 0), IRMath::kPi / 3.0f),
    quatAxisAngle(vec3(1, 0.6f, 0.3f), IRMath::kPi / 4.5f),
    quatAxisAngle(vec3(0.3f, 1.0f, 0.5f), IRMath::kPi / 5.0f),
}};

} // namespace

// The headline invariant at the exactness domain: at cardinal poses + integer
// translations, world-placed detached composite depth == GRID depth, bit-exact.
TEST(DetachedWorldDepthTest, MatchesGridDepthAtCardinalPoses) {
    for (const vec3 &translation : kTranslations) {
        for (const vec4 &rotation : kCardinalRotations) {
            const C_WorldTransform wt = makeTransform(translation, rotation);
            for (const auto &[local, offset] : kVoxels) {
                EXPECT_EQ(detachedWorldPlacedDepth(local, offset, wt), gridDepth(local, offset, wt))
                    << "world-placed detached depth must equal GRID depth — "
                    << "translation (" << translation.x << "," << translation.y << ","
                    << translation.z << ") local (" << local.x << "," << local.y << "," << local.z
                    << ")";
            }
        }
    }
}

// At arbitrary off-cardinal poses the composite depth tracks GRID to within one
// rawDist code (the documented sub-cell half-integer wobble) — never more.
TEST(DetachedWorldDepthTest, TracksGridDepthWithinOneRawDistAtArbitraryPoses) {
    for (const vec3 &translation : kTranslations) {
        for (const vec4 &rotation : kArbitraryRotations) {
            const C_WorldTransform wt = makeTransform(translation, rotation);
            for (const auto &[local, offset] : kVoxels) {
                const int delta = IRMath::abs(
                    detachedWorldPlacedDepth(local, offset, wt) - gridDepth(local, offset, wt)
                );
                EXPECT_LE(delta, 1)
                    << "world-placed detached depth must track GRID within ±1 rawDist — "
                    << "translation (" << translation.x << "," << translation.y << ","
                    << translation.z << ") local (" << local.x << "," << local.y << "," << local.z
                    << ")";
            }
        }
    }
}

// distanceOffset is the entity's pure world iso depth: the same value GRID writes
// for a voxel sitting AT the entity origin (local = offset = 0).
TEST(DetachedWorldDepthTest, OffsetEqualsEntityOriginGridDepth) {
    for (const vec3 &translation : kTranslations) {
        const C_WorldTransform wt = makeTransform(translation, kIdentityQuat);
        EXPECT_EQ(worldPlacedDistanceOffset(translation), gridDepth(vec3(0.0f), vec3(0.0f), wt));
    }
}

// The default (screen-locked overlay) path keeps distanceOffset = 0, so its
// composite depth is the pool-centered model depth — independent of the entity's
// world position. This is what makes the default byte-identical across world
// positions (and is the behavior worldPlaced_ opts OUT of).
TEST(DetachedWorldDepthTest, OverlayDepthIsWorldPositionIndependent) {
    const auto &[local, offset] = kVoxels[1];
    const C_WorldTransform poolFrame = makeTransform(vec3(0.0f), kIdentityQuat);
    const int overlayDepth = worldVoxelRawDist(worldCellForGridVoxel(local, offset, poolFrame));

    // Same voxel, two different world placements → identical overlay depth (the
    // overlay ignores translation), but DIFFERENT world-placed depth (it doesn't).
    const C_WorldTransform near = makeTransform(vec3(0.0f, 0.0f, 0.0f), kIdentityQuat);
    const C_WorldTransform far = makeTransform(vec3(0.0f, 0.0f, 42.0f), kIdentityQuat);
    EXPECT_EQ(
        worldVoxelRawDist(
            worldCellForGridVoxel(local, offset, makeTransform(near.translation_, kIdentityQuat))
        ),
        overlayDepth
    );
    EXPECT_NE(
        detachedWorldPlacedDepth(local, offset, far),
        detachedWorldPlacedDepth(local, offset, near)
    );
}

// The linearity of pos3DtoDistance is what makes the constant per-entity offset
// exact: the offset can be hoisted out of the per-voxel depth precisely because
// pos3DtoDistance(translation + cell) == pos3DtoDistance(translation) + pos3DtoDistance(cell).
TEST(DetachedWorldDepthTest, Pos3DtoDistanceIsLinearOverIntegerCells) {
    const std::array<ivec3, 4> a = {
        {ivec3(0, 0, 42), ivec3(10, -7, 3), ivec3(-15, 20, -8), ivec3(1, 1, 1)}
    };
    const std::array<ivec3, 4> b = {
        {ivec3(3, -2, 5), ivec3(-4, 6, -1), ivec3(5, 5, 5), ivec3(-6, -6, -6)}
    };
    for (const ivec3 &x : a) {
        for (const ivec3 &y : b) {
            EXPECT_EQ(
                IRMath::pos3DtoDistance(x + y),
                IRMath::pos3DtoDistance(x) + IRMath::pos3DtoDistance(y)
            );
        }
    }
}
