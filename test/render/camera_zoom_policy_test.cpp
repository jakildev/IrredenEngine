#include <gtest/gtest.h>

#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/components/component_camera_zoom_frame_state.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/systems/system_sprites_to_screen.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>

#include <cmath>
#include <utility>

namespace {

using IRComponents::C_Camera;
using IRComponents::C_CameraZoomFrameState;
using IRComponents::C_TrixelCanvasRenderBehavior;
using IRComponents::C_ZoomLevel;
using IRMath::dvec2;
using IRMath::ivec2;
using IRMath::vec2;
using IRRender::SubdivisionMode;
using IRRender::ZoomDensityRounding;

// The named camera entity as the render manager builds it, minus everything
// that needs a GPU: the policy, the zoom value, and the transient frame state.
class CameraZoomPolicyTest : public testing::Test {
  protected:
    CameraZoomPolicyTest() {
        m_camera = IREntity::createEntity(C_Camera{}, C_ZoomLevel{1.0f}, C_CameraZoomFrameState{});
        IREntity::setName(m_camera, "camera");
    }

    vec2 &zoom() {
        return IREntity::getComponent<C_ZoomLevel>(m_camera).zoom_;
    }

    C_CameraZoomFrameState &frameState() {
        return IREntity::getComponent<C_CameraZoomFrameState>(m_camera);
    }

