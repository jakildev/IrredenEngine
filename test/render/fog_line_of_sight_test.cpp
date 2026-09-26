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

#include <cstddef>
#include <fstream>
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

void stampVoxel(std::vector<float> &field, vec3 position, const LosRasterFrame &frame) {
    vec3 boxMin;
    vec3 boxMax;
    IRPrefab::Fog::losVoxelBox(position, frame, boxMin, boxMax);
    IRPrefab::Fog::stampLosBox(field, vec2(boxMin), vec2(boxMax), boxMin.z);
    IRPrefab::Fog::buildLosPyramid(field);
}

float topAt(const std::vector<float> &field, int halfCellX, int halfCellY) {
    return FogLosColumnField{field.data()}.topPlane(halfCellX, halfCellY);
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

std::vector<float> ridgeField(float shift = 0.0f) {
    std::vector<float> field = flatField(kGroundTop + shift);
    const LosRasterFrame frame = subdividedFrame();
    for (int row = -7; row <= 8; ++row) {
        const float y = static_cast<float>(row) - 0.5f;
        stampVoxel(field, vec3(-0.5f, y, kRidgeTop + shift), frame);
        stampVoxel(field, vec3(0.5f, y, kRidgeTop + shift), frame);
    }
    stampVoxel(field, vec3(-2.0f, 5.0f, kGroundTop - 10.0f + shift), frame);
    return field;
}

bool clear(const std::vector<float> &field, vec3 eye, vec3 target) {
    float bandClearance = 0.0f;
    const float minClearance = IRPrefab::Fog::traceLosClearance(
        FogLosColumnField{field.data()},
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
    vec3 position
) {
    return IRPrefab::Fog::losVisibility(
        FogLosColumnField{field.data()},
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
    IRPrefab::Fog::stampLosBox(field, vec2(-2.5f, -3.5f), vec2(-2.0f, -3.0f), -20.0f);
    IRPrefab::Fog::buildLosPyramid(field);
    EXPECT_TRUE(clear(field, eye, vec3(3.0f, -3.4f, kGroundTop)))
        << "the eye's own half-cell occluded the ray";

    IRPrefab::Fog::stampLosBox(field, vec2(-2.0f, -3.5f), vec2(-1.5f, -3.0f), -20.0f);
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

class FogLineOfSightEcsTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entityManager{};
    C_VoxelPool m_pool{ivec3(4, 4, 4)};
    std::vector<float> m_field = emptyField();
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
        IRPrefab::Fog::rasterizeLosColumns(m_pool, IREntity::kNullEntity, m_frame, m_field);
    }

    float top(int halfCellX, int halfCellY) const {
        return topAt(m_field, halfCellX, halfCellY);
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
