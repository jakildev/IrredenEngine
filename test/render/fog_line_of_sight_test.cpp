// FogLineOfSight exercises the CPU half of the fog line-of-sight model
// (component_canvas_fog_of_war.hpp states it; fog_line_of_sight.hpp implements
// it): the half-cell column raster, the exact segment march, the softness
// band and the oracle the reveal evaluator reads. Headless — the raster and
// the march work on plain vectors, and the shape raster needs only an
// EntityManager.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/math/sdf.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/fog_line_of_sight.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using IRComponents::C_CanvasFogOfWar;
using IRComponents::C_LightBlocker;
using IRComponents::C_ShapeDescriptor;
using IRComponents::C_VoxelPool;
using IRComponents::C_WorldTransform;
using IRComponents::FogLosColumnField;
using IRComponents::FrameDataFogObservers;
using IRComponents::kFogLosClearanceTolerance;
using IRComponents::kFogLosColumnEmpty;
using IRComponents::kFogLosFieldHalfExtent;
using IRComponents::kFogLosHardGate;
using IRMath::ivec2;
using IRMath::ivec3;
using IRMath::vec2;
using IRMath::vec3;
using IRMath::vec4;
using IRPrefab::Fog::LosRasterFrame;

// The field corner a window centred on the world origin anchors.
const ivec2 kWorldFieldMin(-kFogLosFieldHalfExtent);

std::vector<float> emptyField() {
    return std::vector<float>(IRComponents::kFogLosFieldFloatCount, kFogLosColumnEmpty);
}

std::vector<float> flatField(float topPlane) {
    return std::vector<float>(IRComponents::kFogLosFieldFloatCount, topPlane);
}

LosRasterFrame subdividedFrame() {
    LosRasterFrame frame;
    frame.subdivisions_ = 8;
    return frame;
}

void stampVoxel(
    std::vector<float> &field,
    vec3 position,
    const LosRasterFrame &frame,
    ivec2 fieldMin = kWorldFieldMin
) {
    vec3 boxMin;
    vec3 boxMax;
    IRPrefab::Fog::losVoxelBox(position, frame, boxMin, boxMax);
    IRPrefab::Fog::stampLosBox(field, fieldMin, vec2(boxMin), vec2(boxMax), boxMin.z);
    IRPrefab::Fog::buildLosPyramid(field);
}

float topAt(
    const std::vector<float> &field, int halfCellX, int halfCellY, ivec2 fieldMin = kWorldFieldMin
) {
    return FogLosColumnField{field.data(), fieldMin}.topPlane(halfCellX, halfCellY);
}

// The fog_demo --occlusion scene as the subdivided raster draws it: slab top
// voxels with centre z 4 (top plane 4), the ridge's even-sized set at
// x {-0.5, 0.5}, y -7.5..7.5, top voxel centre -0.5 (top plane -0.5, box
// x -0.5..1.5, y -7.5..8.5), and a free-standing tower at (-2, 5) ten voxels
// above the ground.
constexpr float kGroundTop = 4.0f;
constexpr float kRidgeTop = -0.5f;
const vec3 kGroundEye(-6.0f, 0.0f, 3.0f);
const vec3 kRidgeEye(0.0f, 0.0f, -2.0f);

// The ridge scene raised by @p shift and translated by @p offset in XY, in the
// field whose lower corner is @p fieldMin.
std::vector<float>
ridgeField(float shift = 0.0f, vec2 offset = vec2(0.0f), ivec2 fieldMin = kWorldFieldMin) {
    std::vector<float> field = flatField(kGroundTop + shift);
    const LosRasterFrame frame = subdividedFrame();
    for (int row = -7; row <= 8; ++row) {
        const float y = static_cast<float>(row) - 0.5f + offset.y;
        stampVoxel(field, vec3(-0.5f + offset.x, y, kRidgeTop + shift), frame, fieldMin);
        stampVoxel(field, vec3(0.5f + offset.x, y, kRidgeTop + shift), frame, fieldMin);
    }
    stampVoxel(
        field,
        vec3(offset + vec2(-2.0f, 5.0f), kGroundTop - 10.0f + shift),
        frame,
        fieldMin
    );
    return field;
}

bool clear(
    const std::vector<float> &field, vec3 eye, vec3 target, ivec2 fieldMin = kWorldFieldMin
) {
    float bandClearance = 0.0f;
    const float minClearance = IRPrefab::Fog::traceLosClearance(
        FogLosColumnField{field.data(), fieldMin},
        eye,
        target,
        kFogLosHardGate,
        bandClearance
    );
    return minClearance >= -kFogLosClearanceTolerance;
}

FrameDataFogObservers gatedSources(
    std::initializer_list<vec4> circles, float observerZ, float eyeHeight, float softness = 0.0f
) {
    FrameDataFogObservers observers{};
    for (const vec4 &circle : circles) {
        const int slot = C_CanvasFogOfWar::addVisionCircle(
            observers,
            circle.x,
            circle.y,
            circle.z,
            circle.w,
            observerZ,
            0.0f,
            0.0f,
            0.0f
        );
        C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, slot, eyeHeight, softness);
    }
    return observers;
}

float visibility(
    const std::vector<float> &field,
    const FrameDataFogObservers &observers,
    int source,
    vec3 position,
    ivec2 fieldMin = kWorldFieldMin
) {
    return IRPrefab::Fog::losVisibility(
        FogLosColumnField{field.data(), fieldMin},
        observers,
        source,
        position
    );
}

} // namespace

// Flat ground never hides itself: samples resting on the plane pass hard and
// soft, across fractional eye heights and centres (one on a negative
// half-integer) and translations of the whole scene. Mutation control: letting
// the sample's own surface into the softness band fails the soft half beyond
// the eye's cell.
TEST(FogLineOfSightTest, FlatGroundRemainsVisible) {
    constexpr float kRadius = 32.0f;
    int probed = 0;
    for (const float top : {4.0f, 0.0f, -1024.0f, 1024.0f}) {
        const std::vector<float> field = flatField(top);
        for (const float eyeHeight : {1.5f, 0.5f, 0.73f, 2.25f, 0.05f}) {
            for (const vec2 centre : {vec2(0.0f), vec2(0.37f, -0.61f), vec2(-2.5f, -3.5f)}) {
                for (const float softness : {kFogLosHardGate, 1.0f}) {
                    const FrameDataFogObservers observers =
                        gatedSources({vec4(centre, kRadius, 0.0f)}, top, eyeHeight, softness);
                    for (float y = -40.0f; y <= 40.0f; y += 0.5f) {
                        for (float x = -40.0f; x <= 40.0f; x += 0.5f) {
                            const vec2 sample(x + 0.13f, y - 0.29f);
                            if (IRMath::length(sample - centre) > kRadius) {
                                continue;
                            }
                            ++probed;
                            ASSERT_FLOAT_EQ(
                                visibility(field, observers, 0, vec3(sample, top)),
                                1.0f
                            ) << "flat ground hid itself at ("
                              << sample.x << ", " << sample.y << ") top " << top << " eye height "
                              << eyeHeight << " softness " << softness;
                        }
                    }
                }
            }
        }
    }
    EXPECT_GT(probed, 0);
}