    IREntity::EntityManager m_entity_manager;
    IREntity::EntityId m_camera = IREntity::kNullEntity;
};

// --- what a zoom write stores ------------------------------------------------

TEST(CameraZoomRequest, SnappedPolicyRoundsToAPowerOfTwo) {
    EXPECT_FLOAT_EQ(IRPrefab::Camera::resolveZoomRequest(2.5f, false), 2.0f);
    EXPECT_FLOAT_EQ(IRPrefab::Camera::resolveZoomRequest(3.0f, false), 4.0f);
    EXPECT_FLOAT_EQ(IRPrefab::Camera::resolveZoomRequest(4.0f, false), 4.0f);
}

TEST(CameraZoomRequest, ContinuousPolicyStoresTheRequestExactly) {
    for (const float requested : {1.0f, 1.37f, 2.5f, 3.3f, 4.0f, 17.25f, 64.0f}) {
        EXPECT_EQ(IRPrefab::Camera::resolveZoomRequest(requested, true), requested);
    }
}

TEST(CameraZoomRequest, BothPoliciesClampToTheEngineRange) {
    const float zoomMin = IRConstants::kTrixelCanvasZoomMin.x;
    const float zoomMax = IRConstants::kTrixelCanvasZoomMax.x;
    for (const bool continuous : {false, true}) {
        EXPECT_FLOAT_EQ(IRPrefab::Camera::resolveZoomRequest(0.2f, continuous), zoomMin);
        EXPECT_FLOAT_EQ(IRPrefab::Camera::resolveZoomRequest(1000.0f, continuous), zoomMax);
    }
}

// --- raster density ----------------------------------------------------------

// A request of 2.5 stores 2 under the snapped policy and 2.5 under the
// continuous one; the density each renders at follows from the stored value.
TEST(CameraZoomDensity, FullModeRoundsUpOnlyUnderTheContinuousPolicy) {
    const vec2 snapped(IRPrefab::Camera::resolveZoomRequest(2.5f, false));
    const vec2 continuous(IRPrefab::Camera::resolveZoomRequest(2.5f, true));
    EXPECT_EQ(
        IRRender::voxelRenderEffectiveSubdivisions(
            SubdivisionMode::FULL,
            1,
            snapped,
            ZoomDensityRounding::NEAREST
        ),
        2
    );
    EXPECT_EQ(
        IRRender::voxelRenderEffectiveSubdivisions(
            SubdivisionMode::FULL,
            1,
            continuous,
            ZoomDensityRounding::UP
        ),
        3
    );
}

// The two rules on the same zoom: the rounding is the policy's, not the
// value's. NEAREST is also what every explicit (secondary viewport) zoom takes.
TEST(CameraZoomDensity, RoundingRulesDifferOnTheSameZoom) {
    const vec2 zoom(2.3f);
    EXPECT_EQ(
        IRRender::voxelRenderEffectiveSubdivisions(
            SubdivisionMode::FULL,
            1,
            zoom,
            ZoomDensityRounding::NEAREST
        ),
        2
    );
    EXPECT_EQ(
        IRRender::voxelRenderEffectiveSubdivisions(
            SubdivisionMode::FULL,
            1,
            zoom,
            ZoomDensityRounding::UP
        ),
        3
    );
    EXPECT_EQ(
        IRRender::voxelRenderEffectiveSubdivisions(
            SubdivisionMode::FULL,
            2,
            zoom,
            ZoomDensityRounding::UP
        ),
        6
    );
}

TEST(CameraZoomDensity, RoundingRulesAgreeAtAPowerOfTwo) {
    for (const float zoom : {1.0f, 2.0f, 4.0f, 8.0f}) {
        EXPECT_EQ(
            IRRender::voxelRenderEffectiveSubdivisions(
                SubdivisionMode::FULL,
                1,
                vec2(zoom),
                ZoomDensityRounding::UP
            ),
            IRRender::voxelRenderEffectiveSubdivisions(
                SubdivisionMode::FULL,
                1,
                vec2(zoom),
                ZoomDensityRounding::NEAREST
            )
        );
    }
}

TEST(CameraZoomDensity, NoneAndPositionOnlyIgnoreTheZoomAndTheRounding) {
    for (const ZoomDensityRounding rounding :
         {ZoomDensityRounding::NEAREST, ZoomDensityRounding::UP}) {
        EXPECT_EQ(
            IRRender::voxelRenderEffectiveSubdivisions(
                SubdivisionMode::NONE,
                4,
                vec2(2.5f),
                rounding
            ),
            1
        );
        EXPECT_EQ(
            IRRender::voxelRenderEffectiveSubdivisions(
                SubdivisionMode::POSITION_ONLY,
                4,
                vec2(2.5f),
                rounding
            ),
            4
        );
    }
}

TEST(CameraZoomDensity, FullModeClampsAtSixteen) {
    EXPECT_EQ(
        IRRender::voxelRenderEffectiveSubdivisions(
            SubdivisionMode::FULL,
            4,
            vec2(7.5f),
            ZoomDensityRounding::UP
        ),
        16
    );
}

// --- policy transitions ------------------------------------------------------

TEST_F(CameraZoomPolicyTest, DefaultsToTheSnappedPolicy) {
    EXPECT_FALSE(IRPrefab::Camera::isZoomContinuous());
    EXPECT_FALSE(C_Camera{}.continuousZoom_);
}

TEST_F(CameraZoomPolicyTest, EnablingKeepsTheStoredZoom) {
    zoom() = vec2(4.0f);
    IRPrefab::Camera::setZoomContinuous(true);
    EXPECT_TRUE(IRPrefab::Camera::isZoomContinuous());
    EXPECT_EQ(zoom(), vec2(4.0f));
}

TEST_F(CameraZoomPolicyTest, DisablingSnapsEachAxisOnItsOwn) {
    IRPrefab::Camera::setZoomContinuous(true);
    zoom() = vec2(2.5f, 6.0f);
    IRPrefab::Camera::setZoomContinuous(false);
    EXPECT_FALSE(IRPrefab::Camera::isZoomContinuous());
    EXPECT_EQ(zoom(), vec2(2.0f, 8.0f));
}

TEST_F(CameraZoomPolicyTest, ARedundantSetLeavesTheZoomAndThePhaseAlone) {
    zoom() = vec2(3.0f, 3.0f);
    IRPrefab::Camera::setZoomContinuous(false);
    EXPECT_EQ(zoom(), vec2(3.0f, 3.0f));

    IRPrefab::Camera::setZoomContinuous(true);
    IRPrefab::Camera::prepareZoomFrame(vec2(17.3f), vec2(2.5f));
    IRPrefab::Camera::prepareZoomFrame(vec2(17.3f), vec2(2.7f));
    const dvec2 carried = frameState().sample_.phase_;
    ASSERT_NE(carried, dvec2(0.0));
    IRPrefab::Camera::setZoomContinuous(true);
    EXPECT_EQ(frameState().sample_.phase_, carried);
}

TEST_F(CameraZoomPolicyTest, APolicyChangeDropsTheCarriedPhase) {
    IRPrefab::Camera::setZoomContinuous(true);
    IRPrefab::Camera::prepareZoomFrame(vec2(17.3f), vec2(2.5f));
    IRPrefab::Camera::prepareZoomFrame(vec2(17.3f), vec2(2.7f));
    ASSERT_TRUE(frameState().published_);

    IRPrefab::Camera::setZoomContinuous(false);
    EXPECT_FALSE(frameState().published_);
    EXPECT_EQ(frameState().sample_.phase_, dvec2(0.0));

    // Re-enabling starts a fresh history: the first prepared frame has no
    // prior sample to advance from.
    IRPrefab::Camera::setZoomContinuous(true);
    IRPrefab::Camera::prepareZoomFrame(vec2(17.3f), vec2(2.7f));
    EXPECT_TRUE(frameState().published_);
    EXPECT_EQ(frameState().sample_.phase_, dvec2(0.0));
}

// --- zoom steps --------------------------------------------------------------

TEST_F(CameraZoomPolicyTest, SnappedStepsAreTheComponentMethods) {
    // A directly written anisotropic, non-power-of-two zoom is part of the
    // snapped policy's contract: the step doubles and rounds each axis and
    // never collapses them to one uniform snapped value.
    zoom() = vec2(3.0f, 5.0f);
    IRPrefab::Camera::zoomIn();
    EXPECT_EQ(zoom(), vec2(6.0f, 10.0f));
    IRPrefab::Camera::zoomOut();
    EXPECT_EQ(zoom(), vec2(3.0f, 5.0f));

    C_ZoomLevel reference{vec2(3.0f, 5.0f)};
    reference.zoomOut();
    IRPrefab::Camera::zoomOut();
    EXPECT_EQ(zoom(), reference.zoom_);
}

TEST_F(CameraZoomPolicyTest, ContinuousStepsDoubleAndHalveWithoutRounding) {
    IRPrefab::Camera::setZoomContinuous(true);
    zoom() = vec2(2.5f);
    IRPrefab::Camera::zoomIn();
    EXPECT_EQ(zoom(), vec2(5.0f));
    IRPrefab::Camera::zoomOut();
    IRPrefab::Camera::zoomOut();
    EXPECT_EQ(zoom(), vec2(1.25f));
    IRPrefab::Camera::zoomOut();
    EXPECT_EQ(zoom(), IRConstants::kTrixelCanvasZoomMin);

    zoom() = vec2(40.0f);
    IRPrefab::Camera::zoomIn();
    EXPECT_EQ(zoom(), IRConstants::kTrixelCanvasZoomMax);
}

// --- the published frame -----------------------------------------------------

TEST_F(CameraZoomPolicyTest, SnappedPolicyPublishesNothing) {
    IRPrefab::Camera::prepareZoomFrame(vec2(17.3f), vec2(4.0f));
    EXPECT_FALSE(frameState().published_);
    EXPECT_EQ(IRPrefab::Camera::zoomFrame(vec2(17.3f), vec2(4.0f)), nullptr);
}

TEST_F(CameraZoomPolicyTest, AConsumerGetsNoFrameUntilOneIsPrepared) {
    IRPrefab::Camera::setZoomContinuous(true);
    EXPECT_EQ(IRPrefab::Camera::zoomFrame(vec2(17.3f), vec2(2.5f)), nullptr);

    IRPrefab::Camera::prepareZoomFrame(vec2(17.3f), vec2(2.5f));
    const IRMath::CameraRasterPhase *frame = IRPrefab::Camera::zoomFrame(vec2(17.3f), vec2(2.5f));
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->cameraIso_, dvec2(vec2(17.3f)));
    EXPECT_EQ(frame->pitch_, dvec2(5.0, 2.5));
    EXPECT_EQ(frame->phase_, dvec2(0.0));
}

