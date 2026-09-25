// FogLineOfSight exercises the CPU half of the fog line-of-sight model
// (component_canvas_fog_of_war.hpp states it; fog_line_of_sight.hpp implements
// it): the column rasteriser, the supercover horizon trace, the per-source
// horizon build and the field gate the reveal oracle reads. Headless — the
// builder works on plain vectors, and the shape rasteriser needs only an
// EntityManager.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/math/sdf.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/fog_line_of_sight.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <cstddef>
#include <cstdint>
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
using IRComponents::FogLineOfSightField;
using IRComponents::FogLosEyeHeights;
using IRComponents::FrameDataFogObservers;
using IRComponents::kFogLosColumnEmpty;
using IRComponents::kFogLosHorizonClear;
using IRComponents::kFogOfWarHalfExtent;
using IRMath::ivec2;
using IRMath::ivec3;
using IRMath::vec2;
using IRMath::vec3;
using IRMath::vec4;

std::vector<std::int32_t> emptyColumns() {
    return std::vector<std::int32_t>(IRComponents::kFogLosColumnCount, kFogLosColumnEmpty);
}

void setColumn(std::vector<std::int32_t> &columns, int x, int y, int top) {
    columns[C_CanvasFogOfWar::flatIndex(x, y)] = top;
}

std::vector<std::int32_t> flatColumns(int top) {
    std::vector<std::int32_t> columns(IRComponents::kFogLosColumnCount, top);
    return columns;
}

// The shared scene: flat ground at `groundTop`, a ridge four voxels above it
// at x 0..1, y -7..8 (the fog_demo --occlusion ridge), and a free-standing
// tower at (-2, 5) ten voxels above the ground.
constexpr int kGroundTop = 4;

std::vector<std::int32_t> ridgeColumns(int shift = 0) {
    std::vector<std::int32_t> columns = flatColumns(kGroundTop + shift);
    for (int y = -7; y <= 8; ++y) {
        for (int x = 0; x <= 1; ++x) {
            setColumn(columns, x, y, kGroundTop - 4 + shift);
        }
    }
    setColumn(columns, -2, 5, kGroundTop - 10 + shift);
    return columns;
}

float horizon(const std::vector<std::int32_t> &columns, vec3 eye, ivec2 target) {
    return IRPrefab::Fog::traceLosHorizon(columns, eye, target);
}

bool visible(const std::vector<std::int32_t> &columns, vec3 eye, ivec3 sample) {
    return static_cast<float>(sample.z) <= horizon(columns, eye, ivec2(sample));
}

FrameDataFogObservers gatedSources(std::initializer_list<vec4> circles, float observerZ) {
    FrameDataFogObservers observers{};
    for (const vec4 &circle : circles) {
        const int slot = observers.visionCircleCount_++;
        observers.visionCircles_[slot] = circle;
        observers.visionCircleHeights_[slot] = vec4(observerZ, 0.0f, 0.0f, 0.0f);
        observers.losSourceMask_ |= 1 << slot;
    }
    return observers;
}

std::vector<float> buildField(
    const FrameDataFogObservers &observers,
    const FogLosEyeHeights &eyes,
    const std::vector<std::int32_t> &columns
) {
    std::vector<float> horizons(IRComponents::kFogLosHorizonCount, 0.0f);
    IRPrefab::Fog::buildLosHorizons(observers, eyes, columns, horizons);
    return horizons;
}

FogLosEyeHeights eyesOf(float height) {
    FogLosEyeHeights eyes{};
    eyes.fill(height);
    return eyes;
}

} // namespace

// Flat ground never hides itself: every in-disc top voxel (the height a top-face
// pixel recovers exactly) passes, across fractional eye heights and centres
// (one on a negative half-integer) and integer translations of the whole scene.
// Mutation control: adding a `- 0.5f` surface offset to the column top in
// `traceLosHorizon` fails this beyond the near cells.
TEST(FogLineOfSightTest, FlatSlabRemainsVisible) {
    constexpr float kRadius = 32.0f;
    int probed = 0;
    for (const int top : {4, 0, -1024, 1024}) {
        const std::vector<std::int32_t> columns = flatColumns(top);
        for (const float eyeHeight : {1.5f, 0.5f, 0.73f, 2.25f, 0.0f}) {
            for (const vec2 centre : {vec2(0.0f), vec2(0.37f, -0.61f), vec2(-2.5f, -3.5f)}) {
                const FrameDataFogObservers observers =
                    gatedSources({vec4(centre, kRadius, 0.0f)}, static_cast<float>(top));
                const FogLosEyeHeights eyes = eyesOf(eyeHeight);
                const std::vector<float> horizons = buildField(observers, eyes, columns);
                const FogLineOfSightField field{horizons.data()};
                const vec3 eye(centre, static_cast<float>(top) - eyeHeight);
                for (int y = -40; y <= 40; ++y) {
                    for (int x = -40; x <= 40; ++x) {
                        if (IRMath::length(vec2(x, y) - centre) > kRadius) {
                            continue;
                        }
                        ++probed;
                        ASSERT_TRUE(visible(columns, eye, ivec3(x, y, top)))
                            << "flat ground hid its own top voxel at (" << x << ", " << y
                            << ") top " << top << " eye height " << eyeHeight;
                        ASSERT_TRUE(field.visible(0, ivec3(x, y, top)))
                            << "the built field hid flat ground at (" << x << ", " << y << ")";
                    }
                }
            }
        }
    }
    EXPECT_GT(probed, 0);
}