// The eye's own half-cell never occludes, whatever stands in it; the next
// half-cell along the ray does.
TEST(FogLineOfSightTest, EyeCellNeverOccludes) {
    std::vector<float> field = flatField(kGroundTop);
    const vec3 eye(-2.3f, -3.4f, 2.0f);
    IRPrefab::Fog::stampLosBox(
        field,
        kWorldFieldMin,
        vec2(-2.5f, -3.5f),
        vec2(-2.0f, -3.0f),
        -20.0f
    );
    IRPrefab::Fog::buildLosPyramid(field);
    EXPECT_TRUE(clear(field, eye, vec3(3.0f, -3.4f, kGroundTop)))
        << "the eye's own half-cell occluded the ray";

    IRPrefab::Fog::stampLosBox(
        field,
        kWorldFieldMin,
        vec2(-2.0f, -3.5f),
        vec2(-1.5f, -3.0f),
        -20.0f
    );
    IRPrefab::Fog::buildLosPyramid(field);
    EXPECT_FALSE(clear(field, eye, vec3(3.0f, -3.4f, kGroundTop)))
        << "control: a tower one half-cell along the ray must occlude";
}

TEST(FogLineOfSightTest, SameCellOutOfFieldAndUnpublishedAreClear) {
    const std::vector<float> field = flatField(-100.0f);
    EXPECT_TRUE(clear(field, vec3(3.2f, 4.4f, 0.0f), vec3(3.4f, 4.3f, 50.0f)))
        << "a target in the eye's half-cell is never occluded";
    EXPECT_TRUE(clear(emptyField(), vec3(0.0f), vec3(kFogLosFieldHalfExtent + 4.0f, 0.0f, 0.0f)))
        << "out-of-field columns are empty";

    const FrameDataFogObservers observers =
        gatedSources({vec4(0.0f, 0.0f, 10.0f, 0.0f)}, 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::losVisibility(FogLosColumnField{}, observers, 0, vec3(1.0f)),
        0.0f
    ) << "an unpublished field is closed";
    EXPECT_FLOAT_EQ(visibility(field, observers, 0, vec3(40.0f, 0.0f, 0.0f)), 1.0f)
        << "past the source's reach nothing is gated";
}

// The ridge scene from the ground: the near slab and the ridge's facing wall
// are visible, the slab behind it and the ridge's top (the eye is below it)
// are hidden, and a tower top above the ridge's shadow line stays visible.
// From the ridge top the far slab reveals while the strip at the ridge's base
// stays in its shadow.
TEST(FogLineOfSightTest, RidgeHidesWhatIsBehindItFromTheGround) {
    const std::vector<float> field = ridgeField();
    EXPECT_TRUE(clear(field, kGroundEye, vec3(-3.0f, 0.0f, kGroundTop)));
    EXPECT_TRUE(clear(field, kGroundEye, vec3(-0.52f, 0.0f, 1.0f)))
        << "the ridge's facing wall, a hair out of the face, is visible";
    EXPECT_FALSE(clear(field, kGroundEye, vec3(-0.25f, 0.0f, kRidgeTop)))
        << "a wall top seen from below is hidden";
    EXPECT_FALSE(clear(field, kGroundEye, vec3(6.0f, 0.0f, kGroundTop)));
    EXPECT_TRUE(clear(field, kGroundEye, vec3(6.0f, 0.0f, -10.0f)))
        << "a tower top above the ridge's shadow line stays visible";

    EXPECT_TRUE(clear(field, kRidgeEye, vec3(0.75f, 0.0f, kRidgeTop)))
        << "the ridge top is visible from an eye above it";
    EXPECT_FALSE(clear(field, kRidgeEye, vec3(3.0f, 0.0f, kGroundTop)))
        << "the strip at the ridge's base is in its own shadow";
    EXPECT_TRUE(clear(field, kRidgeEye, vec3(10.0f, 0.0f, kGroundTop)));

    EXPECT_TRUE(clear(flatField(kGroundTop), kGroundEye, vec3(6.0f, 0.0f, kGroundTop)))
        << "control: without the ridge the far ground is visible";
}

// The field anchored with a fog window: a window centred on the world origin
// anchors the world-centred field, and every window's field is aligned to the
// coarsest pyramid block and holds at least 96 cells around the window's
// snapped centre on each axis.
TEST(FogLineOfSightTest, FieldAnchorsOnTheWindowCentre) {
    constexpr int kCoarsestBlock = 1 << (IRComponents::kFogLosLevelCount - 1);
    constexpr int kHeldHalfCells = 192;
    for (const int edge : {1152, 1472, 4096}) {
        EXPECT_EQ(FogLosColumnField::fieldMinForWindow(ivec2(-edge / 2), edge), kWorldFieldMin)
            << "edge " << edge;
        for (int y = -5000; y <= 5000; y += 997) {
            for (int x = -5000; x <= 5000; x += 1231) {
                const ivec2 origin = IRPrefab::Fog::detail::windowOriginForCentre(
                    vec2(static_cast<float>(x) + 0.3f, static_cast<float>(y) - 0.4f),
                    edge
                );
                const ivec2 centre = (origin + edge / 2) * IRComponents::kFogLosCellsPerUnit;
                const ivec2 fieldMin = FogLosColumnField::fieldMinForWindow(origin, edge);
                EXPECT_EQ(IRMath::floorMod(fieldMin.x, kCoarsestBlock), 0);
                EXPECT_EQ(IRMath::floorMod(fieldMin.y, kCoarsestBlock), 0);
                EXPECT_TRUE(FogLosColumnField::cellInField(centre - kHeldHalfCells, fieldMin))
                    << "centre (" << x << ", " << y << ") edge " << edge;
                EXPECT_TRUE(FogLosColumnField::cellInField(centre + kHeldHalfCells - 1, fieldMin))
                    << "centre (" << x << ", " << y << ") edge " << edge;
            }
        }
    }
}

// The window re-anchor: with the ridge scene translated far past the
// world-centred field and the window centred on it, the ridge still hides the
// slab behind it, through the march and through the source's gated factor.
// Control: the same stamps into the world-centred field fall outside it, so
// there the ridge is unknown and occludes nothing.
TEST(FogLineOfSightTest, OffOriginRidgeOccludesInTheWindowAnchoredField) {
    constexpr int kWindowEdge = 1152;
    const vec2 offset(640.0f, -392.0f);
    const ivec2 fieldMin = FogLosColumnField::fieldMinForWindow(
        IRPrefab::Fog::detail::windowOriginForCentre(offset, kWindowEdge),
        kWindowEdge
    );
    const vec3 eye = kGroundEye + vec3(offset, 0.0f);
    const vec3 nearSlab(offset.x - 3.0f, offset.y, kGroundTop);
    const vec3 behindRidge(offset.x + 6.0f, offset.y, kGroundTop);

    const std::vector<float> anchored = ridgeField(0.0f, offset, fieldMin);
    EXPECT_TRUE(clear(anchored, eye, nearSlab, fieldMin));
    EXPECT_FALSE(clear(anchored, eye, behindRidge, fieldMin))
        << "the ridge hides nothing in the field anchored on the window";
    const FrameDataFogObservers observers =
        gatedSources({vec4(eye.x, eye.y, 14.0f, 0.0f)}, kGroundTop + 0.5f, 1.5f);
    EXPECT_FLOAT_EQ(visibility(anchored, observers, 0, nearSlab, fieldMin), 1.0f);
    EXPECT_FLOAT_EQ(visibility(anchored, observers, 0, behindRidge, fieldMin), 0.0f);

    const std::vector<float> worldCentred = ridgeField(0.0f, offset);
    EXPECT_TRUE(clear(worldCentred, eye, behindRidge))
        << "control: a world-centred field must not hold the off-origin ridge";
}

