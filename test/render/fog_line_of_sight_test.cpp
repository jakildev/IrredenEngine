// FogLineOfSight exercises the CPU half of the fog line-of-sight model
// (component_canvas_fog_of_war.hpp states it; fog_line_of_sight.hpp implements
// it): the column rasteriser, the supercover horizon trace, the per-source
// horizon build over source-anchored tiles and the field gate the reveal
// oracle reads. Headless — the builder works on plain vectors, and the shape
// rasteriser needs only an EntityManager. Every scene test runs at the origin
// and translated far from it, so a tile that silently stayed world-centred
// fails on the translated arm.

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
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
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
using IRComponents::kFogLosTileEdge;
using IRComponents::kFogLosTileHalfExtent;
using IRMath::ivec2;
using IRMath::ivec3;
using IRMath::vec2;
using IRMath::vec3;
using IRMath::vec4;
using IRPrefab::Fog::LosColumnView;
using IRPrefab::Fog::LosColumnViews;

// The XY translations every scene test runs at: the origin, and a point far
// enough that no world-centred 256² tile could contain it.
const ivec2 kSceneShifts[] = {ivec2(0, 0), ivec2(3000, -5000)};

// A column scene: flat ground plus per-column overrides, rasterisable into a
// view at any tile origin.
struct ColumnScene {
    std::int32_t ground_ = kFogLosColumnEmpty;
    std::vector<std::pair<ivec2, std::int32_t>> overrides_;

    std::vector<std::int32_t> view(ivec2 tileOrigin) const {
        std::vector<std::int32_t> columns(IRComponents::kFogLosColumnCount, ground_);
        for (const auto &[column, value] : overrides_) {
            IRPrefab::Fog::stampLosColumn(columns, tileOrigin, ivec3(column, value));
        }
        return columns;
    }
};

ColumnScene emptyScene() {
    return ColumnScene{};
}

ColumnScene flatScene(int top) {
    return ColumnScene{top, {}};
}

void setColumn(ColumnScene &scene, int x, int y, int top) {
    scene.overrides_.emplace_back(ivec2(x, y), top);
}

ColumnScene translated(ColumnScene scene, ivec2 shift) {
    for (auto &[column, value] : scene.overrides_) {
        column += shift;
    }
    return scene;
}

// The shared scene: flat ground at `groundTop`, a ridge four voxels above it
// at x 0..1, y -7..8 (the fog_demo --occlusion ridge), and a free-standing
// tower at (-2, 5) ten voxels above the ground.
constexpr int kGroundTop = 4;

ColumnScene ridgeScene(int zShift = 0) {
    ColumnScene scene = flatScene(kGroundTop + zShift);
    for (int y = -7; y <= 8; ++y) {
        for (int x = 0; x <= 1; ++x) {
            setColumn(scene, x, y, kGroundTop - 4 + zShift);
        }
    }
    setColumn(scene, -2, 5, kGroundTop - 10 + zShift);
    return scene;
}

// The query tile `IRPrefab::Fog::lineOfSight` uses: anchored on the eye.
ivec2 queryOrigin(vec3 eye) {
    return FogLineOfSightField::tileOrigin(vec4(eye.x, eye.y, 0.0f, 0.0f));
}

float horizon(const ColumnScene &scene, vec3 eye, ivec2 target) {
    const ivec2 origin = queryOrigin(eye);
    const std::vector<std::int32_t> columns = scene.view(origin);
    return IRPrefab::Fog::traceLosHorizon(columns, origin, eye, target);
}