// The eye's cell is `roundHalfUp` of the eye: at (-2.5, -3.5) that is (-2, -3),
// not std::round's (-3, -4). A tower in the eye's own cell never occludes.
TEST(FogLineOfSightTest, NegativeHalfIntegerEyeRoundsHalfUp) {
    std::vector<std::int32_t> columns = flatColumns(kGroundTop);
    setColumn(columns, -2, -3, -20);
    const vec3 eye(-2.5f, -3.5f, 2.0f);
    EXPECT_TRUE(visible(columns, eye, ivec3(3, -3, kGroundTop)))
        << "the eye's own (round-half-up) cell occluded the ray";

    setColumn(columns, -1, -3, -20);
    EXPECT_FALSE(visible(columns, eye, ivec3(3, -3, kGroundTop)))
        << "control: a tower one cell along the ray must occlude";
}

TEST(FogLineOfSightTest, SameCellAndOutOfFieldAreClear) {
    const std::vector<std::int32_t> columns = flatColumns(-100);
    EXPECT_EQ(horizon(columns, vec3(3.2f, 4.4f, 0.0f), ivec2(3, 4)), kFogLosHorizonClear);

    const std::vector<float> horizons(IRComponents::kFogLosHorizonCount, -1000.0f);
    const FogLineOfSightField field{horizons.data()};
    EXPECT_TRUE(field.visible(0, ivec3(kFogOfWarHalfExtent, 0, 0)));
    EXPECT_TRUE(field.visible(7, ivec3(0, -kFogOfWarHalfExtent - 1, 0)));
    EXPECT_FALSE(field.visible(7, ivec3(0, -kFogOfWarHalfExtent, 0)));
    EXPECT_FALSE(FogLineOfSightField{}.visible(0, ivec3(0))) << "an unpublished field is closed";
}

// An exact corner crossing visits both side cells: a tower on either side of
// the diagonal occludes the target.
TEST(FogLineOfSightTest, CornerTieVisitsBothSideCells) {
    const vec3 eye(0.0f, 0.0f, 2.0f);
    const ivec3 target(2, 2, kGroundTop);
    EXPECT_TRUE(visible(emptyColumns(), eye, target));
    for (const ivec2 side : {ivec2(1, 0), ivec2(0, 1), ivec2(2, 1), ivec2(1, 2)}) {
        std::vector<std::int32_t> columns = emptyColumns();
        setColumn(columns, side.x, side.y, -20);
        EXPECT_FALSE(visible(columns, eye, target))
            << "side cell (" << side.x << ", " << side.y << ") of the corner tie was skipped";
    }
    std::vector<std::int32_t> offRay = emptyColumns();
    setColumn(offRay, 2, 0, -20);
    EXPECT_TRUE(visible(offRay, eye, target)) << "control: a tower off the ray must not occlude";
}

// The ridge scene: ground behind the ridge hides from a ground observer, the
// near ground and a tower top above the ridge's shadow line stay visible, and
// the same far ground reveals from the ridge top.
TEST(FogLineOfSightTest, RidgeHidesWhatIsBehindItFromTheGround) {
    const std::vector<std::int32_t> columns = ridgeColumns();
    const vec3 groundEye(-6.0f, 0.0f, 3.0f);
    EXPECT_TRUE(visible(columns, groundEye, ivec3(-3, 0, kGroundTop)));
    EXPECT_TRUE(visible(columns, groundEye, ivec3(0, 0, kGroundTop - 4)))
        << "the ridge's near top voxel faces the eye";
    EXPECT_FALSE(visible(columns, groundEye, ivec3(6, 0, kGroundTop)));
    EXPECT_TRUE(visible(columns, groundEye, ivec3(6, 0, -10)))
        << "a tower top above the ridge's shadow line stays visible";

    const vec3 ridgeEye(0.0f, 0.0f, -1.0f);
    EXPECT_TRUE(visible(columns, ridgeEye, ivec3(6, 0, kGroundTop)));
    EXPECT_FALSE(visible(columns, ridgeEye, ivec3(2, 0, kGroundTop)))
        << "the strip at the ridge's base is in its own shadow";

    EXPECT_TRUE(visible(flatColumns(kGroundTop), groundEye, ivec3(6, 0, kGroundTop)))
        << "control: without the ridge the far ground is visible";
}