// The shadow's edges are straight lines: along the ridge's flank the verdict
// flips exactly where the segment leaves the ridge's footprint, and behind a
// plateau exactly where the segment clears its top plane.
TEST(FogLineOfSightTest, ShadowEdgesAreExactLines) {
    const std::vector<float> field = ridgeField();
    // The ridge spans y -7.5..8.5; from the ground eye at (-6, 0) a segment to
    // (2, y) crosses the ridge's near face (x = -0.5) at y * 5.5 / 8.
    for (const float y : {12.0f, 12.4f, 13.0f, 20.0f}) {
        const bool inside = y * 5.5f / 8.0f < 8.5f;
        EXPECT_EQ(clear(field, kGroundEye, vec3(2.0f, y, kGroundTop)), !inside)
            << "flank at y " << y;
    }
    // From the ridge eye 1.5 above the top, the plateau's far edge (x = 1.5)
    // hides the ground until x = 6 and no farther.
    for (const float x : {2.0f, 5.9f, 6.1f, 8.0f}) {
        EXPECT_EQ(clear(field, kRidgeEye, vec3(x, 0.0f, kGroundTop)), x > 6.0f)
            << "far edge at x " << x;
    }
}

// Softness grades the far edge behind a plateau over the clearance band and
// never softens a flank, a sample's own surface, or a hard-blocked sample.
TEST(FogLineOfSightTest, SoftnessGradesTheFarEdgeOnly) {
    const std::vector<float> field = ridgeField();
    const FrameDataFogObservers observers =
        gatedSources({vec4(0.0f, 0.0f, 14.0f, 0.0f)}, kRidgeTop, 1.5f, 1.0f);
    EXPECT_FLOAT_EQ(visibility(field, observers, 0, vec3(3.0f, 0.0f, kGroundTop)), 0.0f)
        << "a blocked sample stays 0 under softness";
    const float nearEdge = visibility(field, observers, 0, vec3(6.5f, 0.0f, kGroundTop));
    const float farther = visibility(field, observers, 0, vec3(8.0f, 0.0f, kGroundTop));
    EXPECT_GT(nearEdge, 0.0f);
    EXPECT_LT(nearEdge, 1.0f);
    EXPECT_GT(farther, nearEdge) << "the band rises with clearance";
    // The segment to x = 20 clears the ridge's far edge by 1.05, past the band.
    EXPECT_FLOAT_EQ(visibility(field, observers, 0, vec3(20.0f, 0.0f, kGroundTop)), 1.0f)
        << "well past the band the sample is fully visible";

    FrameDataFogObservers hard = observers;
    C_CanvasFogOfWar::setVisionCircleLineOfSight(hard, 0, 1.5f, kFogLosHardGate);
    EXPECT_FLOAT_EQ(visibility(field, hard, 0, vec3(6.5f, 0.0f, kGroundTop)), 1.0f)
        << "the hard gate is a step at the same edge";

    // From the ground, the ridge's flank stays a step under softness: the
    // segment to (2, 12) crosses the ridge, the one to (2, 14) passes its end
    // and crosses only ground at the sample's own height.
    const FrameDataFogObservers ground =
        gatedSources({vec4(-6.0f, 0.0f, 14.0f, 0.0f)}, 4.5f, 1.5f, 1.0f);
    EXPECT_FLOAT_EQ(visibility(field, ground, 0, vec3(2.0f, 12.0f, kGroundTop)), 0.0f);
    EXPECT_FLOAT_EQ(visibility(field, ground, 0, vec3(2.0f, 14.0f, kGroundTop)), 1.0f)
        << "a flank stays a step: nothing higher than the sample is crossed";
}

// An anchor authored inside its ground voxel is evaluated on the voxel's top
// plane, so bodies resting on the ground read their surface.
TEST(FogLineOfSightTest, AnchorsBelowTheirSurfaceAreLifted) {
    const std::vector<float> field = ridgeField();
    const FrameDataFogObservers observers =
        gatedSources({vec4(-6.0f, 0.0f, 14.0f, 0.0f)}, 4.5f, 1.5f);
    EXPECT_FLOAT_EQ(visibility(field, observers, 0, vec3(-3.0f, 0.5f, 4.5f)), 1.0f);
    EXPECT_FLOAT_EQ(visibility(field, observers, 0, vec3(6.0f, 0.5f, 4.5f)), 0.0f);
    EXPECT_FLOAT_EQ(visibility(field, observers, 0, vec3(6.0f, 0.5f, -10.0f)), 1.0f)
        << "a point above the surface keeps its own height";
}

// Voxel boxes land on the half-cell lattice whether the set is integer- or
// half-integer-positioned, follow the raster's subdivision snap, and turn
// with the cardinal view.
TEST(FogLineOfSightTest, VoxelBoxesFollowTheRasterLattice) {
    std::vector<float> field = emptyField();
    stampVoxel(field, vec3(0.5f, 0.5f, -3.0f), subdividedFrame());
    EXPECT_FLOAT_EQ(topAt(field, 1, 1), -3.0f);
    EXPECT_FLOAT_EQ(topAt(field, 2, 2), -3.0f);
    EXPECT_FLOAT_EQ(topAt(field, 0, 1), kFogLosColumnEmpty);
    EXPECT_FLOAT_EQ(topAt(field, 3, 1), kFogLosColumnEmpty);

    field = emptyField();
    stampVoxel(field, vec3(0.0f, 0.0f, -3.0f), subdividedFrame());
    EXPECT_FLOAT_EQ(topAt(field, 0, 0), -3.0f);
    EXPECT_FLOAT_EQ(topAt(field, 1, 1), -3.0f);
    EXPECT_FLOAT_EQ(topAt(field, -1, 0), kFogLosColumnEmpty);
    EXPECT_FLOAT_EQ(topAt(field, 2, 0), kFogLosColumnEmpty);

    field = emptyField();
    stampVoxel(field, vec3(0.5f, 0.5f, -3.5f), LosRasterFrame{});
    EXPECT_FLOAT_EQ(topAt(field, 2, 2), -3.0f)
        << "at subdivision 1 the raster rounds the half-integer set half up";
    EXPECT_FLOAT_EQ(topAt(field, 1, 1), kFogLosColumnEmpty);

    field = emptyField();
    LosRasterFrame quarterTurn;
    quarterTurn.cardinal_ = IRMath::CardinalIndex::k90;
    stampVoxel(field, vec3(0.0f, 0.0f, 2.0f), quarterTurn);
    EXPECT_FLOAT_EQ(topAt(field, -2, 0), 2.0f) << "a quarter turn draws the cell toward -X";
    EXPECT_FLOAT_EQ(topAt(field, -1, 1), 2.0f);
    EXPECT_FLOAT_EQ(topAt(field, 0, 0), kFogLosColumnEmpty);

    field = emptyField();
    LosRasterFrame turning;
    turning.rotating_ = true;
    stampVoxel(field, vec3(0.25f, 0.0f, 2.0f), turning);
    EXPECT_FLOAT_EQ(topAt(field, 1, 0), 2.0f)
        << "a turning camera keeps the continuous box, on the nearest half-cell edges";
    EXPECT_FLOAT_EQ(topAt(field, 2, 0), 2.0f);
    EXPECT_FLOAT_EQ(topAt(field, 0, 0), kFogLosColumnEmpty);
    EXPECT_FLOAT_EQ(topAt(field, 3, 0), kFogLosColumnEmpty);
}

