#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <irreden/ir_math.hpp>

// Hermetic numeric harness for the per-axis sun-shadow CAST/RECEIVE seam
// (#2816). No GL/Metal context: both recoveries are ported to the host through
// the IRMath CPU mirrors the shaders' own helpers document themselves against,
// so the disagreement the issue describes is measurable on any host.
//
// The seam has two sides, and before #2816 they recovered the same surface at
// two different positions:
//
//   RECEIVE  c_compute_sun_shadow.{glsl,metal} -> perAxisCellToWorld3DSubCell
//            (ir_per_axis_lighting.{glsl,metal}) = lattice origin + the
//            encoding's three 4-bit sub-cell fracs.
//   CAST     c_resolve_per_axis_screen_depth.{glsl,metal} re-projects the store
//            cell into the main-canvas cardinal layout; BAKE_SUN_SHADOW_MAP
//            then recovers a world position from that deposit with
//            trixelCanvasPixelToWorld3D. Pre-#2816 the resolve recovered
//            LATTICE-ONLY, so the caster landed up to half a world cell off the
//            surface the receiver samples.
//
// The harness models the round trip CAST -> BAKE, not just the resolve, because
// the deposit is only meaningful through the recovery that reads it.
//
// Why the model can be this small and still be faithful: the resolve writes at
// `mainBase + pos3DtoPos2DIso(microView)` with key `pos3DtoDistance(microView)`,
// and BAKE reads `isoPixelToPos3D(pixel - mainBase, key)`. `mainBase` is the
// same `trixelFrameOffset(trixelOriginOffsetZ1(mainCanvasSize), ...)` on both
// sides, so it cancels, and `isoPixelToPos3D(pos3DtoPos2DIso(v),
// pos3DtoDistance(v)) == v` is the exact iso identity both sides are built on.
// What survives is BAKE's `/= scale` and `rotateCardinalZInv` — modelled below.

