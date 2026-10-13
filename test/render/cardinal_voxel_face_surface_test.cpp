#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <irreden/ir_math.hpp>

// Hermetic numeric harness for the cardinal single-canvas sun-shadow receive.
// COMPUTE_SUN_SHADOW recovers the exact surface point of the voxel face a pixel
// shows (trixelCanvasPixelToVoxelFaceSurface3D, ir_iso_common.{glsl,metal}) and
// tests it against the exact caster faces c_bake_voxel_sun_faces indexed. A cast
// edge then follows the caster's sub-voxel placement only if that point sits on
// the plane of the face the bake casts from, for every texel stage 1 writes.
//
// The model ports both sides through the IRMath CPU mirrors:
//
//   RASTER   c_voxel_to_trixel_stage_1: cell = roundHalfUp(position * s),
//            rotated into the view frame, micro-face corner from
//            faceMicroPositionFixed6, texels base + {0,1}x{0,1,2} keyed with the
//            corner's depth.
//   CAST     c_bake_voxel_sun_faces: face corner = cell / s - 0.5 in the world
//            frame, +1 on the face axis for a positive face.

namespace {

using IRMath::CardinalIndex;
using IRMath::ivec2;
using IRMath::ivec3;
using IRMath::vec3;

// Mirror of kCardinalFaceBlockRecoveryMid (ir_iso_common.glsl).
constexpr vec3 kRecoveryMid{-5.0f / 12.0f, 1.0f / 12.0f, 1.0f / 3.0f};
constexpr float kCellAnchor = 0.5f;

// Mirror of faceMicroPositionFixed6 (ir_iso_common.glsl).
ivec3 faceMicroPositionFixed6(int faceId, ivec3 cell, int u, int v, int subdivisions) {
    const int axis = faceId >> 1;
    const int offset = (faceId & 1) != 0 ? subdivisions : 0;
    if (axis == 0) {
        return ivec3(cell.x + offset, cell.y + u, cell.z + v);
    }
    if (axis == 1) {
        return ivec3(cell.x + u, cell.y + offset, cell.z + v);
    }
    return ivec3(cell.x + u, cell.y + v, cell.z + offset);
}

// Mirror of rotateFaceIdCardinalZ (ir_iso_common.glsl): the face a world face
// becomes in the view frame. Face ids are XNeg, XPos, YNeg, YPos, ZNeg, ZPos.
int rotateFaceIdCardinalZ(int faceId, CardinalIndex cardinalIndex) {
    if (cardinalIndex == CardinalIndex::k0 || faceId >= 4) {
        return faceId;
    }
    if (cardinalIndex == CardinalIndex::k180) {
        return faceId ^ 1;
    }
    constexpr int kView90[] = {3, 2, 0, 1};
    constexpr int kView270[] = {2, 3, 1, 0};
    return cardinalIndex == CardinalIndex::k90 ? kView90[faceId] : kView270[faceId];
}

// Mirror of trixelCanvasPixelToVoxelFaceSurface3D (ir_iso_common.glsl), with
// the frame offset already removed from the pixel.
vec3 voxelFaceSurface(
    ivec2 isoRel, int rawDepth, int faceAxis, int subdivisions, CardinalIndex cardinalIndex
) {
    const vec3 viewFixed =
        IRMath::isoPixelToPos3D(isoRel.x, isoRel.y, static_cast<float>(rawDepth));
    const float plane =
        static_cast<float>(IRMath::roundHalfUp(viewFixed[faceAxis] - kRecoveryMid[faceAxis]));
    const vec3 onPlane = viewFixed + vec3(plane - viewFixed[faceAxis]);
    const vec3 view = onPlane / static_cast<float>(subdivisions) - vec3(kCellAnchor);
    return IRMath::rotateCardinalZInv(view, cardinalIndex);
}

struct Placement {
    vec3 position_;
    int subdivisions_;
};

constexpr Placement kPlacements[] = {
    {vec3(0.0f, 0.0f, 0.0f), 1},
    {vec3(3.0f, -2.0f, -6.0f), 1},
    {vec3(-2.0f, -2.0f, -3.75f), 2},
    {vec3(-2.0f, -2.0f, -3.5f), 4},
    {vec3(7.125f, -1.375f, 2.625f), 8},
    {vec3(-11.3f, 4.6f, -0.2f), 4},
    {vec3(41.0f, -37.25f, 12.75f), 2},
};

constexpr CardinalIndex kCardinals[] = {
    CardinalIndex::k0,
    CardinalIndex::k90,
    CardinalIndex::k180,
    CardinalIndex::k270,
};

template <typename Visit> void forEachRasterTexel(Visit visit) {
    for (const Placement &placement : kPlacements) {
        const int s = placement.subdivisions_;
        const ivec3 cell = IRMath::roundVec3HalfUp(placement.position_ * static_cast<float>(s));
        for (const CardinalIndex cardinal : kCardinals) {
            const ivec3 viewCell = IRMath::rotateCardinalZ(cell, cardinal);
            for (int worldFace = 0; worldFace < 6; ++worldFace) {
                const int viewFace = rotateFaceIdCardinalZ(worldFace, cardinal);
                for (int u = 0; u < s; ++u) {
                    for (int v = 0; v < s; ++v) {
                        const ivec3 corner = faceMicroPositionFixed6(viewFace, viewCell, u, v, s);
                        const ivec2 base = IRMath::pos3DtoPos2DIso(corner);
                        const int depth = corner.x + corner.y + corner.z;
                        for (int dy = 0; dy < 3; ++dy) {
                            for (int dx = 0; dx < 2; ++dx) {
                                visit(
                                    placement,
                                    cell,
                                    cardinal,
                                    worldFace,
                                    viewFace,
                                    base + ivec2(dx, dy),
                                    depth
                                );
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST(CardinalVoxelFaceSurface, LandsOnTheFacePlaneTheSunBakeCasts) {
    int texels = 0;
    forEachRasterTexel([&](const Placement &placement,
                           ivec3 cell,
                           CardinalIndex cardinal,
                           int worldFace,
                           int viewFace,
                           ivec2 isoRel,
                           int depth) {
        const int worldAxis = worldFace >> 1;
        const vec3 surface =
            voxelFaceSurface(isoRel, depth, viewFace >> 1, placement.subdivisions_, cardinal);
        const float castPlane =
            static_cast<float>(cell[worldAxis]) / static_cast<float>(placement.subdivisions_) -
            kCellAnchor + ((worldFace & 1) != 0 ? 1.0f : 0.0f);
        ASSERT_NEAR(surface[worldAxis], castPlane, 1e-4f)
            << "s=" << placement.subdivisions_ << " cardinal=" << static_cast<int>(cardinal)
            << " face=" << worldFace << " iso=(" << isoRel.x << "," << isoRel.y << ")";
        ++texels;
    });
    EXPECT_GT(texels, 0);
}

TEST(CardinalVoxelFaceSurface, StaysOnThePixelsViewRay) {
    forEachRasterTexel([&](const Placement &placement,
                           ivec3,
                           CardinalIndex cardinal,
                           int,
                           int viewFace,
                           ivec2 isoRel,
                           int depth) {
        const int s = placement.subdivisions_;
        const vec3 surface = voxelFaceSurface(isoRel, depth, viewFace >> 1, s, cardinal);
        const vec3 rasterView = (IRMath::rotateCardinalZ(surface, cardinal) + vec3(kCellAnchor)) *
                                static_cast<float>(s);
        const IRMath::vec2 iso = IRMath::pos3DtoPos2DIso(rasterView);
        ASSERT_NEAR(iso.x, static_cast<float>(isoRel.x), 1e-3f);
        ASSERT_NEAR(iso.y, static_cast<float>(isoRel.y), 1e-3f);
    });
}

// Recovery moves with the caster by exactly its lattice step: the property that
// lets a cast edge follow sub-voxel placement instead of a sun-map texel.
TEST(CardinalVoxelFaceSurface, TracksSubVoxelTranslationExactly) {
    constexpr int kSubdivisions = 4;
    const vec3 start(-2.0f, -2.0f, -4.0f);
    for (int step = 0; step < 8; ++step) {
        const vec3 position = start + vec3(0.0f, 0.0f, 0.25f * static_cast<float>(step));
        const ivec3 cell = IRMath::roundVec3HalfUp(position * static_cast<float>(kSubdivisions));
        const ivec3 corner = faceMicroPositionFixed6(1, cell, 0, 0, kSubdivisions);
        const vec3 surface = voxelFaceSurface(
            IRMath::pos3DtoPos2DIso(corner),
            corner.x + corner.y + corner.z,
            0,
            kSubdivisions,
            CardinalIndex::k0
        );
        EXPECT_NEAR(surface.z, position.z - kCellAnchor, 1e-4f) << "step " << step;
        EXPECT_NEAR(surface.x, position.x + kCellAnchor, 1e-4f) << "step " << step;
    }
}

std::string readShaderSource(const std::string &relativePath) {
    const std::filesystem::path path =
        std::filesystem::path{IR_TEST_RENDER_SHADER_DIR} / relativePath;
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << "cannot open " << path.string();
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

TEST(CardinalVoxelFaceSurface, BothBackendsShareTheRecoveryMidpoint) {
    const std::string glsl = readShaderSource("ir_iso_common.glsl");
    EXPECT_NE(glsl.find("vec3(-5.0 / 12.0, 1.0 / 12.0, 1.0 / 3.0)"), std::string::npos);
    const std::string metal = readShaderSource("metal/ir_iso_common.metal");
    EXPECT_NE(metal.find("float3(-5.0f / 12.0f, 1.0f / 12.0f, 1.0f / 3.0f)"), std::string::npos);
    for (const char *receiver :
         {"c_compute_sun_shadow_body.glsl", "metal/c_compute_sun_shadow_body.metal"}) {
        EXPECT_NE(
            readShaderSource(receiver).find("trixelCanvasPixelToVoxelFaceSurface3D("),
            std::string::npos
        ) << receiver;
    }
}

} // namespace