// Every source reads its own eye and softness: eight sources at distinct
// spots each agree with the plain march from their own eye, and at least one
// probe per source differs from the next source's verdict.
TEST(FogLineOfSightTest, EightSourcesUseIndependentParams) {
    const std::vector<float> field = ridgeField();
    FrameDataFogObservers observers{};
    for (int i = 0; i < IRComponents::kMaxFogVisionCircles; ++i) {
        const float x = i % 2 == 0 ? -6.0f - static_cast<float>(i) : 7.0f + static_cast<float>(i);
        const int slot = C_CanvasFogOfWar::addVisionCircle(
            observers,
            x,
            0.0f,
            20.0f,
            0.0f,
            4.5f,
            0.0f,
            0.0f,
            0.0f
        );
        ASSERT_EQ(slot, i);
        C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, slot, 1.5f);
    }
    for (int i = 0; i < IRComponents::kMaxFogVisionCircles; ++i) {
        const int next = (i + 1) % IRComponents::kMaxFogVisionCircles;
        int distinguishing = 0;
        for (int x = -12; x <= 12; ++x) {
            const vec3 probe(static_cast<float>(x) + 0.25f, 0.25f, kGroundTop);
            const bool expected = clear(field, IRPrefab::Fog::losEye(observers, i), probe);
            ASSERT_EQ(visibility(field, observers, i, probe) > 0.0f, expected)
                << "source " << i << " at x " << x;
            if ((visibility(field, observers, i, probe) > 0.0f) !=
                (visibility(field, observers, next, probe) > 0.0f)) {
                ++distinguishing;
            }
        }
        EXPECT_GT(distinguishing, 0) << "sources " << i << " and " << next << " read identically";
    }
}

// A hard disc's shadow must reach as far as the fog kernel's rim fade, or the
// fade halo reappears behind the shadow: the CPU reach mirrors the constant
// both shared fog reveals and both gate helpers carry.
TEST(FogLineOfSightTest, ReachCoversTheShaderRimFade) {
    for (const char *kernel :
         {"/ir_fog_common.glsl",
          "/metal/ir_fog_common.metal",
          "/ir_fog_los.glsl",
          "/metal/ir_fog_los.metal"}) {
        std::ifstream file(std::string(IR_TEST_RENDER_SHADER_DIR) + kernel);
        std::ostringstream source;
        source << file.rdbuf();
        std::smatch match;
        const std::string text = source.str();
        ASSERT_TRUE(
            std::regex_search(
                text,
                match,
                std::regex(R"(kFog(Los)?RimFadeCells\s*=\s*([0-9.]+)f?;)")
            )
        ) << kernel;
        EXPECT_FLOAT_EQ(std::stof(match[2].str()), IRPrefab::Fog::kFogLosRimFadeCells) << kernel;
    }
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::losReach(vec4(0.0f, 0.0f, 10.0f, 0.0f)),
        10.0f + IRPrefab::Fog::kFogLosRimFadeCells + IRPrefab::Fog::kFogLosDiscMargin
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::losReach(vec4(0.0f, 0.0f, 10.0f, 3.0f)),
        13.0f + IRPrefab::Fog::kFogLosDiscMargin
    ) << "a soft disc has no rim fade";
}

// The shared interior-cell visitor visits exactly the cells a brute-force
// evaluation over a generous box marks interior, clipped to the requested
// box, and the column-top walk keeps each column's highest cell of that set.
TEST(FogLineOfSightTest, InteriorCellVisitorMatchesBruteForce) {
    using IRMath::SDF::ShapeType;
    const struct {
        ShapeType type_;
        vec4 params_;
        vec3 centre_;
    } shapes[] = {
        {ShapeType::BOX, vec4(2.0f, 16.0f, 4.0f, 0.0f), vec3(0.5f, 0.5f, 1.5f)},
        {ShapeType::SPHERE, vec4(3.3f, 3.3f, 3.3f, 0.0f), vec3(-1.2f, 2.7f, -4.0f)},
        {ShapeType::CYLINDER, vec4(2.0f, 2.0f, 7.0f, 0.0f), vec3(4.0f, -3.0f, 0.0f)},
    };
    for (const auto &shape : shapes) {
        for (const bool clipped : {false, true}) {
            const ivec3 clipMin = clipped ? ivec3(-1, -2, -1) : ivec3(-1000);
            const ivec3 clipMax = clipped ? ivec3(2, 3, 2) : ivec3(1000);
            std::vector<ivec3> visited;
            IRMath::SDF::forEachInteriorCell(
                shape.type_,
                shape.params_,
                shape.centre_,
                clipMin,
                clipMax,
                [&](ivec3 cell) { visited.push_back(cell); }
            );
            std::vector<ivec3> expected;
            const vec4 effective = IRMath::SDF::effectiveParams(shape.type_, shape.params_);
            for (int z = -30; z <= 30; ++z) {
                for (int y = -30; y <= 30; ++y) {
                    for (int x = -30; x <= 30; ++x) {
                        const ivec3 cell(x, y, z);
                        if (x < clipMin.x || y < clipMin.y || z < clipMin.z || x > clipMax.x ||
                            y > clipMax.y || z > clipMax.z) {
                            continue;
                        }
                        if (IRMath::SDF::evaluate(
                                vec3(cell) - shape.centre_,
                                shape.type_,
                                effective
                            ) <= IRMath::SDF::kSurfaceThreshold) {
                            expected.push_back(cell);
                        }
                    }
                }
            }
            ASSERT_FALSE(expected.empty());
            EXPECT_EQ(visited, expected);

            std::vector<ivec3> tops;
            IRMath::SDF::forEachInteriorColumnTop(
                shape.type_,
                shape.params_,
                shape.centre_,
                clipMin,
                clipMax,
                [&](ivec3 cell) { tops.push_back(cell); }
            );
            for (const ivec3 &top : tops) {
                for (const ivec3 &cell : visited) {
                    if (cell.x == top.x && cell.y == top.y) {
                        EXPECT_GE(cell.z, top.z) << "a column top above an interior cell";
                    }
                }
            }
            EXPECT_FALSE(tops.empty());
        }
    }
}