// `lineOfSight`'s core — the trace over the same columns — agrees with the
// built field at every finite horizon: floor(H) visible, floor(H) + 1 hidden,
// and the stored value is the trace's exact float. Across Z translations.
TEST(FogLineOfSightTest, PointQueryAgreesWithFieldAtCellCentres) {
    for (const int shift : {0, -1024, 1024}) {
        const std::vector<std::int32_t> columns = ridgeColumns(shift);
        const FrameDataFogObservers observers = gatedSources(
            {vec4(-6.0f, 0.0f, 14.0f, 0.0f), vec4(0.0f, 0.0f, 14.0f, 0.0f)},
            static_cast<float>(kGroundTop + shift) + 0.5f
        );
        FogLosEyeHeights eyes = eyesOf(2.0f);
        eyes[1] = 1.5f;
        const std::vector<float> horizons = buildField(observers, eyes, columns);
        const FogLineOfSightField field{horizons.data()};
        int finite = 0;
        for (int source = 0; source < 2; ++source) {
            const vec3 eye = IRPrefab::Fog::losEye(observers, eyes, source);
            const vec4 circle = observers.visionCircles_[source];
            const float reach = IRPrefab::Fog::losBuildReach(circle);
            for (int y = -20; y <= 20; ++y) {
                for (int x = -20; x <= 20; ++x) {
                    if (IRMath::length(vec2(x, y) - vec2(circle)) > reach) {
                        continue;
                    }
                    const float stored = horizons[FogLineOfSightField::horizonIndex(source, x, y)];
                    ASSERT_EQ(stored, horizon(columns, eye, ivec2(x, y)))
                        << "field and point trace disagree at (" << x << ", " << y << ")";
                    if (stored == kFogLosHorizonClear) {
                        continue;
                    }
                    ++finite;
                    const int floorZ = static_cast<int>(IRMath::floor(stored));
                    EXPECT_TRUE(field.visible(source, ivec3(x, y, floorZ)));
                    EXPECT_FALSE(field.visible(source, ivec3(x, y, floorZ + 1)));
                    EXPECT_TRUE(visible(columns, eye, ivec3(x, y, floorZ)));
                    EXPECT_FALSE(visible(columns, eye, ivec3(x, y, floorZ + 1)));
                }
            }
        }
        EXPECT_GT(finite, 0) << "the fixture has no finite horizon";
    }
}

// Every lane of the RGBA32F tiles is addressed independently: each of the
// eight sources sits at its own spot, and at least one probe per lane differs
// from the next lane's verdict, so a swapped channel or tile fails here.
TEST(FogLineOfSightTest, EightSourcesUseIndependentLanes) {
    const std::vector<std::int32_t> columns = ridgeColumns();
    FrameDataFogObservers observers{};
    FogLosEyeHeights eyes{};
    for (int i = 0; i < IRComponents::kMaxFogVisionCircles; ++i) {
        const float x = i % 2 == 0 ? -6.0f - static_cast<float>(i) : 7.0f + static_cast<float>(i);
        const int slot = C_CanvasFogOfWar::addVisionCircle(
            observers,
            eyes,
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
        C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, eyes, slot, 1.5f);
    }
    const std::vector<float> horizons = buildField(observers, eyes, columns);
    const FogLineOfSightField field{horizons.data()};
    for (int i = 0; i < IRComponents::kMaxFogVisionCircles; ++i) {
        const int next = (i + 1) % IRComponents::kMaxFogVisionCircles;
        int distinguishing = 0;
        for (int x = -12; x <= 12; ++x) {
            const ivec3 probe(x, 0, kGroundTop);
            const bool expected =
                visible(columns, IRPrefab::Fog::losEye(observers, eyes, i), probe);
            ASSERT_EQ(field.visible(i, probe), expected) << "lane " << i << " at x " << x;
            if (field.visible(i, probe) != field.visible(next, probe)) {
                ++distinguishing;
            }
        }
        EXPECT_GT(distinguishing, 0) << "lanes " << i << " and " << next << " read identically";
    }
}