// A stage that runs with no prepare ahead of it this frame — a pipeline
// without the trixel composite, or a camera write after it — sees a sample
// for a different pose and must fall back to the snapped placement.
TEST_F(CameraZoomPolicyTest, AFrameForAnotherPoseIsNotHandedOut) {
    IRPrefab::Camera::setZoomContinuous(true);
    IRPrefab::Camera::prepareZoomFrame(vec2(17.3f), vec2(2.5f));
    EXPECT_EQ(IRPrefab::Camera::zoomFrame(vec2(17.4f), vec2(2.5f)), nullptr);
    EXPECT_EQ(IRPrefab::Camera::zoomFrame(vec2(17.3f), vec2(2.6f)), nullptr);
    EXPECT_NE(IRPrefab::Camera::zoomFrame(vec2(17.3f), vec2(2.5f)), nullptr);
}

TEST_F(CameraZoomPolicyTest, ASecondPrepareInOneFrameRepublishesTheSameSample) {
    IRPrefab::Camera::setZoomContinuous(true);
    IRPrefab::Camera::prepareZoomFrame(vec2(17.3f), vec2(2.5f));
    IRPrefab::Camera::prepareZoomFrame(vec2(17.3f), vec2(2.7f));
    const IRMath::CameraRasterPhase first = frameState().sample_;
    IRPrefab::Camera::prepareZoomFrame(vec2(17.3f), vec2(2.7f));
    EXPECT_EQ(frameState().sample_.phase_, first.phase_);
    EXPECT_EQ(frameState().sample_.cameraIso_, first.cameraIso_);
    EXPECT_EQ(frameState().sample_.pitch_, first.pitch_);
}