namespace {

// The census scene: ground, the ridge, a tall tower, a sunken pit and a
// column on the field's far edge, half of it at negative coordinates.
std::vector<float> censusField(vec2 offset = vec2(0.0f), ivec2 fieldMin = kWorldFieldMin) {
    std::vector<float> field = ridgeField(0.0f, offset, fieldMin);
    const auto box = [&](vec2 minXY, vec2 maxXY, float top) {
        IRPrefab::Fog::stampLosBox(field, fieldMin, minXY + offset, maxXY + offset, top);
    };
    box(vec2(-9.0f, -4.5f), vec2(-7.5f, -3.0f), -9.0f);
    box(vec2(-23.0f, 10.0f), vec2(-21.5f, 16.5f), 1.0f);
    box(vec2(-43.5f, -38.0f), vec2(-42.0f, -36.0f), -2.0f);
    box(vec2(126.5f, -2.0f), vec2(128.0f, 2.0f), 0.0f);
    for (int y = -28; y < -12; ++y) {
        for (int x = 12; x < 32; ++x) {
            const ivec2 cell = ivec2(x, y) + IRMath::ivec2(offset * 2.0f);
            if (FogLosColumnField::cellInField(cell, fieldMin)) {
                field[FogLosColumnField::columnIndex(cell, fieldMin)] = kGroundTop + 5.0f;
            }
        }
    }
    IRPrefab::Fog::buildLosPyramid(field);
    return field;
}

struct CensusSource {
    vec2 centre_;
    float radius_;
    float observerZ_;
    float eyeHeight_;
};

// Census sources: on lattice lines and corners, beside a column (the first
// cell entered at t = 0), low and high eyes, inside the pit, near the field
// edge and far into negative coordinates.
const CensusSource kCensusSources[IRComponents::kMaxFogVisionCircles] = {
    {vec2(-6.0f, 0.0f), 20.0f, kGroundTop, 1.0f},
    {vec2(0.0f, 0.0f), 16.0f, kRidgeTop, 1.5f},
    {vec2(0.5f, -3.0f), 20.0f, kGroundTop, 0.25f},
    {vec2(-20.25f, 13.75f), 18.0f, kGroundTop, 6.0f},
    {vec2(10.3f, -11.1f), 18.0f, kGroundTop + 5.0f, 1.0f},
    {vec2(118.0f, 0.5f), 14.0f, kGroundTop, 1.0f},
    {vec2(1.5f, 2.25f), 20.0f, kGroundTop, 1.0f},
    {vec2(-40.5f, -40.5f), 12.0f, kGroundTop, 2.0f},
};

FrameDataFogObservers censusObservers(vec2 offset = vec2(0.0f)) {
    FrameDataFogObservers observers{};
    for (const CensusSource &source : kCensusSources) {
        const int slot = C_CanvasFogOfWar::addVisionCircle(
            observers,
            source.centre_.x + offset.x,
            source.centre_.y + offset.y,
            source.radius_,
            0.0f,
            source.observerZ_,
            0.0f,
            0.0f,
            0.0f
        );
        C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, slot, source.eyeHeight_);
    }
    return observers;
}

struct CensusResult {
    int compared_ = 0;
    int mismatches_ = 0;
    int decidedVisible_ = 0;
    int decidedHidden_ = 0;
    int decidedBelowEye_ = 0;
    int bandEdges_ = 0;
    int swept_ = 0;
    int sweptUndecided_ = 0;
};

constexpr int kCensusSweepHeights = 25;

// Every height worth probing over @p route from the eye at @p eyeZ: a sweep,
// heights under the target's own column top (lifted onto it), and each finite
// band edge with three representable floats either side and points inside
// the band.
std::vector<float> censusHeights(const IRPrefab::Fog::LosHardRoute &route, float eyeZ, int &edges) {
    std::vector<float> heights;
    for (int i = 0; i < kCensusSweepHeights; ++i) {
        heights.push_back(-14.0f + 1.25f * static_cast<float>(i));
    }
    if (route.ownTop_ != kFogLosColumnEmpty) {
        heights.push_back(route.ownTop_);
        heights.push_back(route.ownTop_ + 0.5f);
        heights.push_back(route.ownTop_ + 7.0f);
    }
    for (const IRPrefab::Fog::LosRiseBand &band : {route.belowEye_, route.atOrAboveEye_}) {
        for (const float edge : {band.clearBelow_, band.blockedFrom_}) {
            if (!(IRMath::abs(edge) < 1.0e6f)) {
                continue;
            }
            ++edges;
            float up = edge + eyeZ;
            float down = up;
            heights.push_back(up);
            for (int step = 0; step < 3; ++step) {
                up = IRMath::nextAfter(up, 1.0e30f);
                down = IRMath::nextAfter(down, -1.0e30f);
                heights.push_back(up);
                heights.push_back(down);
            }
        }
        if (IRMath::abs(band.clearBelow_) < 1.0e6f && IRMath::abs(band.blockedFrom_) < 1.0e6f) {
            for (const float f : {0.25f, 0.5f, 0.75f}) {
                heights.push_back(
                    band.clearBelow_ + (band.blockedFrom_ - band.clearBelow_) * f + eyeZ
                );
            }
        }
    }
    return heights;
}

// Summarize every source over a lattice of target XY (corners, lattice lines
// and fractional pairs inside one half-cell) and compare each summarized
// verdict, with the march standing in where it is undecided, against
// `losVisibility`.
CensusResult runCensus(
    const std::vector<float> &columns,
    ivec2 fieldMin,
    const FrameDataFogObservers &observers,
    vec2 offset
) {
    const FogLosColumnField field{columns.data(), fieldMin};
    const vec2 subCell[] = {
        vec2(0.0f, 0.0f),
        vec2(0.5f, 0.25f),
        vec2(0.13f, 0.37f),
        vec2(0.41f, 0.02f),
        vec2(0.26f, 0.5f),
    };
    CensusResult result;
    for (int source = 0; source < observers.visionCircleCount_; ++source) {
        const vec4 circle = observers.visionCircles_[source];
        const float eyeZ = IRPrefab::Fog::losEye(observers, source).z;
        const int span = static_cast<int>(IRPrefab::Fog::losReach(circle)) + 3;
        for (int j = -span; j <= span; j += 4) {
            for (int i = -span; i <= span; i += 4) {
                for (const vec2 sub : subCell) {
                    const vec2 target = vec2(IRMath::floor(circle.x), IRMath::floor(circle.y)) +
                                        vec2(static_cast<float>(i), static_cast<float>(j)) + sub;
                    const IRPrefab::Fog::LosHardRoute route =
                        IRPrefab::Fog::buildLosHardRoute(field, observers, source, target);
                    const std::vector<float> heights =
                        censusHeights(route, eyeZ, result.bandEdges_);
                    for (std::size_t h = 0; h < heights.size(); ++h) {
                        const float z = heights[h];
                        const vec3 position(target, z);
                        const float marched =
                            IRPrefab::Fog::losVisibility(field, observers, source, position);
                        const int verdict = IRPrefab::Fog::losHardRouteVerdict(route, eyeZ, z);
                        const float summarized =
                            verdict < 0 ? marched : static_cast<float>(verdict);
                        ++result.compared_;
                        if (summarized != marched) {
                            ++result.mismatches_;
                            ADD_FAILURE() << "source " << source << " target (" << target.x << ", "
                                          << target.y << ", " << z << ") offset (" << offset.x
                                          << ", " << offset.y << ") marched " << marched
                                          << " summarized " << summarized;
                        }
                        if (verdict == 1) {
                            ++result.decidedVisible_;
                        } else if (verdict == 0) {
                            ++result.decidedHidden_;
                        }
                        if (verdict >= 0 && IRMath::min(z, route.ownTop_) - eyeZ > 0.0f) {
                            ++result.decidedBelowEye_;
                        }
                        if (h < kCensusSweepHeights) {
                            ++result.swept_;
                            result.sweptUndecided_ += verdict < 0 ? 1 : 0;
                        }
                    }
                }
            }
        }
    }
    return result;
}

} // namespace

