// Occlusion-aware light-volume boundary seed (#2330).
//
// A light whose rounded origin falls outside the camera-anchored light
// volume seeds the per-axis-clamped boundary cell. When that clamped cell
// is inside solid geometry, `c_propagate_light_volume`'s neighbour
// occlusion gate traps the seed and the light pops off, so the gather
// relocates the seed to the nearest unoccluded cell on a window face the
// clamp touched — or skips the light when no such cell is within its
// remaining residual reach.
//
// These tests drive `IRSystem::detail::gatherLightSources` directly against
// synthetic occupancy bitfields, the same fixture shape
// `per_canvas_light_scope_test.cpp` uses: the gather only touches the
// EntityManager, and the occlusion view is a pointer pair into two
// stack-owned vectors. No RenderManager, no GPU resources.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace IRComponents;
using namespace IRMath;

namespace {

// Both the occlusion grid and the light volume are anchored at the world
// origin for every case here, so a window coordinate IS a world coordinate.
const ivec3 kGridOrigin{0, 0, 0};

// Highest in-window index per axis: the seedable window is
// [-kLightVolumeHalfExtent, kLightVolumeHalfExtent - 1].
constexpr int kWindowMax = IRComponents::kLightVolumeHalfExtent - 1;

struct GatherResult {
    std::vector<IRRender::GPULightSource> out_;
    std::vector<IRSystem::LightGatherRecord> states_;
    std::uint32_t count_ = 0;
    std::uint32_t eligible_ = 0;
    int maxRadius_ = 0;
    bool hasSpot_ = false;
};

class LightVolumeBoundarySeedTest : public testing::Test {
  protected:
    LightVolumeBoundarySeedTest()
        : m_entityManager{}
        , m_voxelBits(IRSystem::kLightOcclusionBitfieldUintCount, 0u)
        , m_blockerBits(IRSystem::kLightOcclusionBitfieldUintCount, 0u) {}

    void occludeVoxel(int wx, int wy, int wz) {
        IRSystem::detail::gridSetBit(m_voxelBits, wx, wy, wz, kGridOrigin);
    }

    void occludeBlocker(int wx, int wy, int wz) {
        IRSystem::detail::gridSetBit(m_blockerBits, wx, wy, wz, kGridOrigin);
    }

    /// A view as the producer hands it out on a frame it actually populated
    /// the mirror. `populated_` is fail-closed, so it is passed explicitly —
    /// see `staleView()` for the other half.
    IRSystem::detail::LightOcclusionGridView view() const {
        return IRSystem::detail::LightOcclusionGridView{
            &m_voxelBits, &m_blockerBits, kGridOrigin, true};
    }

    /// A view over the same bits with the liveness stamp clear — what a
    /// consumer gets on a frame where BUILD_LIGHT_OCCLUSION_GRID's archetype
    /// did not match (it filters C_VoxelPool; the consumer filters
    /// C_CanvasLightVolume, so the two can diverge).
    IRSystem::detail::LightOcclusionGridView staleView() const {
        return IRSystem::detail::LightOcclusionGridView{
            &m_voxelBits, &m_blockerBits, kGridOrigin, false};
    }

