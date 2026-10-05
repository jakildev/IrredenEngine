#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>

// ---------------------------------------------------------------------------
// The per-axis store frame.
//
// While the camera sits at a residual yaw, every visible voxel face is filed in
// a per-axis store keyed in the nearest-cardinal view frame:
//
//   cell = trixelOriginOffsetZ1(storeSize) + perAxisStoreAnchor(...)
//        + pos3DtoPos2DIso(rotateCardinalZ(facePos, storeCardinal))
//
// A face whose cell falls outside the store is dropped, so the frame carries
// one obligation: every face on screen lands inside. These tests are that
// obligation as pure math over the IRMath helpers the store, its consumers and
// the framebuffer scatter share — no GPU.
// ---------------------------------------------------------------------------

namespace {

using IRMath::CardinalIndex;
using IRMath::ivec2;
using IRMath::ivec3;
using IRMath::vec2;
using IRMath::vec3;

// Main-canvas extents in trixels: 16:9, portrait 9:16, square, and the two
// small fixture canvases of IRZYawCoverage.
const ivec2 kCanvases[] = {
    ivec2(642, 722), ivec2(272, 962), ivec2(362, 722), ivec2(137, 482), ivec2(242, 272)
};

// Effective camera offsets: the origin, a moderate pan, and views thousands of
// cells from the world origin — the regime a camera-anchored window loses.
const vec2 kCameras[] = {
    vec2(0.0f), vec2(700.25f, -300.5f), vec2(-2500.3f, 1800.7f), vec2(20000.0f, -15000.0f)
};

const float kReferenceHeights[] = {0.0f, 36.0f, -80.0f};

CardinalIndex storeCardinal(float visualYaw) {
    return IRMath::rasterYawCardinalIndex(
        IRMath::round(visualYaw / IRMath::kHalfPi) * IRMath::kHalfPi
    );
}

ivec2 storeCell(vec3 world, ivec2 storeSize, ivec2 anchor, CardinalIndex cardinal) {
    const vec2 iso = IRMath::pos3DtoPos2DIso(IRMath::rotateCardinalZ(world, cardinal));
    return IRMath::trixelOriginOffsetZ1(storeSize) + anchor + ivec2(IRMath::floor(iso));
}

// How far outside the store the worst on-screen sample lands, in cells; <= 0
// means every sample is inside. Samples a 9x9 grid over the zoom-1 screen
// rectangle at each of @p heightOffsets from the reference plane.
template <typename AnchorFn, typename KeyFrameFn>
int worstOvershoot(
    ivec2 canvas,
    ivec2 storeSize,
    vec2 effectiveCameraIso,
    float visualYaw,
    float referenceHeight,
    std::initializer_list<float> heightOffsets,
    AnchorFn anchorOf,
    KeyFrameFn cardinalOf
) {
    const CardinalIndex cardinal = cardinalOf(visualYaw);
    const ivec2 anchor = anchorOf(effectiveCameraIso, visualYaw, cardinal, referenceHeight);
    int worst = -(1 << 20);
    for (int i = 0; i <= 8; ++i) {
        for (int j = 0; j <= 8; ++j) {
            const vec2 screenIso =
                vec2(canvas) * (vec2(static_cast<float>(i), static_cast<float>(j)) / 8.0f - 0.5f);
            for (const float heightOffset : heightOffsets) {
                const vec3 world = IRMath::pos2DIsoToPos3DAtZLevelYawed(
                    screenIso - effectiveCameraIso,
                    referenceHeight + heightOffset,
                    visualYaw
                );
                const ivec2 cell = storeCell(world, storeSize, anchor, cardinal);
                worst = IRMath::max(worst, IRMath::max(-cell.x, cell.x - (storeSize.x - 1)));
                worst = IRMath::max(worst, IRMath::max(-cell.y, cell.y - (storeSize.y - 1)));
            }
        }
    }
    return worst;
}

// ---------------------------------------------------------------------------
// The obligation: every on-screen point within the height headroom of the
// reference plane is inside the store, at every yaw, pan and canvas aspect.
// ---------------------------------------------------------------------------

TEST(PerAxisStoreFrame, WindowHoldsEveryOnScreenPointAtEveryYawPanAndAspect) {
    const float headroom = IRMath::kPerAxisStoreHeightHeadroom;
    for (const ivec2 canvas : kCanvases) {
        for (const vec2 camera : kCameras) {
            for (const float referenceHeight : kReferenceHeights) {
                for (int degrees = 1; degrees < 360; degrees += 4) {
                    const float visualYaw = IRMath::kPi * static_cast<float>(degrees) / 180.0f;
                    const int overshoot = worstOvershoot(
                        canvas,
                        IRMath::perAxisTrixelCanvasWorstCaseSize(canvas),
                        camera,
                        visualYaw,
                        referenceHeight,
                        {-headroom, 0.0f, headroom},
                        IRMath::perAxisStoreAnchor,
                        storeCardinal
                    );
                    EXPECT_LE(overshoot, 0)
                        << "canvas=(" << canvas.x << "," << canvas.y << ") camera=(" << camera.x
                        << "," << camera.y << ") height=" << referenceHeight << " yaw=" << degrees;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Negative control — what the anchor and the cardinal key frame are for. A
// window anchored on the camera and keyed in the yaw-0 frame is centered on
// what the UNROTATED view at that camera offset would show. Far from the world
// origin that is somewhere else entirely; and on a tall canvas near a quarter
// turn it is the right place but the wrong shape for a store sized by the
// face-deformation bounds alone, `(2W, W + H)`.
// ---------------------------------------------------------------------------

TEST(PerAxisStoreFrame, CameraAnchoredYawZeroWindowLosesTheView) {
    const auto cameraAnchor = [](vec2 effectiveCameraIso, float, CardinalIndex, float) {
        return ivec2(IRMath::floor(effectiveCameraIso));
    };
    const auto yawZeroFrame = [](float) { return CardinalIndex::k0; };
    const float visualYaw = IRMath::kPi * 78.0f / 180.0f;

    const ivec2 landscape(642, 722);
    EXPECT_GT(
        worstOvershoot(
            landscape,
            IRMath::perAxisTrixelCanvasWorstCaseSize(landscape),
            vec2(-2500.3f, 1800.7f),
            visualYaw,
            0.0f,
            {0.0f},
            cameraAnchor,
            yawZeroFrame
        ),
        1000
    );
    const ivec2 portrait(272, 962);
    EXPECT_GT(
        worstOvershoot(
            portrait,
            ivec2(2 * portrait.x, portrait.x + portrait.y),
            vec2(0.0f),
            visualYaw,
            0.0f,
            {0.0f},
            cameraAnchor,
            yawZeroFrame
        ),
        100
    );
}

// ---------------------------------------------------------------------------
// The anchor moves in whole quanta off the camera anchor, and not at all while
// the cardinal view and the live view agree on what is at the center — so a
// camera near the world origin keeps the camera-anchored cells exactly.
// ---------------------------------------------------------------------------

TEST(PerAxisStoreFrame, AnchorIsTheCameraAnchorPlusWholeQuanta) {
    for (const vec2 camera : kCameras) {
        for (int degrees = 1; degrees < 360; degrees += 11) {
            const float visualYaw = IRMath::kPi * static_cast<float>(degrees) / 180.0f;
            const ivec2 anchor =
                IRMath::perAxisStoreAnchor(camera, visualYaw, storeCardinal(visualYaw), 0.0f);
            const ivec2 shift = anchor - ivec2(IRMath::floor(camera));
            EXPECT_EQ(shift.x % IRMath::kPerAxisStoreAnchorQuantum, 0) << "yaw=" << degrees;
            EXPECT_EQ(shift.y % IRMath::kPerAxisStoreAnchorQuantum, 0) << "yaw=" << degrees;
        }
    }
}

TEST(PerAxisStoreFrame, AnchorIsTheCameraAnchorNearTheWorldOrigin) {
    const vec2 cameras[] = {vec2(0.0f), vec2(16.0f, 16.0f), vec2(-12.75f, 9.5f)};
    for (const vec2 camera : cameras) {
        for (int degrees = -44; degrees <= 44; degrees += 4) {
            const float visualYaw = IRMath::kPi * static_cast<float>(degrees) / 180.0f;
            EXPECT_EQ(
                IRMath::perAxisStoreAnchor(camera, visualYaw, CardinalIndex::k0, 0.0f),
                ivec2(IRMath::floor(camera))
            ) << "camera=("
              << camera.x << "," << camera.y << ") yaw=" << degrees;
        }
    }
}

TEST(PerAxisStoreFrame, AnchorIsTheCameraAnchorAtEveryCardinalYaw) {
    // At a cardinal yaw the store frame IS the view, so there is no drift to
    // remove however far the camera has panned.
    for (const vec2 camera : kCameras) {
        for (int quarter = 0; quarter < 4; ++quarter) {
            const float visualYaw = IRMath::kHalfPi * static_cast<float>(quarter);
            EXPECT_EQ(
                IRMath::perAxisStoreAnchor(camera, visualYaw, storeCardinal(visualYaw), 36.0f),
                ivec2(IRMath::floor(camera))
            ) << "camera=("
              << camera.x << "," << camera.y << ") quarter=" << quarter;
        }
    }
}

// ---------------------------------------------------------------------------
// The key is a bijection on the lattice in every store frame: a cell and its
// store-frame depth invert to exactly the world face position that was filed.
// ---------------------------------------------------------------------------

TEST(PerAxisStoreFrame, StoreCellAndDepthInvertToTheWorldLatticeAtEveryCardinal) {
    const ivec2 storeSize(1284, 1364);
    const ivec2 anchor(-37, 1216);
    const ivec2 origin = IRMath::trixelOriginOffsetZ1(storeSize) + anchor;
    for (const CardinalIndex cardinal :
         {CardinalIndex::k0, CardinalIndex::k90, CardinalIndex::k180, CardinalIndex::k270}) {
        for (const ivec3 facePos :
             {ivec3(0, 0, 0), ivec3(4, -3, 7), ivec3(-311, 209, -2), ivec3(1500, -900, 36)}) {
            const ivec3 framePos = IRMath::rotateCardinalZ(facePos, cardinal);
            const ivec2 cell = origin + IRMath::pos3DtoPos2DIso(framePos);
            const ivec2 isoPix = cell - origin;
            const vec3 recovered = IRMath::rotateCardinalZInv(
                IRMath::isoPixelToPos3D(
                    isoPix.x,
                    isoPix.y,
                    static_cast<float>(IRMath::pos3DtoDistance(framePos))
                ),
                cardinal
            );
            EXPECT_EQ(IRMath::roundVec3HalfUp(recovered), facePos)
                << "cardinal=" << static_cast<int>(cardinal);
        }
    }
}

} // namespace