// The summarized hard route is the march bit for bit: over eight sources and
// both slope regimes, at the band edges a summary proves and the
// representable floats beside them, every verdict the summary decides is the
// march's, and it decides nearly all of them.
TEST(FogLineOfSightTest, HardRouteSummaryMatchesTheMarchAtEveryHeight) {
    const std::vector<float> field = censusField();
    const CensusResult result = runCensus(field, kWorldFieldMin, censusObservers(), vec2(0.0f));
    EXPECT_EQ(result.mismatches_, 0);
    EXPECT_GT(result.decidedHidden_, 1000);
    EXPECT_GT(result.decidedVisible_, 1000);
    EXPECT_GT(result.decidedBelowEye_, 1000);
    EXPECT_GT(result.bandEdges_, 1000);
    EXPECT_LT(result.sweptUndecided_, result.swept_ / 1000)
        << "the summary left " << result.sweptUndecided_ << " of " << result.swept_
        << " swept heights to the march";
}

// The same census in a field anchored on a window far from the world origin.
TEST(FogLineOfSightTest, HardRouteSummaryMatchesTheMarchOffOrigin) {
    constexpr int kWindowEdge = 1152;
    const vec2 offset(640.0f, -392.0f);
    const ivec2 fieldMin = FogLosColumnField::fieldMinForWindow(
        IRPrefab::Fog::detail::windowOriginForCentre(offset, kWindowEdge),
        kWindowEdge
    );
    const std::vector<float> field = censusField(offset, fieldMin);
    const CensusResult result = runCensus(field, fieldMin, censusObservers(offset), offset);
    EXPECT_EQ(result.mismatches_, 0);
    EXPECT_GT(result.decidedHidden_, 1000);
    EXPECT_GT(result.decidedVisible_, 1000);
}

// A source whose eye stands on a lattice line beside a column taller than the
// eye: the first cell the walk enters, at t = 0, hides every sample at or
// above the eye's height behind it, and the summary decides those samples
// hidden without a band. An unpublished field hides everything; past the
// reach everything is visible.
TEST(FogLineOfSightTest, HardRouteSummaryConstantVerdicts) {
    const std::vector<float> columns = censusField();
    const FogLosColumnField field{columns.data(), kWorldFieldMin};
    const FrameDataFogObservers observers = censusObservers();
    constexpr int kBesideRidge = 6;
    const float eyeZ = IRPrefab::Fog::losEye(observers, kBesideRidge).z;
    const vec2 behind(-4.0f, 2.25f);
    const IRPrefab::Fog::LosHardRoute route =
        IRPrefab::Fog::buildLosHardRoute(field, observers, kBesideRidge, behind);
    EXPECT_EQ(route.atOrAboveEye_.blockedFrom_, -std::numeric_limits<float>::infinity());
    for (const float z : {eyeZ, eyeZ - 3.0f, -40.0f}) {
        EXPECT_EQ(IRPrefab::Fog::losHardRouteVerdict(route, eyeZ, z), 0);
        EXPECT_EQ(
            IRPrefab::Fog::losVisibility(field, observers, kBesideRidge, vec3(behind, z)),
            0.0f
        );
    }

    const IRPrefab::Fog::LosHardRoute unpublished =
        IRPrefab::Fog::buildLosHardRoute(FogLosColumnField{}, observers, 0, behind);
    const IRPrefab::Fog::LosHardRoute beyond =
        IRPrefab::Fog::buildLosHardRoute(field, observers, 0, vec2(60.0f, 0.0f));
    for (const float z : {-20.0f, 0.0f, 3.0f, 20.0f}) {
        EXPECT_EQ(IRPrefab::Fog::losHardRouteVerdict(unpublished, 3.0f, z), 0);
        EXPECT_EQ(IRPrefab::Fog::losHardRouteVerdict(beyond, 3.0f, z), 1);
    }

    // A band is a promise only at its edges: a rise inside it is undecided.
    IRPrefab::Fog::LosHardRoute undecided;
    undecided.belowEye_ = {-1.0f, 2.0f};
    undecided.atOrAboveEye_ = {-1.0f, 2.0f};
    EXPECT_EQ(IRPrefab::Fog::losHardRouteVerdict(undecided, 0.0f, -1.5f), 1);
    EXPECT_EQ(IRPrefab::Fog::losHardRouteVerdict(undecided, 0.0f, -1.0f), -1);
    EXPECT_EQ(IRPrefab::Fog::losHardRouteVerdict(undecided, 0.0f, 0.0f), -1);
    EXPECT_EQ(IRPrefab::Fog::losHardRouteVerdict(undecided, 0.0f, 1.0f), -1);
    EXPECT_EQ(IRPrefab::Fog::losHardRouteVerdict(undecided, 0.0f, 2.0f), 0);
}

class FogLineOfSightEcsTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entityManager{};
    C_VoxelPool m_pool{ivec3(4, 4, 4)};
    std::vector<float> m_field = emptyField();
    ivec2 m_fieldMin = kWorldFieldMin;
    LosRasterFrame m_frame{};

    IREntity::EntityId makeWall(bool blocksLos, vec3 centre = vec3(0.5f, 0.5f, 1.5f)) {
        C_ShapeDescriptor shape{
            IRMath::SDF::ShapeType::BOX,
            vec4(2.0f, 16.0f, 4.0f, 0.0f),
            IRMath::Color{255, 255, 255, 255}
        };
        return IREntity::createEntity(
            shape,
            C_LightBlocker{blocksLos, false, 1.0f},
            C_WorldTransform{centre, vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)}
        );
    }

    void rasterize() {
        IRPrefab::Fog::rasterizeLosColumns(
            m_pool,
            IREntity::kNullEntity,
            m_frame,
            m_fieldMin,
            m_field
        );
    }

    float top(int halfCellX, int halfCellY) const {
        return topAt(m_field, halfCellX, halfCellY, m_fieldMin);
    }
};

// A flagged box is stamped at the surface the shape raster draws: at
// subdivision 1 its cardinal-snapped origin (1, 1, 2) with half extents
// (1, 8, 2), so x 0..2, y -7..9 and a top plane at 0.
TEST_F(FogLineOfSightEcsTest, ShapeOccludesOnlyWhenFlagged) {
    const IREntity::EntityId wall = makeWall(true);
    rasterize();
    EXPECT_FLOAT_EQ(top(0, 0), 0.0f);
    EXPECT_FLOAT_EQ(top(3, 17), 0.0f);
    EXPECT_FLOAT_EQ(top(-1, 0), kFogLosColumnEmpty);
    EXPECT_FLOAT_EQ(top(4, 0), kFogLosColumnEmpty);
    EXPECT_FLOAT_EQ(top(12, 0), kFogLosColumnEmpty);

    IREntity::getComponent<C_LightBlocker>(wall).blocksLOS_ = false;
    rasterize();
    EXPECT_FLOAT_EQ(top(0, 0), kFogLosColumnEmpty) << "blocksLOS_ = false must not occlude";

    IREntity::getComponent<C_LightBlocker>(wall).blocksLOS_ = true;
    IREntity::getComponent<C_ShapeDescriptor>(wall).canvasEntity_ = 12345;
    rasterize();
    EXPECT_FLOAT_EQ(top(0, 0), kFogLosColumnEmpty) << "a shape on another canvas must not occlude";
}