namespace {

using IRMath::CardinalIndex;
using IRMath::ivec2;
using IRMath::ivec3;
using IRMath::vec2;
using IRMath::vec3;

// ---------------------------------------------------------------------------
// Shader mirrors that IRMath does not already expose.
// ---------------------------------------------------------------------------

// Mirror of `faceInPlaneUnitAxes` (ir_iso_common.glsl): the two in-plane unit
// model axes a face's quad spans, by axis = faceId >> 1.
void faceInPlaneUnitAxes(int axis, vec3 &eu, vec3 &ev) {
    eu = (axis == 0) ? vec3(0.0f, 1.0f, 0.0f) : vec3(1.0f, 0.0f, 0.0f);
    ev = (axis == 2) ? vec3(0.0f, 1.0f, 0.0f) : vec3(0.0f, 0.0f, 1.0f);
}

// Mirror of `faceOutOfPlaneUnitAxis` (ir_iso_common.glsl): the direction the
// wFrac offset moves the reconstructed plane.
vec3 faceOutOfPlaneUnitAxis(int axis) {
    return vec3(
        axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f
    );
}

// Mirror of `effectiveTrixelSubdivisionScale` (ir_iso_common.glsl).
int effectiveTrixelSubdivisionScale(ivec2 voxelRenderOptions) {
    return voxelRenderOptions.x != 0 ? IRMath::max(voxelRenderOptions.y, 1) : 1;
}

// Mirror of the #1458 per-axis encode (`encodeDepthWithFaceFrac`,
// ir_iso_common.glsl): rawDepth[31:15] | wFrac4[14:11] | flip[10] |
// uFrac4[9:6] | vFrac4[5:2] | slot[1:0].
int encodeDepthWithFaceFrac(
    int rawDepth, int slot, int uFrac4, int vFrac4, int wFrac4, int flip
) {
    return (rawDepth << 15) | (wFrac4 << 11) | (flip << 10) | (uFrac4 << 6) |
           (vFrac4 << 2) | slot;
}

// Mirror of `perAxisSubCellFrac` (ir_per_axis_lighting.{glsl,metal}) — the one
// definition of the frac layout's centring convention, which both recoveries
// below read.
vec3 perAxisSubCellFrac(int uFrac4, int vFrac4, int wFrac4) {
    return vec3(
        static_cast<float>(uFrac4) / 16.0f - 0.5f,
        static_cast<float>(vFrac4) / 16.0f - 0.5f,
        static_cast<float>(wFrac4) / 16.0f - 0.5f
    );
}

// ---------------------------------------------------------------------------
// The scene a case describes.
// ---------------------------------------------------------------------------

struct Case {
    ivec3 facePos{4, -3, 7};   // integer world face position the store filed
    int axis = 2;              // faceId >> 1
    int uFrac4 = 8;            // 8/8/8 == integer-positioned content
    int vFrac4 = 8;
    int wFrac4 = 8;
    CardinalIndex cardinal = CardinalIndex::k0;
    int subdivisions = 1;      // effSub; 1 == the integer world-cell layout
    ivec2 perAxisSize{512, 512};
    vec2 frameCanvasOffset{0.0f, 0.0f};
};

ivec2 voxelRenderOptions(const Case &c) {
    return ivec2(c.subdivisions > 1 ? 1 : 0, c.subdivisions);
}

// The store cell this face was filed at: `perAxisBase + pos3DtoPos2DIso(facePos)`
// (c_voxel_to_trixel_stage_1_body.glsl, via perAxisStoreFacePos).
ivec2 storeCell(const Case &c) {
    const ivec2 perAxisBase =
        IRMath::trixelOriginOffsetZ1(c.perAxisSize) +
        ivec2(
            static_cast<int>(IRMath::floor(c.frameCanvasOffset.x)),
            static_cast<int>(IRMath::floor(c.frameCanvasOffset.y))
        );
    return perAxisBase + IRMath::pos3DtoPos2DIso(c.facePos);
}

int storeEncoded(const Case &c) {
    return encodeDepthWithFaceFrac(
        IRMath::pos3DtoDistance(c.facePos), /*slot=*/c.axis, c.uFrac4, c.vFrac4,
        c.wFrac4, /*flip=*/0
    );
}

// The lattice origin both recoveries start from — the exact iso inverse
// (`perAxisCellToWorld3D`, ir_per_axis_lighting.glsl:32-34, byte-for-byte the
// block c_resolve_per_axis_screen_depth.glsl inlines).
vec3 latticeOrigin(const Case &c) {
    const ivec2 perAxisBase =
        IRMath::trixelOriginOffsetZ1(c.perAxisSize) +
        ivec2(
            static_cast<int>(IRMath::floor(c.frameCanvasOffset.x)),
            static_cast<int>(IRMath::floor(c.frameCanvasOffset.y))
        );
    const ivec2 isoPix = storeCell(c) - perAxisBase;
    return IRMath::isoPixelToPos3D(
        isoPix.x, isoPix.y, static_cast<float>(IRMath::pos3DtoDistance(c.facePos))
    );
}

// ---------------------------------------------------------------------------
// RECEIVE — perAxisCellToWorld3DSubCell.
// ---------------------------------------------------------------------------

vec3 receiveWorld(const Case &c) {
    vec3 eu;
    vec3 ev;
    faceInPlaneUnitAxes(c.axis, eu, ev);
    const vec3 frac = perAxisSubCellFrac(c.uFrac4, c.vFrac4, c.wFrac4);
    return latticeOrigin(c) + eu * frac.x + ev * frac.y +
           faceOutOfPlaneUnitAxis(c.axis) * frac.z;
}

// ---------------------------------------------------------------------------
// CAST — the resolve emit, read back through BAKE's recovery.
// ---------------------------------------------------------------------------

// Pre-#2816: the resolve rounded to the lattice, rotated, scaled, and emitted.
// BAKE's `/scale` + `rotateCardinalZInv` invert the last two exactly, so the
// caster lands on the integer lattice cell regardless of effSub.
vec3 castWorldLatticeOnly(const Case &c) {
    const int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions(c));
    const ivec3 origin = IRMath::roundVec3HalfUp(latticeOrigin(c));
    const ivec3 viewPos = IRMath::rotateCardinalZ(origin, c.cardinal) * scale;
    return IRMath::rotateCardinalZInv(vec3(viewPos) / static_cast<float>(scale), c.cardinal);
}