// Ungated sources and cells past the build reach read clear.
TEST(FogLineOfSightTest, UngatedTilesAndCellsBeyondReachAreClear) {
    const std::vector<std::int32_t> columns = ridgeColumns();
    FrameDataFogObservers observers =
        gatedSources({vec4(-6.0f, 0.0f, 10.0f, 0.0f), vec4(-6.0f, 0.0f, 10.0f, 0.0f)}, 4.5f);
    observers.losSourceMask_ = 0b10;
    const std::vector<float> horizons = buildField(observers, eyesOf(1.5f), columns);
    const FogLineOfSightField field{horizons.data()};
    EXPECT_TRUE(field.visible(0, ivec3(6, 0, kGroundTop))) << "an ungated tile must be clear";
    EXPECT_FALSE(field.visible(1, ivec3(6, 0, kGroundTop)));
    const int beyond =
        static_cast<int>(IRPrefab::Fog::losBuildReach(vec4(-6.0f, 0.0f, 10.0f, 0.0f))) + 2;
    EXPECT_EQ(horizons[FogLineOfSightField::horizonIndex(1, -6 + beyond, 0)], kFogLosHorizonClear);
}

// A hard disc's shadow must reach as far as the fog kernel's rim fade, or the
// fade halo reappears behind the shadow at the build's edge: the builder's
// mirror of kFogRimFadeCells must match both shared fog reveals.
TEST(FogLineOfSightTest, BuildReachCoversTheShaderRimFade) {
    for (const char *kernel : {"/ir_fog_common.glsl", "/metal/ir_fog_common.metal"}) {
        std::ifstream file(std::string(IR_TEST_RENDER_SHADER_DIR) + kernel);
        std::ostringstream source;
        source << file.rdbuf();
        std::smatch match;
        const std::string text = source.str();
        ASSERT_TRUE(
            std::regex_search(text, match, std::regex(R"(kFogRimFadeCells\s*=\s*([0-9.]+)f?;)"))
        ) << kernel;
        EXPECT_FLOAT_EQ(std::stof(match[1].str()), IRPrefab::Fog::kFogLosRimFadeCells) << kernel;
    }
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::losBuildReach(vec4(0.0f, 0.0f, 10.0f, 0.0f)),
        10.0f + IRPrefab::Fog::kFogLosRimFadeCells + IRPrefab::Fog::kFogLosDiscMargin
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::losBuildReach(vec4(0.0f, 0.0f, 10.0f, 3.0f)),
        13.0f + IRPrefab::Fog::kFogLosDiscMargin
    ) << "a soft disc has no rim fade";
}

// The shared interior-cell visitor visits exactly the cells a brute-force
// evaluation over a generous box marks interior, clipped to the requested box.
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

            // The column-top walk keeps exactly the smallest-z cell of each
            // column of the full walk.
            std::vector<std::int32_t> fromCells = emptyColumns();
            for (const ivec3 &cell : visited) {
                IRPrefab::Fog::stampLosColumn(fromCells, cell);
            }
            std::vector<std::int32_t> fromTops = emptyColumns();
            IRMath::SDF::forEachInteriorColumnTop(
                shape.type_,
                shape.params_,
                shape.centre_,
                clipMin,
                clipMax,
                [&](ivec3 cell) { setColumn(fromTops, cell.x, cell.y, cell.z); }
            );
            EXPECT_EQ(fromTops, fromCells);
        }
    }
}

class FogLineOfSightEcsTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entityManager{};
    C_VoxelPool m_pool{ivec3(4, 4, 4)};
    std::vector<std::int32_t> m_columns = emptyColumns();

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
        IRPrefab::Fog::rasterizeLosColumns(m_pool, IREntity::kNullEntity, m_columns);
    }

    int top(int x, int y) const {
        return m_columns[C_CanvasFogOfWar::flatIndex(x, y)];
    }
};

TEST_F(FogLineOfSightEcsTest, ShapeOccludesOnlyWhenFlagged) {
    const IREntity::EntityId wall = makeWall(true);
    rasterize();
    EXPECT_EQ(top(0, 0), 0) << "the box spans z -0.5..3.5; its top interior cell centre is 0";
    EXPECT_NE(top(1, 8), kFogLosColumnEmpty);
    EXPECT_EQ(top(6, 0), kFogLosColumnEmpty);

    IREntity::getComponent<C_LightBlocker>(wall).blocksLOS_ = false;
    rasterize();
    EXPECT_EQ(top(0, 0), kFogLosColumnEmpty) << "blocksLOS_ = false must not occlude";

    IREntity::getComponent<C_LightBlocker>(wall).blocksLOS_ = true;
    IREntity::getComponent<C_ShapeDescriptor>(wall).canvasEntity_ = 12345;
    rasterize();
    EXPECT_EQ(top(0, 0), kFogLosColumnEmpty) << "a shape on another canvas must not occlude";
}