// At a higher subdivision the drawn box shrinks to half a micro cell past its
// voxel centres, and the stamp follows to the nearest half-cell edge.
TEST_F(FogLineOfSightEcsTest, ShapeFootprintFollowsTheSubdivision) {
    makeWall(true);
    m_frame.subdivisions_ = 8;
    rasterize();
    EXPECT_FLOAT_EQ(top(1, 0), 2.0f - 1.5f - 0.0625f)
        << "top plane at origin z 2 minus half 1.5625";
    EXPECT_FLOAT_EQ(top(2, 0), 2.0f - 1.5f - 0.0625f);
    EXPECT_FLOAT_EQ(top(0, 0), kFogLosColumnEmpty) << "x 0.4375..1.5625 rounds to half-cells 1..2";
    EXPECT_FLOAT_EQ(top(3, 0), kFogLosColumnEmpty);
}

// Moving, toggling and removing a blocker between builds leaves no stale
// occlusion: each build reads only the current frame's occluders.
TEST_F(FogLineOfSightEcsTest, BlockerChangesLeaveNoStaleOcclusion) {
    const FrameDataFogObservers observers =
        gatedSources({vec4(-6.0f, 0.0f, 14.0f, 0.0f)}, 4.5f, 1.5f);
    const vec3 behind(6.0f, 0.5f, 4.0f);
    const auto farSideVisible = [&]() {
        rasterize();
        return visibility(m_field, observers, 0, behind) > 0.0f;
    };

    const IREntity::EntityId wall = makeWall(true);
    EXPECT_FALSE(farSideVisible());
    IREntity::getComponent<C_WorldTransform>(wall).translation_ = vec3(0.5f, 40.5f, 1.5f);
    EXPECT_TRUE(farSideVisible()) << "a moved blocker left its old shadow behind";
    IREntity::getComponent<C_WorldTransform>(wall).translation_ = vec3(0.5f, 0.5f, 1.5f);
    EXPECT_FALSE(farSideVisible());
    IREntity::getComponent<C_LightBlocker>(wall).blocksLOS_ = false;
    EXPECT_TRUE(farSideVisible()) << "an unflagged blocker left its shadow behind";
    IREntity::getComponent<C_LightBlocker>(wall).blocksLOS_ = true;
    EXPECT_FALSE(farSideVisible());
    m_entityManager.destroyEntity(wall);
    EXPECT_TRUE(farSideVisible()) << "a destroyed blocker left its shadow behind";
}

// Pool voxels stamp the box the raster draws them as; carved (alpha 0) and
// governed (kFogWholeBodyExempt) voxels never occlude.
TEST_F(FogLineOfSightEcsTest, PoolVoxelsOccludeExceptCarvedAndGoverned) {
    IRRender::VoxelPoolAllocation allocation = m_pool.allocateVoxels(3);
    allocation.positionGlobals_[0].pos_ = vec3(2.4f, -0.5f, -3.5f);
    allocation.positionGlobals_[1].pos_ = vec3(5.0f, 5.0f, -8.0f);
    allocation.positionGlobals_[2].pos_ = vec3(7.0f, 7.0f, -9.0f);
    allocation.voxels_[0].color_.alpha_ = 255;
    allocation.voxels_[1].color_.alpha_ = 0;
    allocation.voxels_[2].color_.alpha_ = 255;
    allocation.voxels_[2].reserved_ |= IRComponents::VoxelReserved::kFogWholeBodyExempt;
    rasterize();
    EXPECT_FLOAT_EQ(top(4, 0), -3.0f)
        << "at subdivision 1 the voxel snaps to (2, 0, -3) and fills x 2..3, y 0..1";
    EXPECT_FLOAT_EQ(top(5, 1), -3.0f);
    EXPECT_FLOAT_EQ(top(3, 0), kFogLosColumnEmpty);
    EXPECT_FLOAT_EQ(top(10, 10), kFogLosColumnEmpty) << "a carved voxel must not occlude";
    EXPECT_FLOAT_EQ(top(14, 14), kFogLosColumnEmpty) << "a governed voxel must not occlude";
}

// The raster stamps into the field anchored with the window: a voxel far from
// the origin falls outside the world-centred field and lands in a field
// anchored on a window centred near it.
TEST_F(FogLineOfSightEcsTest, RasterFillsTheWindowAnchoredField) {
    IRRender::VoxelPoolAllocation allocation = m_pool.allocateVoxels(1);
    allocation.positionGlobals_[0].pos_ = vec3(642.0f, -390.0f, -3.0f);
    allocation.voxels_[0].color_.alpha_ = 255;
    rasterize();
    EXPECT_TRUE(std::all_of(m_field.begin(), m_field.end(), [](float columnTop) {
        return columnTop == kFogLosColumnEmpty;
    })) << "a voxel outside the world-centred field stamped into it";

    constexpr int kWindowEdge = 1152;
    m_fieldMin = FogLosColumnField::fieldMinForWindow(
        IRPrefab::Fog::detail::windowOriginForCentre(vec2(640.0f, -392.0f), kWindowEdge),
        kWindowEdge
    );
    rasterize();
    EXPECT_FLOAT_EQ(top(1284, -780), -3.0f) << "the voxel fills x 642..643, y -390..-389";
    EXPECT_FLOAT_EQ(top(1285, -779), -3.0f);
    EXPECT_FLOAT_EQ(top(1283, -780), kFogLosColumnEmpty);
    EXPECT_FLOAT_EQ(top(1284, -778), kFogLosColumnEmpty);
}