// Post-#2816: the frac is quantized to subdivision units in the FACE-LOCAL
// frame, then composed against the already-rotated unit basis. Everything after
// the quantize is integer.
vec3 castWorldSubCell(const Case &c) {
    const int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions(c));
    const ivec3 origin = IRMath::roundVec3HalfUp(latticeOrigin(c));
    const ivec3 viewPos = IRMath::rotateCardinalZ(origin, c.cardinal) * scale;

    vec3 eu;
    vec3 ev;
    faceInPlaneUnitAxes(c.axis, eu, ev);
    const ivec3 stepU = IRMath::rotateCardinalZ(ivec3(eu), c.cardinal);
    const ivec3 stepV = IRMath::rotateCardinalZ(ivec3(ev), c.cardinal);
    const ivec3 stepW =
        IRMath::rotateCardinalZ(ivec3(faceOutOfPlaneUnitAxis(c.axis)), c.cardinal);

    const ivec3 subCellSteps = IRMath::roundVec3HalfUp(
        perAxisSubCellFrac(c.uFrac4, c.vFrac4, c.wFrac4) * static_cast<float>(scale)
    );
    const ivec3 microView = viewPos + stepU * subCellSteps.x +
                            stepV * subCellSteps.y + stepW * subCellSteps.z;
    return IRMath::rotateCardinalZInv(
        vec3(microView) / static_cast<float>(scale), c.cardinal
    );
}

float maxAbsDelta(vec3 a, vec3 b) {
    return IRMath::max(
        IRMath::abs(a.x - b.x),
        IRMath::max(IRMath::abs(a.y - b.y), IRMath::abs(a.z - b.z))
    );
}

// The layout's own quantum: the resolve deposits into an INTEGER cardinal
// layout at effSub resolution, so no formulation can beat half a subdivision
// unit.
float layoutQuantum(const Case &c) {
    return 0.5f / static_cast<float>(effectiveTrixelSubdivisionScale(voxelRenderOptions(c)));
}