TEST_F(CameraZoomPolicyTest, APanKeepsThePhaseAndAZoomCarriesIt) {
    IRPrefab::Camera::setZoomContinuous(true);
    IRPrefab::Camera::prepareZoomFrame(vec2(17.3f), vec2(2.5f));
    IRPrefab::Camera::prepareZoomFrame(vec2(18.9f), vec2(2.5f));
    EXPECT_EQ(frameState().sample_.phase_, dvec2(0.0));

    // The update moves the phase by `previous camera * pitch delta`, which
    // leaves `camera * pitch - phase` where it was up to a whole pixel.
    const dvec2 offsetBefore = frameState().sample_.cameraIso_ * frameState().sample_.pitch_ -
                               frameState().sample_.phase_;
    IRPrefab::Camera::prepareZoomFrame(vec2(18.9f), vec2(2.7f));
    const dvec2 offsetAfter = frameState().sample_.cameraIso_ * frameState().sample_.pitch_ -
                              frameState().sample_.phase_;
    EXPECT_NE(frameState().sample_.phase_, dvec2(0.0));
    for (int axis = 0; axis < 2; ++axis) {
        const double moved = offsetAfter[axis] - offsetBefore[axis];
        EXPECT_NEAR(moved, std::round(moved), 1e-9) << "axis=" << axis;
    }
}

TEST_F(CameraZoomPolicyTest, ScreenResidualFollowsThePublishedFrame) {
    const vec2 camera(17.3f, -4.7f);
    const vec2 zoomValue(2.5f);
    const ivec2 scale(3, 3);
    EXPECT_EQ(
        IRPrefab::Camera::screenResidual(camera, zoomValue, scale),
        IRMath::cameraSubPixelOffsets(camera, zoomValue, scale).screenPxResidual_
    );

    IRPrefab::Camera::setZoomContinuous(true);
    IRPrefab::Camera::prepareZoomFrame(camera, vec2(2.3f));
    IRPrefab::Camera::prepareZoomFrame(camera, zoomValue);
    const IRMath::CameraRasterPhase *frame = IRPrefab::Camera::zoomFrame(camera, zoomValue);
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(
        IRPrefab::Camera::screenResidual(camera, zoomValue, scale),
        IRMath::cameraRasterScreenResidual(*frame, scale)
    );
}

// --- which canvases the camera places ----------------------------------------

using TrixelToFramebuffer = IRSystem::System<IRSystem::TRIXEL_TO_FRAMEBUFFER>;

C_TrixelCanvasRenderBehavior canvasBehavior(bool useCameraPosition, bool useCameraZoom) {
    C_TrixelCanvasRenderBehavior behavior{};
    behavior.useCameraPositionIso_ = useCameraPosition;
    behavior.useCameraZoom_ = useCameraZoom;
    behavior.parityOffsetIsoX_ = 0.0f;
    behavior.parityOffsetIsoY_ = 0.0f;
    return behavior;
}

IRMath::CameraRasterPhase carriedFrame() {
    const IRMath::CameraRasterPhase first{
        dvec2(vec2(17.3f)),
        IRMath::cameraZoomPitch(vec2(2.3f)),
        dvec2(0.0)
    };
    return IRMath::advanceCameraRasterPhase(first, vec2(17.3f), vec2(2.5f));
}