bool visible(const ColumnScene &scene, vec3 eye, ivec3 sample) {
    return static_cast<float>(sample.z) <= horizon(scene, eye, ivec2(sample));
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

FrameDataFogObservers translated(FrameDataFogObservers observers, ivec2 shift) {
    for (int i = 0; i < observers.visionCircleCount_; ++i) {
        observers.visionCircles_[i].x += static_cast<float>(shift.x);
        observers.visionCircles_[i].y += static_cast<float>(shift.y);
    }
    return observers;
}

// Rasterises one view per gated source from @p scene and builds the field.
std::vector<float> buildField(
    const FrameDataFogObservers &observers, const FogLosEyeHeights &eyes, const ColumnScene &scene
) {
    std::vector<std::int32_t> columnTops(IRComponents::kFogLosColumnViewCount, kFogLosColumnEmpty);
    LosColumnViews views{};
    const int viewCount = IRPrefab::Fog::losSourceViews(observers, columnTops, views);
    for (int i = 0; i < viewCount; ++i) {
        const LosColumnView &view = views[static_cast<std::size_t>(i)];
        const std::vector<std::int32_t> rendered = scene.view(view.origin_);
        std::copy(rendered.begin(), rendered.end(), view.tops_.begin());
    }
    std::vector<float> horizons(IRComponents::kFogLosHorizonCount, 0.0f);
    IRPrefab::Fog::buildLosHorizons(
        observers,
        eyes,
        std::span<const LosColumnView>{views.data(), static_cast<std::size_t>(viewCount)},
        horizons
    );
    return horizons;
}

FogLineOfSightField
fieldOf(const std::vector<float> &horizons, const FrameDataFogObservers &observers) {
    return FogLineOfSightField{horizons.data(), FogLineOfSightField::tileOriginsOf(observers)};
}

FogLosEyeHeights eyesOf(float height) {
    FogLosEyeHeights eyes{};
    eyes.fill(height);
    return eyes;
}

vec3 shifted(vec3 point, ivec2 shift) {
    return point + vec3(static_cast<float>(shift.x), static_cast<float>(shift.y), 0.0f);
}

ivec3 shifted(ivec3 cell, ivec2 shift) {
    return cell + ivec3(shift.x, shift.y, 0);
}

} // namespace

// A tile is anchored on its source: `roundHalfUp(centre) - 128` on both axes,
// including at a negative half-integer centre, and the component derives the
// same origins for every registered source.
TEST(FogLineOfSightTest, TilesAnchorOnTheirSourceCentre) {
    EXPECT_EQ(FogLineOfSightField::tileOrigin(vec4(0.0f, 0.0f, 10.0f, 0.0f)), ivec2(-128, -128));
    EXPECT_EQ(FogLineOfSightField::tileOrigin(vec4(-2.5f, -3.5f, 10.0f, 0.0f)), ivec2(-130, -131));
    EXPECT_EQ(
        FogLineOfSightField::tileOrigin(vec4(3000.4f, -5000.6f, 10.0f, 0.0f)),
        ivec2(2872, -5129)
    );
    const FrameDataFogObservers observers =
        gatedSources({vec4(7.0f, 6.0f, 10.0f, 0.0f), vec4(-6.0f, -6.0f, 10.0f, 0.0f)}, 0.0f);
    const IRComponents::FogLosTileOrigins origins = FogLineOfSightField::tileOriginsOf(observers);
    EXPECT_EQ(origins[0], ivec2(-121, -122));
    EXPECT_EQ(origins[1], ivec2(-134, -134));
    EXPECT_TRUE(FogLineOfSightField::cellInTile(ivec2(7 + 127, 6), origins[0]));
    EXPECT_FALSE(FogLineOfSightField::cellInTile(ivec2(7 + 128, 6), origins[0]));
    EXPECT_EQ(FogLineOfSightField::columnIndex(ivec2(-121, -122), origins[0]), 0u);
    EXPECT_EQ(FogLineOfSightField::horizonIndex(1, ivec2(-134, -134), origins[1]), 1u)
        << "source 1 is channel 1 of tile row 0";
    EXPECT_EQ(
        FogLineOfSightField::horizonIndex(4, ivec2(-121, -122), origins[0]),
        static_cast<std::size_t>(kFogLosTileEdge) * kFogLosTileEdge * 4u
    ) << "source 4 is channel 0 of tile row 1";
}