    // Seed C_WorldTransform directly — no PROPAGATE_TRANSFORM is running to
    // resolve a local transform into the world slot.
    static IREntity::EntityId makeLight(vec3 position, std::uint8_t radius) {
        return IREntity::createEntity(
            C_WorldTransform{position, vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
            C_LightSource{LightType::EMISSIVE, Color{255, 255, 255, 255}, 1.0f, radius}
        );
    }

    static IREntity::EntityId makeSpotLight(vec3 position, std::uint8_t radius) {
        return IREntity::createEntity(
            C_WorldTransform{position, vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
            C_LightSource{
                LightType::SPOT,
                Color{255, 255, 255, 255},
                1.0f,
                radius,
                vec3(-1.0f, 0.0f, 0.0f),
                40.0f
            }
        );
    }

    GatherResult
    gather(IREntity::EntityId canvas, const IRSystem::detail::LightOcclusionGridView *occlusion) {
        GatherResult result;
        result.out_.reserve(IRRender::kLightVolumeMaxSources);
        result.count_ = IRSystem::detail::gatherLightSources(
            result.out_,
            canvas,
            kGridOrigin,
            result.maxRadius_,
            result.eligible_,
            result.hasSpot_,
            &result.states_,
            occlusion
        );
        return result;
    }

    static ivec3 seedCell(const IRRender::GPULightSource &light) {
        return ivec3(
            static_cast<int>(light.originAndType_.x),
            static_cast<int>(light.originAndType_.y),
            static_cast<int>(light.originAndType_.z)
        );
    }

    IREntity::EntityManager m_entityManager;
    std::vector<std::uint32_t> m_voxelBits;
    std::vector<std::uint32_t> m_blockerBits;
};

} // namespace

// The OFF-path proof: when the clamped cell is free, passing an occlusion
// view must produce exactly the same GPULightSource as passing none. Every
// scene whose boundary seeds land in open space stays byte-identical.
TEST_F(LightVolumeBoundarySeedTest, FreeClampedCellIsByteIdenticalToToday) {
    const IREntity::EntityId canvas = IREntity::createEntity();
    makeLight(vec3(70.0f, 0.0f, 0.0f), 32);

    const IRSystem::detail::LightOcclusionGridView occlusion = view();
    const GatherResult withView = gather(canvas, &occlusion);
    const GatherResult withoutView = gather(canvas, nullptr);

    ASSERT_EQ(withView.count_, 1u);
    ASSERT_EQ(withoutView.count_, 1u);
    EXPECT_EQ(seedCell(withView.out_[0]), ivec3(kWindowMax, 0, 0));
    // boundaryDist 7, reach 32 (the light's own radius) -> alpha 1 - 7/32.
    EXPECT_NEAR(withView.out_[0].coneAndSeedAlpha_.y, 1.0f - 7.0f / 32.0f, 1e-5f);
    ASSERT_EQ(withView.states_.size(), 1u);
    EXPECT_EQ(withView.states_[0].state_, IRSystem::LightGatherState::BOUNDARY_DISCOUNTED);
    EXPECT_EQ(
        std::memcmp(
            &withView.out_[0],
            &withoutView.out_[0],
            sizeof(IRRender::GPULightSource)
        ),
        0
    );
}

TEST_F(LightVolumeBoundarySeedTest, OccludedClampRelocatesToNearestFaceCell) {
    const IREntity::EntityId canvas = IREntity::createEntity();
    makeLight(vec3(70.0f, 0.0f, 0.0f), 32);
    occludeVoxel(kWindowMax, 0, 0);

    const IRSystem::detail::LightOcclusionGridView occlusion = view();
    const GatherResult result = gather(canvas, &occlusion);

    ASSERT_EQ(result.count_, 1u);
    // Nearest-first, ties by (lower free axis, negative direction) — the first
    // dist-1 candidate on the clamped x face is (kWindowMax, -1, 0).
    EXPECT_EQ(seedCell(result.out_[0]), ivec3(kWindowMax, -1, 0));
    EXPECT_NEAR(result.out_[0].coneAndSeedAlpha_.y, 1.0f - 8.0f / 32.0f, 1e-5f);
    ASSERT_EQ(result.states_.size(), 1u);
    EXPECT_EQ(result.states_[0].state_, IRSystem::LightGatherState::BOUNDARY_RELOCATED);
    // Only the seed cell moves — the cone apex stays the light's true origin.
    EXPECT_EQ(static_cast<int>(result.out_[0].trueOriginVoxel_.x), 70);
    EXPECT_EQ(static_cast<int>(result.out_[0].trueOriginVoxel_.y), 0);
    EXPECT_EQ(static_cast<int>(result.out_[0].trueOriginVoxel_.z), 0);
}

// A view whose liveness stamp is clear must be treated as absent, not as an
// occupancy oracle. BUILD_LIGHT_OCCLUSION_GRID filters `C_VoxelPool` while
// COMPUTE_LIGHT_VOLUME filters `C_CanvasLightVolume`, so a scene where the
// consumer matches and the producer does not is reachable — and the mirror
// then holds an arbitrarily old `(origin_, bitfield)` pair. Answering from it
// would relocate a seed off a wall that may no longer exist; falling back to
// the no-view path degrades to today's behaviour instead. Same bits as
// `OccludedClampRelocatesToNearestFaceCell`, opposite outcome.
TEST_F(LightVolumeBoundarySeedTest, StaleViewIsIgnoredAndFallsBackToTodaysPath) {
    const IREntity::EntityId canvas = IREntity::createEntity();
    makeLight(vec3(70.0f, 0.0f, 0.0f), 32);
    occludeVoxel(kWindowMax, 0, 0);

    const IRSystem::detail::LightOcclusionGridView stale = staleView();
    EXPECT_FALSE(stale.valid());
    const GatherResult result = gather(canvas, &stale);
    const GatherResult withoutView = gather(canvas, nullptr);

    ASSERT_EQ(result.count_, 1u);
    // No relocation: the seed stays on the clamped cell, exactly as it does
    // with no view at all.
    EXPECT_EQ(seedCell(result.out_[0]), ivec3(kWindowMax, 0, 0));
    ASSERT_EQ(result.states_.size(), 1u);
    EXPECT_EQ(result.states_[0].state_, IRSystem::LightGatherState::BOUNDARY_DISCOUNTED);
    ASSERT_EQ(withoutView.count_, 1u);
    EXPECT_EQ(
        std::memcmp(&result.out_[0], &withoutView.out_[0], sizeof(IRRender::GPULightSource)),
        0
    );
}

// Parity with the propagate gate: it rejects a neighbour on
// `voxelOcclusionGetBit || lightBlockerGetBit`, so the seed-eligibility test
// must treat a blocker bit exactly like a voxel bit.
TEST_F(LightVolumeBoundarySeedTest, BlockerBitIsAsOccludingAsVoxelBit) {
    const IREntity::EntityId canvas = IREntity::createEntity();
    makeLight(vec3(70.0f, 0.0f, 0.0f), 32);
    occludeBlocker(kWindowMax, 0, 0);

    const IRSystem::detail::LightOcclusionGridView occlusion = view();
    const GatherResult result = gather(canvas, &occlusion);

    ASSERT_EQ(result.count_, 1u);
    EXPECT_EQ(seedCell(result.out_[0]), ivec3(kWindowMax, -1, 0));
    EXPECT_NEAR(result.out_[0].coneAndSeedAlpha_.y, 1.0f - 8.0f / 32.0f, 1e-5f);
    ASSERT_EQ(result.states_.size(), 1u);
    EXPECT_EQ(result.states_[0].state_, IRSystem::LightGatherState::BOUNDARY_RELOCATED);
}

// The face-only rule. Seeding an interior cell would light the far side of an
// in-window wall — the propagate gate models that geometry, so a seed placed
// past it is light-through-wall, strictly worse than the pop this change
// removes. With the whole clamped face occluded there is no legal candidate,
// so policy (a) applies: skip the light rather than walk inward.
TEST_F(LightVolumeBoundarySeedTest, NeverRelocatesIntoTheInterior) {
    const IREntity::EntityId canvas = IREntity::createEntity();
    makeLight(vec3(70.0f, 0.0f, 0.0f), 32);
    for (int y = -IRComponents::kLightVolumeHalfExtent; y <= kWindowMax; ++y) {
        for (int z = -IRComponents::kLightVolumeHalfExtent; z <= kWindowMax; ++z) {
            occludeVoxel(kWindowMax, y, z);
        }
    }

    const IRSystem::detail::LightOcclusionGridView occlusion = view();
    const GatherResult result = gather(canvas, &occlusion);

    EXPECT_EQ(result.count_, 0u);
    EXPECT_TRUE(result.out_.empty());
    ASSERT_EQ(result.states_.size(), 1u);
    EXPECT_EQ(result.states_[0].state_, IRSystem::LightGatherState::SKIPPED_OCCLUDED);
    EXPECT_FLOAT_EQ(result.states_[0].residual_, 0.0f);
}

// The enumeration order is part of the contract: render-verify compares
// captured pixels, so two equidistant free cells must not swap between
// frames or hosts. Walk the whole dist-1 ring of the clamped x face and pin
// each successive pick.
TEST_F(LightVolumeBoundarySeedTest, DeterministicTieBreak) {
    const IREntity::EntityId canvas = IREntity::createEntity();
    makeLight(vec3(70.0f, 0.0f, 0.0f), 32);
    const IRSystem::detail::LightOcclusionGridView occlusion = view();

    const ivec3 ringInOrder[] = {
        ivec3(kWindowMax, -1, 0),
        ivec3(kWindowMax, 0, -1),
        ivec3(kWindowMax, 0, 1),
        ivec3(kWindowMax, 1, 0),
    };

    occludeVoxel(kWindowMax, 0, 0);
    for (const ivec3 &expected : ringInOrder) {
        const GatherResult result = gather(canvas, &occlusion);
        ASSERT_EQ(result.count_, 1u);
        EXPECT_EQ(seedCell(result.out_[0]), expected);
        occludeVoxel(expected.x, expected.y, expected.z);
    }
}

// The search bound is the light's own remaining residual, not a constant: a
// cell farther than that would seed at alpha <= 0 and be dropped by the
// propagate anyway. radius 8 with boundaryDist 7 leaves maxDist 0, so an
// occluded clamp has nowhere legal to go.
TEST_F(LightVolumeBoundarySeedTest, SearchStopsAtRemainingReach) {
    const IREntity::EntityId canvas = IREntity::createEntity();
    makeSpotLight(vec3(70.0f, 0.0f, 0.0f), 8);
    occludeVoxel(kWindowMax, 0, 0);

    const IRSystem::detail::LightOcclusionGridView occlusion = view();
    const GatherResult result = gather(canvas, &occlusion);

    EXPECT_EQ(result.count_, 0u);
    ASSERT_EQ(result.states_.size(), 1u);
    EXPECT_EQ(result.states_[0].state_, IRSystem::LightGatherState::SKIPPED_OCCLUDED);
    // A skipped spot must not arm the consumer's winning-light-ID read (#2318).
    EXPECT_FALSE(result.hasSpot_);
}

// A corner clamp touches two faces; "nearest" has to hold across both, so
// every touched face is enumerated at each distance before the radius grows.
TEST_F(LightVolumeBoundarySeedTest, CornerClampEnumeratesEveryTouchedFace) {
    const IREntity::EntityId canvas = IREntity::createEntity();
    makeLight(vec3(70.0f, 70.0f, 0.0f), 32);
    // The clamp itself plus every dist-1 candidate on the x face (the +y
    // neighbour is outside the window), leaving the y face's first candidate
    // as the pick.
    occludeVoxel(kWindowMax, kWindowMax, 0);
    occludeVoxel(kWindowMax, kWindowMax - 1, 0);
    occludeVoxel(kWindowMax, kWindowMax, -1);
    occludeVoxel(kWindowMax, kWindowMax, 1);

    const IRSystem::detail::LightOcclusionGridView occlusion = view();
    const GatherResult result = gather(canvas, &occlusion);

    ASSERT_EQ(result.count_, 1u);
    EXPECT_EQ(seedCell(result.out_[0]), ivec3(kWindowMax - 1, kWindowMax, 0));
    // boundaryDist 14 from the corner clamp, +1 for the relocation step.
    EXPECT_NEAR(result.out_[0].coneAndSeedAlpha_.y, 1.0f - 15.0f / 32.0f, 1e-5f);
    ASSERT_EQ(result.states_.size(), 1u);
    EXPECT_EQ(result.states_[0].state_, IRSystem::LightGatherState::BOUNDARY_RELOCATED);
}

// Scope guard: only clamped seeds are relocated. An in-window light sits at
// its true origin whatever the occupancy there — moving it would change a
// path this change has no business touching.
TEST_F(LightVolumeBoundarySeedTest, InWindowOriginIsNeverTested) {
    const IREntity::EntityId canvas = IREntity::createEntity();
    makeLight(vec3(10.0f, 0.0f, 0.0f), 32);
    occludeVoxel(10, 0, 0);

    const IRSystem::detail::LightOcclusionGridView occlusion = view();
    const GatherResult result = gather(canvas, &occlusion);

    ASSERT_EQ(result.count_, 1u);
    EXPECT_EQ(seedCell(result.out_[0]), ivec3(10, 0, 0));
    EXPECT_NEAR(result.out_[0].coneAndSeedAlpha_.y, 1.0f, 1e-5f);
    ASSERT_EQ(result.states_.size(), 1u);
    EXPECT_EQ(result.states_[0].state_, IRSystem::LightGatherState::SEEDED_FULL);
}

TEST_F(LightVolumeBoundarySeedTest, GridGetBitMirrorsSetBitAndOutOfRangeReadsFree) {
    occludeVoxel(5, -3, 7);

    EXPECT_TRUE(IRSystem::detail::gridGetBit(m_voxelBits, 5, -3, 7, kGridOrigin));
    EXPECT_FALSE(IRSystem::detail::gridGetBit(m_voxelBits, 5, -3, 8, kGridOrigin));
    // Out of the 256^3 grid entirely — the shader answers "not occluded" for
    // an out-of-range lookup, and so must the CPU mirror.
    constexpr int kGridHalf = IRSystem::kMaxLightOcclusionGridSideVoxels / 2;
    EXPECT_FALSE(IRSystem::detail::gridGetBit(m_voxelBits, kGridHalf, 0, 0, kGridOrigin));
    EXPECT_FALSE(IRSystem::detail::gridGetBit(m_voxelBits, 0, -kGridHalf - 1, 0, kGridOrigin));
}