// The GUI canvas and any fixed overlay ignore the camera. Handed the frame's
// sample they must place exactly as they do with none, or they would shift by
// the carried phase every time the zoom moved.
TEST(CameraZoomCanvasPlacement, ACameraIndependentCanvasIgnoresTheFrame) {
    const IRMath::CameraRasterPhase frame = carriedFrame();
    ASSERT_NE(frame.phase_, dvec2(0.0));
    const vec2 canvasZoom(1.0f);
    for (const auto &[usePosition, useZoom] : {
             std::pair{false, false},
             std::pair{true, false},
             std::pair{false, true},
         }) {
        const C_TrixelCanvasRenderBehavior behavior = canvasBehavior(usePosition, useZoom);
        const vec2 canvasCamera = usePosition ? vec2(17.3f) : vec2(0.0f);
        EXPECT_EQ(
            TrixelToFramebuffer::canvasGatherTranslation(
                &frame,
                behavior,
                canvasCamera,
                canvasZoom,
                1
            ),
            TrixelToFramebuffer::canvasGatherTranslation(
                nullptr,
                behavior,
                canvasCamera,
                canvasZoom,
                1
            )
        ) << "usePosition=" << usePosition << " useZoom=" << useZoom;
    }
}

TEST(CameraZoomCanvasPlacement, ACameraFollowingCanvasPlacesFromTheFrame) {
    const IRMath::CameraRasterPhase frame = carriedFrame();
    const C_TrixelCanvasRenderBehavior behavior = canvasBehavior(true, true);
    const vec2 canvasCamera(17.3f);
    const int density = 3;
    const vec2 placed = TrixelToFramebuffer::canvasGatherTranslation(
        &frame,
        behavior,
        canvasCamera,
        vec2(2.5f / density),
        density
    );
    EXPECT_EQ(
        placed,
        vec2(
            IRMath::cameraRasterGatherTranslation(
                frame,
                canvasCamera * static_cast<float>(density),
                vec2(0.0f),
                density
            )
        )
    );
    EXPECT_NE(
        placed,
        TrixelToFramebuffer::canvasGatherTranslation(
            nullptr,
            behavior,
            canvasCamera,
            vec2(2.5f / density),
            density
        )
    );
}

// A parity offset moves that one canvas's content by a fixed number of iso
// units and nothing else: the shared whole offset stays the camera's.
TEST(CameraZoomCanvasPlacement, AParityOffsetShiftsOnlyItsOwnCanvas) {
    const IRMath::CameraRasterPhase frame = carriedFrame();
    const vec2 rasterCamera = vec2(17.3f) * 3.0f;
    const dvec2 plain =
        IRMath::cameraRasterGatherTranslation(frame, rasterCamera, vec2(0.0f), 3);
    const dvec2 shifted =
        IRMath::cameraRasterGatherTranslation(frame, rasterCamera, vec2(0.5f, 1.0f), 3);
    const dvec2 expected =
        dvec2(0.5, 1.0) * frame.pitch_ * dvec2(IRPlatform::kIsoToScreenSign);
    EXPECT_NEAR(shifted.x - plain.x, expected.x, 1e-9);
    EXPECT_NEAR(shifted.y - plain.y, expected.y, 1e-9);
}

// The rotated-view scatter adds the camera's sub-cell offset on top of whole
// anchor cells. Under the snapped policy that offset is unrounded; under the
// continuous one it is the frame's whole-pixel offset plus carried phase, so
// the upscale residual is the remainder of the same quantity.
TEST(CameraZoomCanvasPlacement, RotatedScatterSharesTheFrameOffset) {
    const vec2 camera(17.3f, 4.6f);
    const vec2 pxPerCell(5.0f, 2.5f);
    EXPECT_EQ(
        TrixelToFramebuffer::perAxisScatterCameraOffset(nullptr, camera, pxPerCell),
        vec2(IRMath::fract(camera.x) * 5.0f, -IRMath::fract(camera.y) * 2.5f)
    );

    const IRMath::CameraRasterPhase frame = IRMath::advanceCameraRasterPhase(
        IRMath::CameraRasterPhase{
            dvec2(camera),
            IRMath::cameraZoomPitch(vec2(2.3f)),
            dvec2(0.0)
        },
        camera,
        vec2(2.5f)
    );
    const vec2 placed =
        TrixelToFramebuffer::perAxisScatterCameraOffset(&frame, camera, pxPerCell);
    EXPECT_EQ(placed, vec2(IRMath::cameraRasterDetachedOffset(frame)));

    // Anchor cells plus this offset, plus the residual's source fraction, is
    // the raw camera offset: nothing counted twice, nothing dropped.
    const dvec2 offset = dvec2(placed.x, -placed.y);
    const dvec2 whole = IRMath::floor(frame.cameraIso_) * frame.pitch_ + offset;
    const dvec2 raw = frame.cameraIso_ * frame.pitch_;
    for (int axis = 0; axis < 2; ++axis) {
        EXPECT_LE(whole[axis], raw[axis] + 1e-4);
        EXPECT_GT(whole[axis], raw[axis] - 1.0 - 1e-4);
        const double wholePixels = whole[axis] - frame.phase_[axis];
        EXPECT_NEAR(wholePixels, std::round(wholePixels), 1e-4);
    }
}