// Flat ground never hides itself: every in-disc top voxel (the height a top-face
// pixel recovers exactly) passes, across fractional eye heights and centres
// (one on a negative half-integer) and integer translations of the whole scene.
// Mutation control: adding a `- 0.5f` surface offset to the column top in
// `traceLosHorizon` fails this beyond the near cells.
TEST(FogLineOfSightTest, FlatSlabRemainsVisible) {
    constexpr float kRadius = 32.0f;
    int probed = 0;
    for (const ivec2 shift : kSceneShifts) {
        for (const int top : {4, 0, -1024, 1024}) {
            const ColumnScene scene = flatScene(top);
            for (const float eyeHeight : {1.5f, 0.5f, 0.73f, 2.25f, 0.0f}) {
                for (const vec2 centre : {vec2(0.0f), vec2(0.37f, -0.61f), vec2(-2.5f, -3.5f)}) {
                    const FrameDataFogObservers observers = translated(
                        gatedSources({vec4(centre, kRadius, 0.0f)}, static_cast<float>(top)),
                        shift
                    );
                    const FogLosEyeHeights eyes = eyesOf(eyeHeight);
                    const std::vector<float> horizons = buildField(observers, eyes, scene);
                    const FogLineOfSightField field = fieldOf(horizons, observers);
                    const vec3 eye =
                        shifted(vec3(centre, static_cast<float>(top) - eyeHeight), shift);
                    for (int y = -40; y <= 40; ++y) {
                        for (int x = -40; x <= 40; ++x) {
                            if (IRMath::length(vec2(x, y) - centre) > kRadius) {
                                continue;
                            }
                            ++probed;
                            const ivec3 sample = shifted(ivec3(x, y, top), shift);
                            ASSERT_TRUE(visible(scene, eye, sample))
                                << "flat ground hid its own top voxel at (" << sample.x << ", "
                                << sample.y << ") top " << top << " eye height " << eyeHeight;
                            ASSERT_TRUE(field.visible(0, sample))
                                << "the built field hid flat ground at (" << sample.x << ", "
                                << sample.y << ")";
                        }
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
    ColumnScene scene = flatScene(kGroundTop);
    setColumn(scene, -2, -3, -20);
    const vec3 eye(-2.5f, -3.5f, 2.0f);
    EXPECT_TRUE(visible(scene, eye, ivec3(3, -3, kGroundTop)))
        << "the eye's own (round-half-up) cell occluded the ray";

    setColumn(scene, -1, -3, -20);
    EXPECT_FALSE(visible(scene, eye, ivec3(3, -3, kGroundTop)))
        << "control: a tower one cell along the ray must occlude";
}

// A source sees clear in its own cell, and past its tile: the tile of a source
// at the origin is [-128, 128), so column 128 is out of it and column -128 in.
TEST(FogLineOfSightTest, SameCellAndOutOfTileAreClear) {
    const ColumnScene scene = flatScene(-100);
    EXPECT_EQ(horizon(scene, vec3(3.2f, 4.4f, 0.0f), ivec2(3, 4)), kFogLosHorizonClear);

    FrameDataFogObservers observers{};
    for (int i = 0; i < IRComponents::kMaxFogVisionCircles; ++i) {
        observers.visionCircles_[i] = vec4(0.0f, 0.0f, 10.0f, 0.0f);
        observers.losSourceMask_ |= 1 << i;
    }
    observers.visionCircleCount_ = IRComponents::kMaxFogVisionCircles;
    const std::vector<float> horizons(IRComponents::kFogLosHorizonCount, -1000.0f);
    const FogLineOfSightField field = fieldOf(horizons, observers);
    EXPECT_TRUE(field.visible(0, ivec3(kFogLosTileHalfExtent, 0, 0)));
    EXPECT_TRUE(field.visible(7, ivec3(0, -kFogLosTileHalfExtent - 1, 0)));
    EXPECT_FALSE(field.visible(7, ivec3(0, -kFogLosTileHalfExtent, 0)));
    EXPECT_FALSE(FogLineOfSightField{}.visible(0, ivec3(0))) << "an unpublished field is closed";
}

// An exact corner crossing visits both side cells: a tower on either side of
// the diagonal occludes the target.
TEST(FogLineOfSightTest, CornerTieVisitsBothSideCells) {
    const vec3 eye(0.0f, 0.0f, 2.0f);
    const ivec3 target(2, 2, kGroundTop);
    EXPECT_TRUE(visible(emptyScene(), eye, target));
    for (const ivec2 side : {ivec2(1, 0), ivec2(0, 1), ivec2(2, 1), ivec2(1, 2)}) {
        ColumnScene scene = emptyScene();
        setColumn(scene, side.x, side.y, -20);
        EXPECT_FALSE(visible(scene, eye, target))
            << "side cell (" << side.x << ", " << side.y << ") of the corner tie was skipped";
    }
    ColumnScene offRay = emptyScene();
    setColumn(offRay, 2, 0, -20);
    EXPECT_TRUE(visible(offRay, eye, target)) << "control: a tower off the ray must not occlude";
}

// The ridge scene: ground behind the ridge hides from a ground observer, the
// near ground and a tower top above the ridge's shadow line stay visible, and
// the same far ground reveals from the ridge top. At the origin and far away.
TEST(FogLineOfSightTest, RidgeHidesWhatIsBehindItFromTheGround) {
    for (const ivec2 shift : kSceneShifts) {
        const ColumnScene scene = translated(ridgeScene(), shift);
        const vec3 groundEye = shifted(vec3(-6.0f, 0.0f, 3.0f), shift);
        EXPECT_TRUE(visible(scene, groundEye, shifted(ivec3(-3, 0, kGroundTop), shift)));
        EXPECT_TRUE(visible(scene, groundEye, shifted(ivec3(0, 0, kGroundTop - 4), shift)))
            << "the ridge's near top voxel faces the eye";
        EXPECT_FALSE(visible(scene, groundEye, shifted(ivec3(6, 0, kGroundTop), shift)));
        EXPECT_TRUE(visible(scene, groundEye, shifted(ivec3(6, 0, -10), shift)))
            << "a tower top above the ridge's shadow line stays visible";

        const vec3 ridgeEye = shifted(vec3(0.0f, 0.0f, -1.0f), shift);
        EXPECT_TRUE(visible(scene, ridgeEye, shifted(ivec3(6, 0, kGroundTop), shift)));
        EXPECT_FALSE(visible(scene, ridgeEye, shifted(ivec3(2, 0, kGroundTop), shift)))
            << "the strip at the ridge's base is in its own shadow";

        EXPECT_TRUE(
            visible(flatScene(kGroundTop), groundEye, shifted(ivec3(6, 0, kGroundTop), shift))
        ) << "control: without the ridge the far ground is visible";
    }
}

// `lineOfSight`'s core — the trace over the same columns — agrees with the
// built field at every finite horizon: floor(H) visible, floor(H) + 1 hidden,
// and the stored value is the trace's exact float. Across Z and XY
// translations.
TEST(FogLineOfSightTest, PointQueryAgreesWithFieldAtCellCentres) {
    for (const ivec2 shift : kSceneShifts) {
        for (const int zShift : {0, -1024, 1024}) {
            const ColumnScene scene = translated(ridgeScene(zShift), shift);
            const FrameDataFogObservers observers = translated(
                gatedSources(
                    {vec4(-6.0f, 0.0f, 14.0f, 0.0f), vec4(0.0f, 0.0f, 14.0f, 0.0f)},
                    static_cast<float>(kGroundTop + zShift) + 0.5f
                ),
                shift
            );
            FogLosEyeHeights eyes = eyesOf(2.0f);
            eyes[1] = 1.5f;
            const std::vector<float> horizons = buildField(observers, eyes, scene);
            const FogLineOfSightField field = fieldOf(horizons, observers);
            int finite = 0;
            for (int source = 0; source < 2; ++source) {
                const vec3 eye = IRPrefab::Fog::losEye(observers, eyes, source);
                const vec4 circle = observers.visionCircles_[source];
                const ivec2 origin = FogLineOfSightField::tileOrigin(circle);
                const float reach = IRPrefab::Fog::losBuildReach(circle);
                for (int dy = -20; dy <= 20; ++dy) {
                    for (int dx = -20; dx <= 20; ++dx) {
                        const ivec2 cell = ivec2(dx, dy) + shift;
                        if (IRMath::length(vec2(cell) - vec2(circle)) > reach) {
                            continue;
                        }
                        const float stored =
                            horizons[FogLineOfSightField::horizonIndex(source, cell, origin)];
                        ASSERT_EQ(stored, horizon(scene, eye, cell))
                            << "field and point trace disagree at (" << cell.x << ", " << cell.y
                            << ")";
                        if (stored == kFogLosHorizonClear) {
                            continue;
                        }
                        ++finite;
                        const int floorZ = static_cast<int>(IRMath::floor(stored));
                        EXPECT_TRUE(field.visible(source, ivec3(cell, floorZ)));
                        EXPECT_FALSE(field.visible(source, ivec3(cell, floorZ + 1)));
                        EXPECT_TRUE(visible(scene, eye, ivec3(cell, floorZ)));
                        EXPECT_FALSE(visible(scene, eye, ivec3(cell, floorZ + 1)));
                    }
                }
            }
            EXPECT_GT(finite, 0) << "the fixture has no finite horizon";
        }
    }
}

// Every lane of the RGBA32F tiles is addressed independently: each of the
// eight sources sits at its own spot (so its own tile origin), and at least
// one probe per lane differs from the next lane's verdict, so a swapped
// channel or tile fails here.
TEST(FogLineOfSightTest, EightSourcesUseIndependentLanes) {
    for (const ivec2 shift : kSceneShifts) {
        const ColumnScene scene = translated(ridgeScene(), shift);
        FrameDataFogObservers observers{};
        FogLosEyeHeights eyes{};
        for (int i = 0; i < IRComponents::kMaxFogVisionCircles; ++i) {
            const float x =
                i % 2 == 0 ? -6.0f - static_cast<float>(i) : 7.0f + static_cast<float>(i);
            const int slot = C_CanvasFogOfWar::addVisionCircle(
                observers,
                eyes,
                x + static_cast<float>(shift.x),
                static_cast<float>(shift.y),
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
        const std::vector<float> horizons = buildField(observers, eyes, scene);
        const FogLineOfSightField field = fieldOf(horizons, observers);
        for (int i = 0; i < IRComponents::kMaxFogVisionCircles; ++i) {
            const int next = (i + 1) % IRComponents::kMaxFogVisionCircles;
            int distinguishing = 0;
            for (int x = -12; x <= 12; ++x) {
                const ivec3 probe = shifted(ivec3(x, 0, kGroundTop), shift);
                const bool expected =
                    visible(scene, IRPrefab::Fog::losEye(observers, eyes, i), probe);
                ASSERT_EQ(field.visible(i, probe), expected) << "lane " << i << " at x " << x;
                if (field.visible(i, probe) != field.visible(next, probe)) {
                    ++distinguishing;
                }
            }
            EXPECT_GT(distinguishing, 0) << "lanes " << i << " and " << next << " read identically";
        }
    }
}

// Ungated sources and cells past the build reach read clear.
TEST(FogLineOfSightTest, UngatedTilesAndCellsBeyondReachAreClear) {
    const ColumnScene scene = ridgeScene();
    FrameDataFogObservers observers =
        gatedSources({vec4(-6.0f, 0.0f, 10.0f, 0.0f), vec4(-6.0f, 0.0f, 10.0f, 0.0f)}, 4.5f);
    observers.losSourceMask_ = 0b10;
    const std::vector<float> horizons = buildField(observers, eyesOf(1.5f), scene);
    const FogLineOfSightField field = fieldOf(horizons, observers);
    EXPECT_TRUE(field.visible(0, ivec3(6, 0, kGroundTop))) << "an ungated tile must be clear";
    EXPECT_FALSE(field.visible(1, ivec3(6, 0, kGroundTop)));
    const int beyond =
        static_cast<int>(IRPrefab::Fog::losBuildReach(vec4(-6.0f, 0.0f, 10.0f, 0.0f))) + 2;
    const ivec2 origin = FogLineOfSightField::tileOrigin(observers.visionCircles_[1]);
    EXPECT_EQ(
        horizons[FogLineOfSightField::horizonIndex(1, ivec2(-6 + beyond, 0), origin)],
        kFogLosHorizonClear
    );
}

// A disc wider than the tile's half extent is clipped to the tile: the build
// stays inside the source's tile, an occluder outside it is unknown to the
// source, and a sample outside it reads visible.
TEST(FogLineOfSightTest, DiscWiderThanTheTileIsClippedToTheTile) {
    ColumnScene scene = flatScene(kGroundTop);
    setColumn(scene, 140, 0, -20);
    const FrameDataFogObservers observers = gatedSources({vec4(0.0f, 0.0f, 200.0f, 0.0f)}, 4.5f);
    const std::vector<float> horizons = buildField(observers, eyesOf(1.5f), scene);
    const FogLineOfSightField field = fieldOf(horizons, observers);
    EXPECT_TRUE(field.visible(0, ivec3(200, 0, kGroundTop))) << "outside the tile reads visible";
    EXPECT_TRUE(field.visible(0, ivec3(127, 0, kGroundTop)))
        << "the tower at 140 lies outside the tile and cannot shadow the tile's edge";
    setColumn(scene, 100, 0, -20);
    const std::vector<float> withTower = buildField(observers, eyesOf(1.5f), scene);
    EXPECT_FALSE(fieldOf(withTower, observers).visible(0, ivec3(127, 0, kGroundTop)))
        << "control: an in-tile tower shadows the tile's edge";
}

// A hard disc's shadow must reach as far as the fog kernel's rim fade, or the
// fade halo reappears behind the shadow at the build's edge: the builder's
// mirror of kFogRimFadeCells must match both kernels.
TEST(FogLineOfSightTest, BuildReachCoversTheShaderRimFade) {
    for (const char *kernel : {"/c_fog_to_trixel.glsl", "/metal/c_fog_to_trixel.metal"}) {
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
    const ivec2 tileOrigin(-kFogLosTileHalfExtent);
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
            std::vector<std::int32_t> fromCells(
                IRComponents::kFogLosColumnCount,
                kFogLosColumnEmpty
            );
            for (const ivec3 &cell : visited) {
                IRPrefab::Fog::stampLosColumn(fromCells, tileOrigin, cell);
            }
            std::vector<std::int32_t> fromTops(
                IRComponents::kFogLosColumnCount,
                kFogLosColumnEmpty
            );
            IRMath::SDF::forEachInteriorColumnTop(
                shape.type_,
                shape.params_,
                shape.centre_,
                clipMin,
                clipMax,
                [&](ivec3 cell) {
                    fromTops[FogLineOfSightField::columnIndex(ivec2(cell), tileOrigin)] = cell.z;
                }
            );
            EXPECT_EQ(fromTops, fromCells);
        }
    }
}

class FogLineOfSightEcsTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entityManager{};
    C_VoxelPool m_pool{ivec3(4, 4, 4)};
    ivec2 m_origin{-kFogLosTileHalfExtent};
    std::vector<std::int32_t> m_columns =
        std::vector<std::int32_t>(IRComponents::kFogLosColumnCount, kFogLosColumnEmpty);

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
        const LosColumnView view{0, m_origin, m_columns};
        IRPrefab::Fog::rasterizeLosColumns(
            m_pool,
            IREntity::kNullEntity,
            std::span<const LosColumnView>{&view, 1}
        );
    }

    int top(int x, int y) const {
        return m_columns[FogLineOfSightField::columnIndex(ivec2(x, y), m_origin)];
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

// A flagged shape far from the origin lands in a view anchored there and
// nowhere near a view anchored at the origin.
TEST_F(FogLineOfSightEcsTest, ShapeRasterizesIntoTheViewAnchoredOnIt) {
    makeWall(true, vec3(3000.5f, -5000.5f, 1.5f));
    rasterize();
    EXPECT_EQ(top(0, 0), kFogLosColumnEmpty);
    m_origin = ivec2(3000 - kFogLosTileHalfExtent, -5000 - kFogLosTileHalfExtent);
    rasterize();
    EXPECT_EQ(top(3000, -5000), 0);
    EXPECT_NE(top(3001, -4993), kFogLosColumnEmpty);
    EXPECT_EQ(top(3006, -5000), kFogLosColumnEmpty);
}

// Moving, toggling and removing a blocker between builds leaves no stale
// occlusion: each build reads only the current frame's occluders.
TEST_F(FogLineOfSightEcsTest, BlockerChangesLeaveNoStaleOcclusion) {
    const FrameDataFogObservers observers = gatedSources({vec4(-6.0f, 0.0f, 14.0f, 0.0f)}, 4.5f);
    const FogLosEyeHeights eyes = eyesOf(1.5f);
    const ivec3 behind(6, 0, 4);
    m_origin = FogLineOfSightField::tileOrigin(observers.visionCircles_[0]);
    const auto farSideVisible = [&]() {
        rasterize();
        const LosColumnView view{0, m_origin, m_columns};
        std::vector<float> horizons(IRComponents::kFogLosHorizonCount, 0.0f);
        IRPrefab::Fog::buildLosHorizons(
            observers,
            eyes,
            std::span<const LosColumnView>{&view, 1},
            horizons
        );
        return fieldOf(horizons, observers).visible(0, behind);
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

// One pool pass fills every gated source's view at its own origin.
TEST_F(FogLineOfSightEcsTest, OnePoolPassFillsEveryTile) {
    IRRender::VoxelPoolAllocation allocation = m_pool.allocateVoxels(2);
    allocation.positionGlobals_[0].pos_ = vec3(2.0f, 0.0f, -3.0f);
    allocation.positionGlobals_[1].pos_ = vec3(3002.0f, -5000.0f, -7.0f);
    allocation.voxels_[0].color_.alpha_ = 255;
    allocation.voxels_[1].color_.alpha_ = 255;
    const FrameDataFogObservers observers =
        gatedSources({vec4(0.0f, 0.0f, 10.0f, 0.0f), vec4(3000.0f, -5000.0f, 10.0f, 0.0f)}, 0.0f);
    std::vector<std::int32_t> columnTops(IRComponents::kFogLosColumnViewCount, 0);
    LosColumnViews views{};
    ASSERT_EQ(IRPrefab::Fog::losSourceViews(observers, columnTops, views), 2);
    IRPrefab::Fog::rasterizeLosColumns(
        m_pool,
        IREntity::kNullEntity,
        std::span<const LosColumnView>{views.data(), 2}
    );
    EXPECT_EQ(views[0].tops_[FogLineOfSightField::columnIndex(ivec2(2, 0), views[0].origin_)], -3);
    EXPECT_EQ(
        views[1].tops_[FogLineOfSightField::columnIndex(ivec2(3002, -5000), views[1].origin_)],
        -7
    );
    EXPECT_EQ(
        views[1].tops_[FogLineOfSightField::columnIndex(ivec2(3002, -4998), views[1].origin_)],
        kFogLosColumnEmpty
    ) << "the near voxel is outside the far tile";
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