// A captured view answers every pair exactly as a per-call rebuild over the
// same occluders, and is a snapshot: after an occluder changes it keeps the
// pre-change verdict (and so does every copy) until it is recaptured, and a
// recapture never reaches a copy. Covers a moved flagged wall and a pool voxel
// whose whole-body exemption is set between queries.
TEST_F(FogLineOfSightEcsTest, CapturedViewMatchesPerCallAndIsASnapshot) {
    const IREntity::EntityId wall = makeWall(true);
    IRRender::VoxelPoolAllocation allocation = m_pool.allocateVoxels(2);
    allocation.positionGlobals_[0].pos_ = vec3(-3.0f, 12.0f, -3.0f);
    allocation.positionGlobals_[1].pos_ = vec3(9.0f, -20.0f, -6.0f);
    allocation.voxels_[0].color_.alpha_ = 255;
    allocation.voxels_[1].color_.alpha_ = 255;

    const auto oracle = [&](vec3 from, vec3 to) {
        rasterize();
        return IRPrefab::Fog::losPointVisible(
            FogLosColumnField{m_field.data(), m_fieldMin},
            from,
            to
        );
    };
    IRPrefab::Fog::LineOfSightView view;
    const auto capture = [&]() {
        view.capture(m_pool, IREntity::kNullEntity, m_frame, m_fieldMin);
    };

    const vec3 wallEye(-6.0f, 0.5f, 3.0f);
    const vec3 wallShadow(6.0f, 0.5f, 4.0f);
    const vec3 voxelEye(-6.0f, 12.5f, 3.0f);
    const vec3 voxelShadow(0.0f, 12.5f, 4.0f);
    const struct {
        vec3 from_;
        vec3 to_;
    } pairs[] = {
        {wallEye, wallShadow},
        {wallEye, vec3(-3.0f, 0.5f, 4.0f)},
        {wallEye, vec3(6.0f, 20.0f, 4.0f)},
        {voxelEye, voxelShadow},
        {vec3(4.0f, -24.0f, 3.0f), vec3(14.0f, -16.0f, 4.0f)},
        {vec3(0.1f, 30.1f, 3.0f), vec3(0.2f, 30.2f, 5.0f)},
        {wallEye, vec3(200.0f, 0.5f, 4.0f)},
    };

    IRPrefab::Fog::LineOfSightView empty;
    capture();
    int blocked = 0;
    int clear = 0;
    for (const auto &pair : pairs) {
        const bool expected = oracle(pair.from_, pair.to_);
        EXPECT_EQ(view.visible(pair.from_, pair.to_), expected)
            << "from " << pair.from_.x << "," << pair.from_.y << " to " << pair.to_.x << ","
            << pair.to_.y;
        EXPECT_TRUE(empty.visible(pair.from_, pair.to_)) << "an empty view must answer true";
        (expected ? clear : blocked) += 1;
    }
    EXPECT_GE(blocked, 1);
    EXPECT_GE(clear, 1);

    const auto expectSnapshotFlip = [&](vec3 from, vec3 to, const auto &changeOccluders) {
        ASSERT_FALSE(oracle(from, to));
        capture();
        const IRPrefab::Fog::LineOfSightView copy = view;
        changeOccluders();
        EXPECT_FALSE(copy.visible(from, to)) << "a copy must keep its snapshot";
        EXPECT_FALSE(view.visible(from, to)) << "a view must not change until it is recaptured";
        EXPECT_TRUE(oracle(from, to)) << "the change must flip the per-call verdict";
        capture();
        EXPECT_TRUE(view.visible(from, to)) << "a recapture must read the change";
        EXPECT_FALSE(copy.visible(from, to)) << "a recapture must not reach a copy";
    };
    expectSnapshotFlip(wallEye, wallShadow, [&]() {
        IREntity::getComponent<C_WorldTransform>(wall).translation_ = vec3(0.5f, 40.5f, 1.5f);
    });
    expectSnapshotFlip(voxelEye, voxelShadow, [&]() {
        allocation.voxels_[0].reserved_ |= IRComponents::VoxelReserved::kFogWholeBodyExempt;
    });
}

// Many queries against one capture rasterize once; recapturing an unshared view
// reuses its snapshot, and recapturing while a copy holds it starts a new one.
TEST_F(FogLineOfSightEcsTest, CapturedViewRasterizesOncePerCapture) {
    makeWall(true);
    constexpr int kQueries = 128;
    IRPrefab::Fog::LineOfSightView view;
    EXPECT_EQ(view.rasterizeCount(), 0u);
    view.capture(m_pool, IREntity::kNullEntity, m_frame, m_fieldMin);
    int blocked = 0;
    for (int i = 0; i < kQueries; ++i) {
        const float y = -16.0f + 32.0f * static_cast<float>(i) / static_cast<float>(kQueries);
        blocked += view.visible(vec3(-6.0f, y, 3.0f), vec3(6.0f, y, 4.0f)) ? 0 : 1;
    }
    EXPECT_GT(blocked, 0);
    EXPECT_LT(blocked, kQueries);
    EXPECT_EQ(view.rasterizeCount(), 1u);

    for (int i = 1; i < kQueries; ++i) {
        view.capture(m_pool, IREntity::kNullEntity, m_frame, m_fieldMin);
    }
    EXPECT_EQ(view.rasterizeCount(), static_cast<std::uint64_t>(kQueries));

    const IRPrefab::Fog::LineOfSightView copy = view;
    view.capture(m_pool, IREntity::kNullEntity, m_frame, m_fieldMin);
    EXPECT_EQ(view.rasterizeCount(), 1u);
    EXPECT_EQ(copy.rasterizeCount(), static_cast<std::uint64_t>(kQueries));
}

// Slot authoring: addVisionCircle hands back the slot it filled (or -1), a new
// slot starts ungated with the hard gate, and clearing drops every gate.
TEST(FogVisionSlotTest, SlotsStartUngatedAndClearDropsGates) {
    FrameDataFogObservers observers{};
    EXPECT_EQ(C_CanvasFogOfWar::addVisionCircle(observers, 0, 0, 0.0f, 0, 0, 0, -1, 0), -1)
        << "a non-positive radius is rejected";
    for (int i = 0; i < IRComponents::kMaxFogVisionCircles; ++i) {
        EXPECT_EQ(C_CanvasFogOfWar::addVisionCircle(observers, 0, 0, 5, 0, 0, 0, -1, 0), i);
    }
    EXPECT_EQ(C_CanvasFogOfWar::addVisionCircle(observers, 0, 0, 5, 0, 0, 0, -1, 0), -1)
        << "past the cap is rejected";
    EXPECT_EQ(observers.losSourceMask_, 0);

    C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 3, 1.5f);
    C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 5, 0.0f, 0.75f);
    EXPECT_EQ(observers.losSourceMask_, (1 << 3) | (1 << 5));
    EXPECT_FLOAT_EQ(observers.losEyeHeight(3), 1.5f);
    EXPECT_FLOAT_EQ(observers.losSoftness(3), kFogLosHardGate);
    EXPECT_FLOAT_EQ(observers.losEyeHeight(5), 0.0f);
    EXPECT_FLOAT_EQ(observers.losSoftness(5), 0.75f);
    C_CanvasFogOfWar::setVisionCircleLineOfSight(
        observers,
        5,
        IRComponents::kFogVisionLosOff,
        0.75f
    );
    EXPECT_EQ(observers.losSourceMask_, 1 << 3);
    EXPECT_FLOAT_EQ(observers.losSoftness(5), kFogLosHardGate)
        << "ungating a slot resets its softness";
    C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 3, 1.5f, -2.0f);
    EXPECT_FLOAT_EQ(observers.losSoftness(3), kFogLosHardGate) << "a negative softness is hard";

    C_CanvasFogOfWar::clearVisionCircles(observers);
    EXPECT_EQ(observers.losSourceMask_, 0);
    EXPECT_EQ(observers.visionCircleCount_, 0);
    for (int i = 0; i < 4; ++i) {
        C_CanvasFogOfWar::addVisionCircle(observers, 0, 0, 5, 0, 0, 0, -1, 0);
    }
    EXPECT_EQ(observers.losSourceMask_, 0) << "a re-added slot 3 inherited a stale gate";
    EXPECT_FLOAT_EQ(observers.losEyeHeight(3), IRComponents::kFogVisionLosOff);
}

// Gating an unregistered slot is a caller bug: it asserts in debug and, in a
// release build, leaves the gates untouched.
TEST(FogVisionSlotTest, GatingAnUnregisteredSlotIsRejected) {
    FrameDataFogObservers observers{};
    C_CanvasFogOfWar::addVisionCircle(observers, 0, 0, 5, 0, 0, 0, -1, 0);
#ifndef IR_RELEASE
    EXPECT_THROW(
        C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 1, 1.5f),
        std::runtime_error
    );
    EXPECT_THROW(
        C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, -1, 1.5f),
        std::runtime_error
    );
#else
    C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 1, 1.5f);
#endif
    EXPECT_EQ(observers.losSourceMask_, 0);
    EXPECT_FLOAT_EQ(observers.losEyeHeight(1), 0.0f) << "a rejected call writes nothing";
}
