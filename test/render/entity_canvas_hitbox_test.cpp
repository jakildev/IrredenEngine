#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_platform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/input/components/component_hitbox_2d.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/systems/system_entity_canvas_to_framebuffer.hpp>

namespace {

using IRComponents::C_EntityCanvas;
using IRComponents::C_HitBox2D;
using IRComponents::C_WorldTransform;
using IRSystem::ENTITY_CANVAS_TO_FRAMEBUFFER;

TEST(EntityCanvasHitbox, PlacementConvertsFramebufferYToMouseY) {
    C_HitBox2D hitbox;

    IRSystem::System<ENTITY_CANVAS_TO_FRAMEBUFFER>::publishHitboxPlacement(
        hitbox,
        723.0f,
        IRMath::vec2{642.0f, 377.0f},
        IRMath::vec2{256.0f, 128.0f},
        0,
        42
    );

    EXPECT_EQ(hitbox.centerScreen_, IRMath::vec2(642.0f, 346.0f));
    EXPECT_EQ(hitbox.halfExtent_, IRMath::vec2(128.0f, 64.0f));
    EXPECT_TRUE(hitbox.screenSpacePlaced_);
}

TEST(EntityCanvasHitbox, RemovingCanvasClearsScreenPlacement) {
    IREntity::EntityManager entityManager;
    const IREntity::EntityId entity =
        IREntity::createEntity(C_HitBox2D{}, C_EntityCanvas{}, C_WorldTransform{});
    IREntity::getComponent<C_HitBox2D>(entity).screenSpaceCenter_ = true;
    IREntity::getComponent<C_HitBox2D>(entity).screenSpacePlaced_ = true;

    IREntity::removeComponent<C_EntityCanvas>(entity);
    IRSystem::System<ENTITY_CANVAS_TO_FRAMEBUFFER> system;
    system.collectHitboxes();

    const C_HitBox2D &hitbox = IREntity::getComponent<C_HitBox2D>(entity);
    EXPECT_FALSE(hitbox.screenSpaceCenter_);
    EXPECT_FALSE(hitbox.screenSpacePlaced_);
}

using EntityCanvasToFramebuffer = IRSystem::System<ENTITY_CANVAS_TO_FRAMEBUFFER>;

// Framebuffer and main canvas extents in the 2:1 ratio the render manager
// builds them in, so one iso unit is `zoom * (2, 1)` framebuffer pixels.
constexpr IRMath::vec2 kFramebufferResolution{656.0f, 376.0f};
constexpr IRMath::vec2 kMainCanvasSize{328.0f, 376.0f};

TEST(EntityCanvasPlacement, SnappedCameraTermIsTheWholePixelsOfTheSubCellOffset) {
    const IRMath::vec2 offset = EntityCanvasToFramebuffer::snappedCameraFramebufferOffset(
        IRMath::vec2(17.3f, -4.7f),
        kFramebufferResolution,
        IRMath::vec2(4.0f),
        kMainCanvasSize
    );
    // fract = (0.3, 0.3) at 8x4 px per iso unit -> (2.4, 1.2) -> floor, Y-up.
    EXPECT_EQ(offset, IRMath::vec2(2.0f, -1.0f));
}

// The framebuffer is twice the main canvas across and equal to it down, so the
// extents' ratio times a power-of-two zoom is the exact pitch at any even
// width: a camera a whole number of pixels into its cell lands on that pixel.
// 324 x 182 is an extent at which a pitch rebuilt through the rotated-view
// store size lands one ulp short instead.
TEST(EntityCanvasPlacement, SnappedCameraTermIsExactOnThePixelLattice) {
    for (const IRMath::vec2 framebuffer : {
             IRMath::vec2(324.0f, 182.0f),
             IRMath::vec2(644.0f, 362.0f),
             IRMath::vec2(1284.0f, 722.0f),
         }) {
        const IRMath::vec2 mainCanvas = framebuffer / IRMath::vec2(2.0f, 1.0f);
        for (const float zoom : {1.0f, 2.0f, 4.0f, 8.0f}) {
            const IRMath::vec2 pitch = IRMath::vec2(IRMath::cameraZoomPitch(IRMath::vec2(zoom)));
            for (int kx = 0; kx < static_cast<int>(pitch.x); ++kx) {
                for (int ky = 0; ky < static_cast<int>(pitch.y); ++ky) {
                    const IRMath::vec2 camera = IRMath::vec2(17.0f) + IRMath::vec2(kx, ky) / pitch;
                    EXPECT_EQ(
                        EntityCanvasToFramebuffer::snappedCameraFramebufferOffset(
                            camera,
                            framebuffer,
                            IRMath::vec2(zoom),
                            mainCanvas
                        ),
                        IRMath::vec2(kx, -ky)
                    ) << "framebuffer="
                      << framebuffer.x << " zoom=" << zoom << " k=(" << kx << "," << ky << ")";
                }
            }
        }
    }
}

// Continuous zoom, two camera offsets: the detached centre's camera term plus
// the whole cells the placement already added lands within one framebuffer
// pixel of the raw camera offset, and exactly where the world canvas lands.
TEST(EntityCanvasPlacement, ContinuousCameraTermTracksTheRawCameraWithinOnePixel) {
    const IRMath::vec2 zoom(2.5f);
    const int density = 3;
    for (const IRMath::vec2 camera : {IRMath::vec2(17.3f, 4.6f), IRMath::vec2(18.05f, -3.21f)}) {
        const IRMath::CameraRasterPhase frame = IRMath::advanceCameraRasterPhase(
            IRMath::CameraRasterPhase{
                IRMath::dvec2(camera),
                IRMath::cameraZoomPitch(IRMath::vec2(2.3f)),
                IRMath::dvec2(0.0)
            },
            camera,
            zoom
        );
        const IRMath::dvec2 detached =
            IRMath::floor(frame.cameraIso_) * frame.pitch_ +
            IRMath::cameraRasterDetachedOffset(frame) * IRMath::dvec2(1.0, -1.0);
        const IRMath::dvec2 raw = frame.cameraIso_ * frame.pitch_;
        const IRMath::vec2 rasterCamera = camera * static_cast<float>(density);
        const IRMath::dvec2 world = IRMath::dvec2(IRMath::floor(rasterCamera)) * frame.pitch_ /
                                        static_cast<double>(density) +
                                    IRMath::cameraRasterGatherTranslation(
                                        frame,
                                        rasterCamera,
                                        IRMath::vec2(0.0f),
                                        density
                                    ) * IRMath::dvec2(IRPlatform::kIsoToScreenSign);
        for (int axis = 0; axis < 2; ++axis) {
            EXPECT_LE(detached[axis], raw[axis] + 1e-9);
            EXPECT_GT(detached[axis], raw[axis] - 1.0 - 1e-9);
            EXPECT_NEAR(detached[axis], world[axis], 1e-9);
        }
    }
}

} // namespace
