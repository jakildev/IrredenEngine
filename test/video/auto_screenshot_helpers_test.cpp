#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/system/ir_system_types.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/video/auto_screenshot.hpp>

#include <list>
#include <vector>

// Coverage for the auto-screenshot registration helpers: the fixed-array
// binder (`setAutoScreenshotShots`) and the common wire-up
// (`appendAutoScreenshotIfRequested`). Both deduce the shot count from the
// array they bind, so `shots_` and `numShots_` cannot name different tables.
// A hand-written `numShots_ = sizeof(kFoo) / sizeof(kFoo[0])` can, and nothing
// catches it until the capture run walks off the end.
//
// Only the *inactive* half of `appendAutoScreenshotIfRequested` is unit
// testable. Its active branch calls `createAutoScreenshotSystem`, which needs
// a live system manager AND latches the process-lifetime `g_autoCaptureActive`
// flag (`engine/video/src/auto_screenshot.cpp`) that `World` reads to switch
// the UPDATE loop to a fixed step — so one active call here would change how
// every World-constructing test later in this binary ticks, order-dependently.
// The active path's proof stays the representative `--auto-screenshot` run a
// render PR attaches.

namespace {

using IRVideo::AutoScreenshotConfig;
using IRVideo::AutoScreenshotShot;

const AutoScreenshotShot kThreeShots[] = {
    {.zoom_ = 1.0f, .label_ = "first"},
    {.zoom_ = 2.0f, .label_ = "second"},
    {.zoom_ = 4.0f, .label_ = "third"},
};

const AutoScreenshotShot kOneShot[] = {
    {.zoom_ = 8.0f, .label_ = "only"},
};

void onCaptureFrameNoop(int) {}

// Sentinel ids for the pipeline-untouched assertions — any value works, the
// helper never reads them; only that it neither pushes nor clears.
constexpr IRSystem::SystemId kSentinelA = 41;
constexpr IRSystem::SystemId kSentinelB = 42;

// ─────────────────────────────────────────────
// setAutoScreenshotShots — the fixed-array binder
// ─────────────────────────────────────────────

TEST(AutoScreenshotBinderTest, BindsTableAndDeducesCount) {
    AutoScreenshotConfig config{};

    IRVideo::setAutoScreenshotShots(config, kThreeShots);

    EXPECT_EQ(config.shots_, kThreeShots);
    EXPECT_EQ(config.numShots_, 3);
}

TEST(AutoScreenshotBinderTest, DeducesCountForSingleElementTable) {
    AutoScreenshotConfig config{};

    IRVideo::setAutoScreenshotShots(config, kOneShot);

    EXPECT_EQ(config.shots_, kOneShot);
    EXPECT_EQ(config.numShots_, 1);
}

TEST(AutoScreenshotBinderTest, MixedPoseTablePreservesExplicitSpatialOptIn) {
    const AutoScreenshotShot shots[]{
        {.yawRadians_ = 0.5f},
        {.hasViewRotation_ = true,
         .viewRotation_ = IRMath::uprightOrbitRotation(0.7f, -0.4f, 0.2f)},
    };
    AutoScreenshotConfig config{};
    IRVideo::setAutoScreenshotShots(config, shots);
    ASSERT_EQ(config.numShots_, 2);
    EXPECT_FALSE(config.shots_[0].hasViewRotation_);
    EXPECT_EQ(config.shots_[0].viewRotation_, IRMath::vec4(0, 0, 0, 1));
    EXPECT_FLOAT_EQ(config.shots_[0].yawRadians_, 0.5f);
    EXPECT_TRUE(config.shots_[1].hasViewRotation_);
    EXPECT_EQ(config.shots_[1].viewRotation_, shots[1].viewRotation_);
}

class AutoScreenshotCameraPolicyTest : public testing::Test {
  protected:
    AutoScreenshotCameraPolicyTest() {
        const auto camera = IREntity::createEntity(IRComponents::C_Camera{});
        IREntity::setName(camera, "camera");
    }