// Every (axis, cardinal) combination — the defect is a per-axis/per-yaw seam, so
// a single pose would not establish it.
std::vector<Case> poses(const Case &base) {
    const CardinalIndex kCardinals[] = {
        CardinalIndex::k0, CardinalIndex::k90, CardinalIndex::k180, CardinalIndex::k270
    };
    std::vector<Case> out;
    for (int axis = 0; axis < 3; ++axis) {
        for (const CardinalIndex cardinal : kCardinals) {
            Case c = base;
            c.axis = axis;
            c.cardinal = cardinal;
            out.push_back(c);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Arm 1 — NEGATIVE control. Integer-positioned content encodes frac 8/8/8, so
// the sub-cell offset is exactly zero and BOTH formulations must agree with
// RECEIVE exactly. Pre-#2816 code passes this arm; that is the point — an arm
// only the fix can pass proves nothing on its own.
// ---------------------------------------------------------------------------

TEST(PerAxisCastSubCell, IntegerContentAgreesExactlyBeforeAndAfter) {
    for (const int subdivisions : {1, 2, 4, 8}) {
        Case base;
        base.subdivisions = subdivisions;
        base.uFrac4 = 8;
        base.vFrac4 = 8;
        base.wFrac4 = 8;
        for (const Case &c : poses(base)) {
            const vec3 receive = receiveWorld(c);
            EXPECT_FLOAT_EQ(maxAbsDelta(castWorldLatticeOnly(c), receive), 0.0f)
                << "effSub=" << subdivisions << " axis=" << c.axis
                << " cardinal=" << static_cast<int>(c.cardinal);
            EXPECT_FLOAT_EQ(maxAbsDelta(castWorldSubCell(c), receive), 0.0f)
                << "effSub=" << subdivisions << " axis=" << c.axis
                << " cardinal=" << static_cast<int>(c.cardinal);
        }
    }
}

// ---------------------------------------------------------------------------
// Arm 2 — POSITIVE arm. Fractional content at effSub >= 2: the pre-fix delta is
// non-zero and the post-fix delta is bounded by the layout quantum. The two arms
// move in opposite directions, which is what makes the harness discriminating.
// ---------------------------------------------------------------------------

TEST(PerAxisCastSubCell, FractionalContentDeltaCollapsesToLayoutQuantum) {
    // 13/3/9 is deliberately NOT a frac the subdivision lattice can represent
    // exactly at these scales — an exactly-representable frac would post-fix to
    // zero delta and flatter the fix.
    for (const int subdivisions : {2, 4, 8}) {
        Case base;
        base.subdivisions = subdivisions;
        base.uFrac4 = 13; // +0.3125
        base.vFrac4 = 3;  // -0.3125
        base.wFrac4 = 9;  // +0.0625
        for (const Case &c : poses(base)) {
            const vec3 receive = receiveWorld(c);
            const float before = maxAbsDelta(castWorldLatticeOnly(c), receive);
            const float after = maxAbsDelta(castWorldSubCell(c), receive);

            // Pre-fix: the caster sits on the lattice, so the delta is the full
            // sub-cell offset — independent of effSub.
            EXPECT_FLOAT_EQ(before, 0.3125f)
                << "effSub=" << subdivisions << " axis=" << c.axis
                << " cardinal=" << static_cast<int>(c.cardinal);
            EXPECT_LE(after, layoutQuantum(c))
                << "effSub=" << subdivisions << " axis=" << c.axis
                << " cardinal=" << static_cast<int>(c.cardinal);
            EXPECT_LT(after, before)
                << "effSub=" << subdivisions << " axis=" << c.axis
                << " cardinal=" << static_cast<int>(c.cardinal);
        }
    }
}

// A frac the subdivision lattice CAN represent exactly closes the seam
// completely — the strongest form of the positive arm.
TEST(PerAxisCastSubCell, RepresentableFracClosesTheSeamExactly) {
    Case base;
    base.subdivisions = 4;
    base.uFrac4 = 12; // +0.25 == 1 subdivision unit at effSub 4
    base.vFrac4 = 4;  // -0.25
    base.wFrac4 = 8;  //  0.0
    for (const Case &c : poses(base)) {
        const vec3 receive = receiveWorld(c);
        EXPECT_FLOAT_EQ(maxAbsDelta(castWorldLatticeOnly(c), receive), 0.25f)
            << "axis=" << c.axis << " cardinal=" << static_cast<int>(c.cardinal);
        EXPECT_FLOAT_EQ(maxAbsDelta(castWorldSubCell(c), receive), 0.0f)
            << "axis=" << c.axis << " cardinal=" << static_cast<int>(c.cardinal);
    }
}

// The displacement the fix adds is a property of the content, not of the yaw
// quadrant: quantizing in the face-local frame means the same sub-cell offset
// deposits to the same cell relative to its lattice origin at every cardinal.
// Rounding after the rotation loses this, because rotateCardinalZ negates axes
// and roundHalfUp is not symmetric about zero.
TEST(PerAxisCastSubCell, DisplacementIsCardinalIndependent) {
    for (const int subdivisions : {1, 2, 4, 8}) {
        for (const int uFrac4 : {0, 3, 8, 13, 15}) {
            Case base;
            base.subdivisions = subdivisions;
            base.uFrac4 = uFrac4;
            base.vFrac4 = 15 - uFrac4;
            base.wFrac4 = (uFrac4 + 5) % 16;
            for (int axis = 0; axis < 3; ++axis) {
                Case reference = base;
                reference.axis = axis;
                reference.cardinal = CardinalIndex::k0;
                const vec3 referenceShift =
                    castWorldSubCell(reference) - castWorldLatticeOnly(reference);
                for (const CardinalIndex cardinal :
                     {CardinalIndex::k90, CardinalIndex::k180, CardinalIndex::k270}) {
                    Case c = base;
                    c.axis = axis;
                    c.cardinal = cardinal;
                    const vec3 shift = castWorldSubCell(c) - castWorldLatticeOnly(c);
                    EXPECT_FLOAT_EQ(maxAbsDelta(shift, referenceShift), 0.0f)
                        << "effSub=" << subdivisions << " axis=" << axis
                        << " frac=" << base.uFrac4 << "/" << base.vFrac4 << "/"
                        << base.wFrac4 << " cardinal=" << static_cast<int>(cardinal);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Arm 3 — the documented graceful-degradation boundary. At effSub == 1 the
// destination has no representation finer than the world cell, so the fix is a
// provable no-op there: roundHalfUp maps the whole [-0.5, +7/16] frac range to
// 0 subdivision units. Pinning it keeps the boundary a stated design call
// rather than a silent one, and pins the byte-identity the emit relies on.
//
// This arm is also what forced the quantize into the FACE-LOCAL frame. The
// first formulation composed the frac against the rotated basis and rounded
// afterwards; that fails here at frac4 == 0 on any axis a cardinal negates
// (-0.5 becomes +0.5, which roundHalfUp carries to +1), silently moving the
// caster one cell at effSub 1 — the common case — and staling every reference
// image for no accuracy gain. Exhaustive over all 4096 frac triples × 3 axes ×
// 4 cardinals, because that failure lives at exactly one endpoint of one field.
// ---------------------------------------------------------------------------

TEST(PerAxisCastSubCell, EffSubOneIsAProvableNoOpAcrossEveryFrac) {
    for (int uFrac4 = 0; uFrac4 < 16; ++uFrac4) {
        for (int vFrac4 = 0; vFrac4 < 16; ++vFrac4) {
            for (int wFrac4 = 0; wFrac4 < 16; ++wFrac4) {
                Case base;
                base.subdivisions = 1;
                base.uFrac4 = uFrac4;
                base.vFrac4 = vFrac4;
                base.wFrac4 = wFrac4;
                for (const Case &c : poses(base)) {
                    ASSERT_FLOAT_EQ(
                        maxAbsDelta(castWorldSubCell(c), castWorldLatticeOnly(c)), 0.0f
                    ) << "frac=" << uFrac4 << "/" << vFrac4 << "/" << wFrac4
                      << " axis=" << c.axis
                      << " cardinal=" << static_cast<int>(c.cardinal);
                }
            }
        }
    }
}

// ...and that no-op is a real residual, not an absence of one: at effSub == 1
// fractional content still casts from its lattice cell while receiving at its
// true sub-cell position. This is the arm that would go quiet if someone
// "fixed" the boundary by rounding differently.
TEST(PerAxisCastSubCell, EffSubOneResidualIsRealAndBoundedByHalfACell) {
    Case base;
    base.subdivisions = 1;
    base.uFrac4 = 0; // -0.5, the worst case
    base.vFrac4 = 15;
    base.wFrac4 = 8;
    for (const Case &c : poses(base)) {
        const float after = maxAbsDelta(castWorldSubCell(c), receiveWorld(c));
        EXPECT_GT(after, 0.0f)
            << "axis=" << c.axis << " cardinal=" << static_cast<int>(c.cardinal);
        EXPECT_LE(after, layoutQuantum(c))
            << "axis=" << c.axis << " cardinal=" << static_cast<int>(c.cardinal);
    }
}

// ---------------------------------------------------------------------------
// The encode/decode round trip the two recoveries share. A drift here would
// desync both sides at once and neither arm above would notice.
// ---------------------------------------------------------------------------

TEST(PerAxisCastSubCell, EncodeDecodeRoundTripsEveryFracField) {
    const int rawDepth = 1234;
    for (int uFrac4 = 0; uFrac4 < 16; ++uFrac4) {
        for (int vFrac4 = 0; vFrac4 < 16; ++vFrac4) {
            for (int wFrac4 = 0; wFrac4 < 16; ++wFrac4) {
                for (const int flip : {0, 1}) {
                    for (int slot = 0; slot < 3; ++slot) {
                        const int encoded = encodeDepthWithFaceFrac(
                            rawDepth, slot, uFrac4, vFrac4, wFrac4, flip
                        );
                        // Mirrors of the ir_iso_common decoders.
                        ASSERT_EQ(encoded >> 15, rawDepth);
                        ASSERT_EQ((encoded >> 11) & 15, wFrac4);
                        ASSERT_EQ((encoded >> 10) & 1, flip);
                        ASSERT_EQ((encoded >> 6) & 15, uFrac4);
                        ASSERT_EQ((encoded >> 2) & 15, vFrac4);
                        ASSERT_EQ(encoded & 3, slot);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// What a PORTED harness cannot prove on its own — and the tripwire for it.
//
// Every arm above models both formulations in this file, so it establishes that
// the fix's math is right, NOT that the shipped shaders implement it. A revert
// of either twin would leave all of them green. These two read the shipped
// source and pin the one thing text can pin: that both backends route through
// the shared decode. AC 1 of #2816 is explicitly a backend-symmetry criterion
// ("a one-sided fix creates a parity gap"), and this is its mechanical gate —
// the GL/Metal halves are edited by hand in lockstep with nothing else checking
// they stayed in step.
// ---------------------------------------------------------------------------

std::string readShaderSource(const std::string &relativePath) {
    const std::filesystem::path path =
        std::filesystem::path{IR_TEST_RENDER_SHADER_DIR} / relativePath;
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << "cannot open " << path.string();
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

TEST(PerAxisCastSubCell, BothBackendsRouteTheCastThroughTheSharedFracDecode) {
    for (const char *twin :
         {"c_resolve_per_axis_screen_depth.glsl",
          "metal/c_resolve_per_axis_screen_depth.metal"}) {
        const std::string source = readShaderSource(twin);
        EXPECT_NE(source.find("perAxisSubCellFrac("), std::string::npos)
            << twin
            << " no longer applies the per-axis sub-cell frac to the sun-shadow"
               " CAST bridge — see #2816. A lattice-only recovery here deposits"
               " the caster up to half a world cell off the surface"
               " perAxisCellToWorld3DSubCell samples on the RECEIVE side.";
    }
}

// The recovery and the bridge must read the frac layout from one definition;
// that is what stops the two seams drifting again the way an inlined copy of
// perAxisCellToWorld3D did.
TEST(PerAxisCastSubCell, BothBackendsDefineTheSharedFracDecodeOnce) {
    for (const char *twin :
         {"ir_per_axis_lighting.glsl", "metal/ir_per_axis_lighting.metal"}) {
        const std::string source = readShaderSource(twin);
        EXPECT_NE(source.find("perAxisSubCellFrac("), std::string::npos) << twin;
        // The sub-cell recovery composes the shared decode rather than
        // re-deriving `/ 16.0 - 0.5` inline.
        EXPECT_NE(source.find("perAxisCellToWorld3DSubCell("), std::string::npos)
            << twin;
    }
}

// The store's own cell/key are what the harness feeds both recoveries; if the
// iso identity they rest on ever stopped holding, every delta above would be
// measuring the wrong thing.
TEST(PerAxisCastSubCell, StoreCellAndKeyInvertToTheFacePosition) {
    Case c;
    c.frameCanvasOffset = vec2(37.75f, -12.25f);
    for (const ivec3 facePos :
         {ivec3(0, 0, 0), ivec3(4, -3, 7), ivec3(-11, 9, -2), ivec3(31, 31, 31)}) {
        c.facePos = facePos;
        EXPECT_EQ(IRMath::roundVec3HalfUp(latticeOrigin(c)), facePos);
        EXPECT_EQ(storeEncoded(c) >> 15, IRMath::pos3DtoDistance(facePos));
    }
}

} // namespace