// Moving, toggling and removing a blocker between builds leaves no stale
// occlusion: each build reads only the current frame's occluders.
TEST_F(FogLineOfSightEcsTest, BlockerChangesLeaveNoStaleOcclusion) {
    const FrameDataFogObservers observers = gatedSources({vec4(-6.0f, 0.0f, 14.0f, 0.0f)}, 4.5f);
    const FogLosEyeHeights eyes = eyesOf(1.5f);
    const ivec3 behind(6, 0, 4);
    const auto farSideVisible = [&]() {
        rasterize();
        const std::vector<float> horizons = buildField(observers, eyes, m_columns);
        return FogLineOfSightField{horizons.data()}.visible(0, behind);
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

// Pool voxels stamp their rounded cells; carved (alpha 0) and governed
// (kFogWholeBodyExempt) voxels never occlude.
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
    EXPECT_EQ(top(2, 0), -3) << "roundHalfUp(-0.5) = 0 and roundHalfUp(-3.5) = -3";
    EXPECT_EQ(top(5, 5), kFogLosColumnEmpty) << "a carved voxel must not occlude";
    EXPECT_EQ(top(7, 7), kFogLosColumnEmpty) << "a governed voxel must not occlude";
}

// Slot authoring: addVisionCircle hands back the slot it filled (or -1), a new
// slot starts ungated, and clearing drops every gate.
TEST(FogVisionSlotTest, SlotsStartUngatedAndClearDropsGates) {
    FrameDataFogObservers observers{};
    FogLosEyeHeights eyes = eyesOf(IRComponents::kFogVisionLosOff);
    EXPECT_EQ(C_CanvasFogOfWar::addVisionCircle(observers, eyes, 0, 0, 0.0f, 0, 0, 0, -1, 0), -1)
        << "a non-positive radius is rejected";
    for (int i = 0; i < IRComponents::kMaxFogVisionCircles; ++i) {
        EXPECT_EQ(C_CanvasFogOfWar::addVisionCircle(observers, eyes, 0, 0, 5, 0, 0, 0, -1, 0), i);
    }
    EXPECT_EQ(C_CanvasFogOfWar::addVisionCircle(observers, eyes, 0, 0, 5, 0, 0, 0, -1, 0), -1)
        << "past the cap is rejected";
    EXPECT_EQ(observers.losSourceMask_, 0);

    C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, eyes, 3, 1.5f);
    C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, eyes, 5, 0.0f);
    EXPECT_EQ(observers.losSourceMask_, (1 << 3) | (1 << 5));
    EXPECT_FLOAT_EQ(eyes[3], 1.5f);
    C_CanvasFogOfWar::setVisionCircleLineOfSight(
        observers,
        eyes,
        5,
        IRComponents::kFogVisionLosOff
    );
    EXPECT_EQ(observers.losSourceMask_, 1 << 3);

    C_CanvasFogOfWar::clearVisionCircles(observers, eyes);
    EXPECT_EQ(observers.losSourceMask_, 0);
    EXPECT_EQ(observers.visionCircleCount_, 0);
    for (int i = 0; i < 4; ++i) {
        C_CanvasFogOfWar::addVisionCircle(observers, eyes, 0, 0, 5, 0, 0, 0, -1, 0);
    }
    EXPECT_EQ(observers.losSourceMask_, 0) << "a re-added slot 3 inherited a stale gate";
    EXPECT_FLOAT_EQ(eyes[3], IRComponents::kFogVisionLosOff);
}

// Gating an unregistered slot is a caller bug: it asserts in debug and, in a
// release build, leaves the gates untouched.
TEST(FogVisionSlotTest, GatingAnUnregisteredSlotIsRejected) {
    FrameDataFogObservers observers{};
    FogLosEyeHeights eyes = eyesOf(IRComponents::kFogVisionLosOff);
    C_CanvasFogOfWar::addVisionCircle(observers, eyes, 0, 0, 5, 0, 0, 0, -1, 0);
#ifndef IR_RELEASE
    EXPECT_THROW(
        C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, eyes, 1, 1.5f),
        std::runtime_error
    );
    EXPECT_THROW(
        C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, eyes, -1, 1.5f),
        std::runtime_error
    );
#else
    C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, eyes, 1, 1.5f);
#endif
    EXPECT_EQ(observers.losSourceMask_, 0);
    EXPECT_FLOAT_EQ(eyes[1], IRComponents::kFogVisionLosOff);
}