    IREntity::EntityManager m_entityManager;
};

TEST_F(AutoScreenshotCameraPolicyTest, YawOnlyShotRestoresTheCallerPolicyAfterASpatialShot) {
    IRPrefab::Camera::setYawPitch(-0.3f, 0.25f);
    const IRVideo::detail::AutoScreenshotCameraBaseline baseline{
        IRPrefab::Camera::isSpatialViewEnabled(),
        IRPrefab::Camera::getRotationQuat(),
    };
    const AutoScreenshotShot spatialShot{
        .hasViewRotation_ = true,
        .viewRotation_ = IRMath::uprightOrbitRotation(0.7f, -0.4f, 0.2f),
    };
    const AutoScreenshotShot yawOnlyShot{.yawRadians_ = 0.5f};

    IRVideo::detail::applyCameraPolicyForShot(spatialShot, baseline);
    ASSERT_TRUE(IRPrefab::Camera::isSpatialViewEnabled());
    ASSERT_EQ(IRPrefab::Camera::getRotationQuat(), spatialShot.viewRotation_);
    IRVideo::detail::applyCameraPolicyForShot(yawOnlyShot, baseline);
    const bool afterSpatialMode = IRPrefab::Camera::isSpatialViewEnabled();
    const IRMath::vec4 afterSpatialRotation = IRPrefab::Camera::getRotationQuat();

    IRPrefab::Camera::setSpatialViewEnabled(baseline.spatialViewEnabled_);
    IRPrefab::Camera::setRotationQuat(baseline.rotation_);
    IRVideo::detail::applyCameraPolicyForShot(yawOnlyShot, baseline);

    EXPECT_EQ(afterSpatialMode, IRPrefab::Camera::isSpatialViewEnabled());
    EXPECT_EQ(afterSpatialRotation, IRPrefab::Camera::getRotationQuat());
    EXPECT_FALSE(afterSpatialMode);
    EXPECT_FLOAT_EQ(IRPrefab::Camera::getYaw(), yawOnlyShot.yawRadians_);
    EXPECT_FLOAT_EQ(IRPrefab::Camera::getPitch(), 0.25f);
}

// The seam the helper closes: a rebind must move the pointer and the count
// together. A hand-written pairing is two independent edits, so re-pointing
// `shots_` while leaving a larger `numShots_` behind reads past the table.
TEST(AutoScreenshotBinderTest, RebindMovesPointerAndCountTogether) {
    AutoScreenshotConfig config{};
    IRVideo::setAutoScreenshotShots(config, kThreeShots);

    IRVideo::setAutoScreenshotShots(config, kOneShot);

    EXPECT_EQ(config.shots_, kOneShot);
    EXPECT_EQ(config.numShots_, 1);
}

// The binder is for configurators that also set the rest of the config
// surface, so it must touch exactly the two fields it owns.
TEST(AutoScreenshotBinderTest, LeavesUnrelatedConfigFieldsUntouched) {
    AutoScreenshotConfig config{};
    config.warmupFrames_ = 25;
    config.settleFrames_ = 7;
    config.onCaptureFrame_ = &onCaptureFrameNoop;

    IRVideo::setAutoScreenshotShots(config, kThreeShots);

    EXPECT_EQ(config.warmupFrames_, 25);
    EXPECT_EQ(config.settleFrames_, 7);
    EXPECT_EQ(config.onCaptureFrame_, &onCaptureFrameNoop);
}

// Walk the bound table the way the cycling system does — `shots_[i]` for
// `i` in `[0, numShots_)` — so the assertion covers the pointer and the count
// agreeing in use, not just field-by-field.
TEST(AutoScreenshotBinderTest, BoundTableIsIndexableAcrossTheDeducedCount) {
    AutoScreenshotConfig config{};

    IRVideo::setAutoScreenshotShots(config, kThreeShots);

    ASSERT_EQ(config.numShots_, 3);
    EXPECT_STREQ(config.shots_[0].label_, "first");
    EXPECT_STREQ(config.shots_[1].label_, "second");
    EXPECT_STREQ(config.shots_[2].label_, "third");
    EXPECT_FLOAT_EQ(config.shots_[2].zoom_, 4.0f);
}

// ─────────────────────────────────────────────
// appendAutoScreenshotIfRequested — the inactive (no `--auto-screenshot`) path
// ─────────────────────────────────────────────

TEST(AutoScreenshotAppendTest, PushesNothingWhenWarmupFramesIsZero) {
    std::vector<IRSystem::SystemId> pipeline;

    IRVideo::appendAutoScreenshotIfRequested(pipeline, 0, kThreeShots);

    EXPECT_TRUE(pipeline.empty());
}

// `autoScreenshotWarmupFrames()` returns 0 when `--auto-screenshot` is absent,
// so 0 is the ordinary inactive case. The guard is `<= 0` rather than `== 0` so
// an explicitly negative count stays inactive instead of becoming a warmup
// countdown that never reaches zero.
TEST(AutoScreenshotAppendTest, PushesNothingWhenWarmupFramesIsNegative) {
    std::vector<IRSystem::SystemId> pipeline;

    IRVideo::appendAutoScreenshotIfRequested(pipeline, -1, kThreeShots);

    EXPECT_TRUE(pipeline.empty());
}

// Callers append to a pipeline the camera prefab already populated, so the
// no-op must leave the existing entries alone, not just add nothing.
TEST(AutoScreenshotAppendTest, InactiveCallLeavesExistingPipelineEntriesIntact) {
    std::vector<IRSystem::SystemId> pipeline{kSentinelA, kSentinelB};

    IRVideo::appendAutoScreenshotIfRequested(pipeline, 0, kThreeShots);

    ASSERT_EQ(pipeline.size(), 2u);
    EXPECT_EQ(pipeline[0], kSentinelA);
    EXPECT_EQ(pipeline[1], kSentinelB);
}

// `Pipeline` is deduced rather than fixed to `std::vector` because every demo
// RENDER pipeline is the `std::list<SystemId>` that
// `IRPrefab::Camera::standardControlSystems()` returns. Instantiating the
// helper against a list is the assertion — a re-tightening of the parameter
// to a concrete container would not compile this TU.
TEST(AutoScreenshotAppendTest, InactiveCallAcceptsAListPipeline) {
    std::list<IRSystem::SystemId> pipeline{kSentinelA};

    IRVideo::appendAutoScreenshotIfRequested(pipeline, 0, kOneShot);

    ASSERT_EQ(pipeline.size(), 1u);
    EXPECT_EQ(pipeline.front(), kSentinelA);
}

// The explicit `settleFrames` overload is the same inactive guard — it must
// not push either, and the guard must run before the config is built.
TEST(AutoScreenshotAppendTest, PushesNothingWithAnExplicitSettleFramesArgument) {
    std::vector<IRSystem::SystemId> pipeline;

    IRVideo::appendAutoScreenshotIfRequested(pipeline, 0, kThreeShots, 9);

    EXPECT_TRUE(pipeline.empty());
}

} // namespace