// --- sprites -----------------------------------------------------------------

using SpritesToScreen = IRSystem::System<IRSystem::SPRITE_TO_SCREEN>;

// A sprite's anchor rides the world at the exact step, fraction included, and
// its quad never sees the zoom: the step is the anchor helper's only zoom
// input, and the size is not one of its inputs at all.
TEST(CameraZoomSpriteAnchor, AnchorFollowsTheFractionalStep) {
    const vec2 viewport(1280.0f, 720.0f);
    const vec2 camera(17.3f, -4.7f);
    const ivec2 scale(2, 2);
    const vec2 step = IRMath::calcTriangleStepSizeScreen(vec2(2.5f), scale);
    ASSERT_EQ(step, vec2(10.0f, 5.0f));

    const IRMath::vec3 world(3.0f, -2.0f, 1.0f);
    const vec2 anchor = SpritesToScreen::screenAnchor(world, viewport, camera, step);
    // One iso unit along each iso axis: world +y is iso (+1, -1), world +z is
    // iso (0, +2).
    const vec2 anchorPlusY =
        SpritesToScreen::screenAnchor(world + IRMath::vec3(0, 1, 0), viewport, camera, step);
    const vec2 anchorPlusZ =
        SpritesToScreen::screenAnchor(world + IRMath::vec3(0, 0, 1), viewport, camera, step);
    const float signY = IRPlatform::kGfx.screenYDirection_;
    EXPECT_NEAR(anchorPlusY.x - anchor.x, -step.x, 1e-3f);
    EXPECT_NEAR(anchorPlusY.y - anchor.y, -step.y * signY, 1e-3f);
    EXPECT_NEAR(anchorPlusZ.x - anchor.x, 0.0f, 1e-3f);
    EXPECT_NEAR(anchorPlusZ.y - anchor.y, 2.0f * step.y * signY, 1e-3f);
}

TEST(CameraZoomSpriteAnchor, GridSnapStaysWithinOneFramebufferPixel) {
    const vec2 viewport(1280.0f, 720.0f);
    const ivec2 scale(2, 2);
    const vec2 zoomValue(2.5f);
    const vec2 step = IRMath::calcTriangleStepSizeScreen(zoomValue, scale);
    const IRMath::vec3 world(3.0f, -2.0f, 1.0f);
    for (const vec2 camera : {vec2(17.3f, -4.7f), vec2(18.05f, -3.21f)}) {
        const IRMath::CameraRasterPhase frame = IRMath::advanceCameraRasterPhase(
            IRMath::CameraRasterPhase{
                dvec2(camera),
                IRMath::cameraZoomPitch(vec2(2.3f)),
                dvec2(0.0)
            },
            camera,
            zoomValue
        );
        const vec2 gridOrigin =
            viewport * 0.5f + vec2(IRMath::cameraRasterScreenResidual(frame, scale));
        const vec2 anchor = SpritesToScreen::screenAnchor(world, viewport, camera, step);
        const vec2 snapped = SpritesToScreen::snapToGameGrid(anchor, gridOrigin, vec2(scale));
        EXPECT_LE(IRMath::abs(snapped.x - anchor.x), static_cast<float>(scale.x));
        EXPECT_LE(IRMath::abs(snapped.y - anchor.y), static_cast<float>(scale.y));
    }
}

} // namespace
