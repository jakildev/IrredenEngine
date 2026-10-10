// FogCrossSection exercises the per-fragment fog clip.
//
// The clip lives in VOXEL_TO_TRIXEL_STAGE_1: a column the vision disc merely
// CLIPS is KEPT and rasters its full footprint, and FOG_TO_TRIXEL then trims it
// per pixel on the same analytic curve — rather than the object ending on the
// voxel lattice with FOG_TO_TRIXEL hard-blacking the faces past it.
//
// Two halves, matching what a GL host can and cannot prove:
//
//   * `FogCrossSection` (GL only) — Tests A-D. Stands up the hidden GL 4.5
//     context of gpu_compute_dispatch_test.cpp, dispatches a probe kernel that
//     includes the REAL clip definitions (ir_voxel_face_select.glsl, not a
//     copy), and asserts A-D against the readback.
//   * `FogCrossSectionShaderParity` (every backend) — Test E. A GL host cannot
//     dispatch Metal, so the Metal side is pinned by asserting the two sources
//     carry the same constants and the same reveal expressions, which is the
//     contract both files' headers already state ("keep byte-identical math").
//
// The tests are stated over the clip's CURVES rather than over trixelColors
// pixels. That is deliberate, and it is what makes them assertions rather than
// screenshots: every property A-D names — no boundary black, no coverage pop,
// no interior holes, floor/object edge agreement — is a property of
// `fogColumnRevealNearest` and `fogVisionCircleReveal`, and a pixel-level
// restatement would need the full voxel-pool binding set to say the same thing
// less precisely. Test A carries an explicit non-vacuity control that fails
// under a centre-only clip, so the suite is a real repro and not just a
// tautology over whatever the shader currently computes.

#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/fog_line_of_sight.hpp>

#include <cstddef>
#include <fstream>
#include <regex>
#include <span>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

namespace {

// ---------------------------------------------------------------------------
// Shared CPU mirrors + shader-source helpers (used by both halves).
// ---------------------------------------------------------------------------

// CPU mirror of `fogVisionCircleReveal` (ir_iso_common.glsl) — the ONE analytic
// curve the floor's per-pixel reveal and the per-voxel object clip share.
// `circle` = (centerX, centerY, radius, edgeSoftness).
float visionCircleReveal(IRMath::vec2 worldXY, IRMath::vec4 circle, float aa) {
    const float dist = IRMath::length(worldXY - IRMath::vec2(circle));
    const float a = IRMath::max(circle.w, aa);
    return 1.0f - IRMath::smoothstep(circle.z - a, circle.z + a, dist);
}

// CPU mirror of `fogDiscRevealAtDistance` (ir_voxel_face_select.glsl) — the
// per-column disc test. A hard disc reveals strictly inside the radius, so a
// column exactly on the rim is hidden.
float discRevealAtDistance(float dist, float radius, float softness) {
    if (softness <= 0.0f) {
        return dist < radius ? 1.0f : 0.0f;
    }
    return 1.0f - IRMath::smoothstep(radius - softness, radius + softness, dist);
}

std::string readShaderSource(const std::string &path) {
    std::ifstream file(path);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

// Mirrors of the shader-side constants declared in ir_voxel_face_select.glsl.
// Test E asserts these against BOTH shader sources, so a one-sided edit to
// either backend fails here rather than surviving to a screenshot.
constexpr float kFogColumnCellHalf = 0.5f;
constexpr float kFogColumnKeepAa = 0.5f;
constexpr float kFogHiddenKeepCells = 8.0f;

} // namespace

// ---------------------------------------------------------------------------
// Test E — GL/Metal parity, asserted at the source level.
// ---------------------------------------------------------------------------

namespace {

const std::string kGlslFaceSelectPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/ir_voxel_face_select.glsl";
const std::string kMetalFaceSelectPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/ir_voxel_face_select.metal";
const std::string kGlslIsoCommonPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/ir_iso_common.glsl";
const std::string kMetalIsoCommonPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/ir_iso_common.metal";
const std::string kGlslStage1BodyPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/c_voxel_to_trixel_stage_1_body.glsl";
const std::string kMetalStage1BodyPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/c_voxel_to_trixel_stage_1_body.metal";
const std::string kGlslFogCommonPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/ir_fog_common.glsl";
const std::string kMetalFogCommonPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/ir_fog_common.metal";
const std::string kGlslFogColorPath = std::string(IR_TEST_RENDER_SHADER_DIR) + "/ir_fog_color.glsl";
const std::string kMetalFogColorPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/ir_fog_color.metal";
const std::string kGlslCompositePath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/ir_trixel_to_framebuffer_body.glsl";
const std::string kMetalCompositePath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/ir_trixel_to_framebuffer_body.metal";
const std::string kGlslStage2BodyPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/c_voxel_to_trixel_stage_2_body.glsl";
const std::string kMetalStage2BodyPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/c_voxel_to_trixel_stage_2_body.metal";
const std::string kGlslShapeBodyPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/c_shapes_to_trixel_body.glsl";
const std::string kMetalShapeBodyPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/c_shapes_to_trixel_body.metal";
const std::string kGlslFogPassPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/c_fog_to_trixel.glsl";
const std::string kMetalFogPassPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/c_fog_to_trixel.metal";
const std::string kGlslFogOverflowPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/c_fog_overflow_faces.glsl";
const std::string kMetalFogOverflowPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/c_fog_overflow_faces.metal";
const std::string kGlslFogShadowCasterPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/ir_fog_shadow_caster.glsl";
const std::string kMetalFogShadowCasterPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/ir_fog_shadow_caster.metal";

// Reduces a GLSL or MSL snippet to the dialect-free math it expresses: line
// comments dropped, MSL vector spellings folded onto the GLSL ones, float
// literal suffixes dropped, the Metal-only `obs.` observer-struct qualifier
// dropped (GLSL reaches the same fields through a named uniform block), and all
// whitespace collapsed. Two snippets that normalize equal compute the same
// thing on both backends; anything that survives normalization is a real
// divergence, not a formatting one.
std::string normalizeShaderMath(const std::string &source) {
    const std::string noComments = std::regex_replace(source, std::regex(R"(//[^\n]*)"), "");
    const std::string noObs = std::regex_replace(noComments, std::regex(R"(\bobs\.)"), "");
    std::string folded = std::regex_replace(noObs, std::regex(R"(\bfloat([234])\b)"), "vec$1");
    folded = std::regex_replace(folded, std::regex(R"(\buint([234])\b)"), "uvec$1");
    folded = std::regex_replace(folded, std::regex(R"(\bint([234])\b)"), "ivec$1");
    folded = std::regex_replace(folded, std::regex(R"((\d)f\b)"), "$1");
    folded = std::regex_replace(folded, std::regex(R"(\s+)"), " ");
    const std::string trimmedFront = std::regex_replace(folded, std::regex(R"(^ )"), "");
    return std::regex_replace(trimmedFront, std::regex(R"( $)"), "");
}

// Extracts the text from the first `startToken` at or after `anchor` up to
// (not including) the next `endToken`. Empty when any token is missing.
std::string extractSpan(
    const std::string &source,
    const std::string &anchor,
    const std::string &startToken,
    const std::string &endToken
) {
    const std::size_t anchorAt = source.find(anchor);
    if (anchorAt == std::string::npos) {
        return {};
    }
    const std::size_t startAt = source.find(startToken, anchorAt);
    if (startAt == std::string::npos) {
        return {};
    }
    const std::size_t endAt = source.find(endToken, startAt);
    if (endAt == std::string::npos) {
        return {};
    }
    return source.substr(startAt, endAt - startAt);
}

// Extracts the `float reveal = 0.0 … return reveal;` accumulation out of a
// column-reveal function. That span is the portion the two backends express
// identically — the surrounding grid-memory / out-of-range short-circuits
// differ by necessity (`imageLoad` vs `fog.read`, `imageSize` vs
// `get_width`), so comparing whole bodies there would fail on dialect alone.
//
// The anchor is `<name>(`, not the bare name: both files mention
// `fogColumnRevealZ` (the unpainted-route Z twin, which lives in the stage body)
// in a comment ABOVE these definitions, and a bare-name search matches that
// prefix — landing the span on fogColumnReveal in BOTH files, so the test
// compares one function to itself and passes no matter how far the twins have
// drifted. Verified by mutation: with the anchor fixed, a one-sided edit to
// the GLSL nearest-cell clamp fails this test.
std::string extractRevealAccumulation(const std::string &source, const std::string &functionName) {
    return extractSpan(source, functionName + "(", "float reveal = 0.0", "return reveal;");
}

// Extracts a function's `{ … }` body by brace matching from its declaration.
std::string extractFunctionBody(const std::string &source, const std::string &functionName) {
    const std::size_t functionAt = source.find(functionName + "(");
    if (functionAt == std::string::npos) {
        return {};
    }
    const std::size_t openAt = source.find('{', functionAt);
    if (openAt == std::string::npos) {
        return {};
    }
    int depth = 0;
    for (std::size_t at = openAt; at < source.size(); ++at) {
        if (source[at] == '{') {
            ++depth;
        } else if (source[at] == '}') {
            --depth;
            if (depth == 0) {
                return source.substr(openAt, at - openAt + 1);
            }
        }
    }
    return {};
}

// normalizeShaderMath plus the kernel-scope plumbing only MSL spells out: the
// `frameData.` / `fogObservers.` struct qualifiers and the fog texture +
// observer arguments plus the line-of-sight texture Metal passes to the shared
// reveal functions. Padding just
// inside parentheses is dropped too, so a call the formatter wrapped onto its
// own lines compares equal to the same call on one line.
std::string normalizeKernelMath(const std::string &source) {
    std::string noArgs =
        std::regex_replace(source, std::regex(R"(\bcanvasFogOfWar,\s*fogObservers,\s*)"), "");
    noArgs = std::regex_replace(noArgs, std::regex(R"(,\s*fogLineOfSight\s*\))"), ")");
    noArgs = std::regex_replace(noArgs, std::regex(R"(,\s*fogObservers\s*\))"), ")");
    const std::string normalized = normalizeShaderMath(
        std::regex_replace(noArgs, std::regex(R"(\b(frameData|fogObservers)\.)"), "")
    );
    return std::regex_replace(
        std::regex_replace(normalized, std::regex(R"(\(\s+)"), "("),
        std::regex(R"(\s+\))"),
        ")"
    );
}

// Reads the numeric literal a named shader constant is initialized to, in
// either dialect (`const float kX = 8.0;` / `constant float kX = 8.0f;`).
// Returns false when the constant is absent, which is itself a parity failure.
bool readShaderConstant(const std::string &source, const std::string &name, double &value) {
    std::smatch match;
    const std::regex pattern(R"(\b)" + name + R"(\s*=\s*(-?[0-9]+(?:\.[0-9]*)?)[fu]?\s*;)");
    if (!std::regex_search(source, match, pattern)) {
        return false;
    }
    value = std::stod(match[1].str());
    return true;
}

} // namespace

// Test E, part 1: the six constants the fog clip is parameterized by agree
// across backends AND agree with this test's own mirrors. A one-sided bump to
// kFogHiddenKeepCells (the keep-ring width the cross-section cut depends
// on) is exactly the drift a GL-only smoke cannot see.
TEST(FogCrossSectionShaderParity, ClipConstantsAgreeAcrossBackends) {
    const std::string glsl = readShaderSource(kGlslFaceSelectPath);
    const std::string metal = readShaderSource(kMetalFaceSelectPath);
    ASSERT_FALSE(glsl.empty()) << "could not read " << kGlslFaceSelectPath;
    ASSERT_FALSE(metal.empty()) << "could not read " << kMetalFaceSelectPath;

    const std::pair<const char *, double> expected[] = {
        {"kFogExploredThreshold", 0.25},
        {"kMaxFogVisionCircles", 8.0},
        {"kFogColumnCellHalf", static_cast<double>(kFogColumnCellHalf)},
        {"kFogColumnKeepAa", static_cast<double>(kFogColumnKeepAa)},
        {"kFogHiddenKeepCells", static_cast<double>(kFogHiddenKeepCells)},
    };
    for (const auto &[name, mirrored] : expected) {
        double glslValue = 0.0;
        double metalValue = 0.0;
        ASSERT_TRUE(readShaderConstant(glsl, name, glslValue)) << name << " missing from GLSL";
        ASSERT_TRUE(readShaderConstant(metal, name, metalValue)) << name << " missing from MSL";
        EXPECT_DOUBLE_EQ(glslValue, metalValue)
            << name << " diverged between the GLSL and MSL fog clips";
        EXPECT_DOUBLE_EQ(glslValue, mirrored)
            << name << " changed in the shaders without updating this test's mirror";
    }
}

// Test E, part 1b: the window tap. Every include family that taps the fog
// grid defines `fogWindowTexel` (the toroidal address of a world column, or
// (-1, -1) outside the window) and declares the origin lanes the tap reads;
// the mapping must be the same expression in every file on both backends, or
// one pass reads a different texel than the others for the same column.
TEST(FogCrossSectionShaderParity, WindowTexelMappingIsIdenticalAcrossBackends) {
    const std::string kGlslCompactPath =
        std::string(IR_TEST_RENDER_SHADER_DIR) + "/c_voxel_visibility_compact.glsl";
    const std::string kMetalCompactPath =
        std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/c_voxel_visibility_compact.metal";
    const std::string reference =
        extractFunctionBody(readShaderSource(kGlslFaceSelectPath), "fogWindowTexel");
    ASSERT_FALSE(reference.empty()) << "fogWindowTexel not found in ir_voxel_face_select.glsl";
    EXPECT_NE(reference.find("% fogSize"), std::string::npos)
        << "the tap must wrap through the window edge, not offset by a literal";
    EXPECT_EQ(reference.find("128"), std::string::npos) << "no legacy half-extent literal";
    const std::string normalizedReference = std::regex_replace(
        normalizeShaderMath(reference),
        std::regex(R"(\bint([234])\b)"),
        "ivec$1"
    );
    for (const std::string &path :
         {kMetalFaceSelectPath,
          kGlslFogCommonPath,
          kMetalFogCommonPath,
          kGlslCompactPath,
          kMetalCompactPath}) {
        const std::string source = readShaderSource(path);
        ASSERT_FALSE(source.empty()) << "could not read " << path;
        const std::string body = extractFunctionBody(source, "fogWindowTexel");
        ASSERT_FALSE(body.empty()) << "fogWindowTexel not found in " << path;
        EXPECT_EQ(
            std::regex_replace(
                normalizeShaderMath(body),
                std::regex(R"(\bint([234])\b)"),
                "ivec$1"
            ),
            normalizedReference
        ) << "fogWindowTexel diverged in "
          << path;
        EXPECT_TRUE(
            std::regex_search(
                source,
                std::regex(
                    R"(int visionCircleCount;\s*(//[^\n]*\s*)*int losSourceMask;\s*(//[^\n]*\s*)*)"
                    R"(int windowOriginX;\s*(//[^\n]*\s*)*int windowOriginY;)"
                )
            )
        ) << path
          << " must spell the observer tail out as count, mask, windowOriginX, windowOriginY";
        EXPECT_NE(source.find("windowOriginX, "), std::string::npos)
            << path << " never reads the origin lanes";
    }
    for (const std::string &path : {kGlslStage1BodyPath, kMetalStage1BodyPath}) {
        const std::string body = extractFunctionBody(readShaderSource(path), "fogColumnRevealZ");
        ASSERT_FALSE(body.empty()) << "fogColumnRevealZ not found in " << path;
        EXPECT_NE(body.find("fogWindowTexel("), std::string::npos)
            << path << "'s z-aware drop must tap through the window";
    }
}

// Test E, part 2: the shared analytic curve itself. `fogVisionCircleReveal` is
// the "one formula, no CPU/GPU or GL/Metal drift" claim in code — both
// bodies must reduce to the same expression.
TEST(FogCrossSectionShaderParity, VisionCircleRevealCurveIsIdenticalAcrossBackends) {
    const std::string glslBody =
        extractFunctionBody(readShaderSource(kGlslIsoCommonPath), "float fogVisionCircleReveal");
    const std::string metalBody =
        extractFunctionBody(readShaderSource(kMetalIsoCommonPath), "float fogVisionCircleReveal");
    ASSERT_FALSE(glslBody.empty()) << "fogVisionCircleReveal not found in ir_iso_common.glsl";
    ASSERT_FALSE(metalBody.empty()) << "fogVisionCircleReveal not found in ir_iso_common.metal";

    EXPECT_EQ(normalizeShaderMath(glslBody), normalizeShaderMath(metalBody))
        << "the floor/object shared reveal curve diverged between backends";
}

// Test E, part 2b: the per-column disc test. Its hard-disc branch is the strict
// `dist < radius` step — smoothstep(R, R, d) is undefined and GL and Metal
// resolve its tie differently — and the cut-face rule and the detached
// own-column drop must both route through it: two spellings of the tie drop a
// rim column with no cut wall painted behind it.
TEST(FogCrossSectionShaderParity, DiscRevealIsOneStrictDefinitionOnBothBackends) {
    const std::string glslFaceSelect = readShaderSource(kGlslFaceSelectPath);
    const std::string metalFaceSelect = readShaderSource(kMetalFaceSelectPath);
    const std::string glslBody =
        extractFunctionBody(glslFaceSelect, "float fogDiscRevealAtDistance");
    const std::string metalBody =
        extractFunctionBody(metalFaceSelect, "float fogDiscRevealAtDistance");
    ASSERT_FALSE(glslBody.empty()) << "fogDiscRevealAtDistance not found in GLSL";
    ASSERT_FALSE(metalBody.empty()) << "fogDiscRevealAtDistance not found in MSL";
    EXPECT_EQ(normalizeShaderMath(glslBody), normalizeShaderMath(metalBody))
        << "the per-column disc test diverged between backends";
    EXPECT_NE(normalizeShaderMath(glslBody).find("1.0 - step(radius, dist)"), std::string::npos)
        << "the hard-disc branch must be the strict step form: " << glslBody;

    for (const std::string *source : {&glslFaceSelect, &metalFaceSelect}) {
        EXPECT_NE(
            extractRevealAccumulation(*source, "fogColumnReveal").find("fogDiscRevealAtDistance("),
            std::string::npos
        ) << "the cut-face rule must evaluate the disc through fogDiscRevealAtDistance";
    }
    for (const std::string &path : {kGlslStage1BodyPath, kMetalStage1BodyPath}) {
        const std::string body =
            extractFunctionBody(readShaderSource(path), "float fogColumnRevealZ");
        ASSERT_FALSE(body.empty()) << "fogColumnRevealZ not found in " << path;
        EXPECT_NE(body.find("fogDiscRevealAtDistance(distEff"), std::string::npos)
            << path << "'s own-column drop must evaluate the disc through fogDiscRevealAtDistance";
    }
}

// Test E, part 3: the two column-reveal accumulations — the stage-1 cut-face
// test and the shipped KEEP metric. Their short-circuits are dialect-specific,
// but the reveal maths must match; the nearest-cell clamp and the
// keepAa + keepCells widening are where a silent one-sided edit would land.
TEST(FogCrossSectionShaderParity, ColumnRevealAccumulationsAreIdenticalAcrossBackends) {
    const std::string glsl = readShaderSource(kGlslFaceSelectPath);
    const std::string metal = readShaderSource(kMetalFaceSelectPath);

    for (const char *functionName : {"fogColumnReveal", "fogColumnRevealNearest"}) {
        const std::string glslMath = extractRevealAccumulation(glsl, functionName);
        const std::string metalMath = extractRevealAccumulation(metal, functionName);
        ASSERT_FALSE(glslMath.empty()) << functionName << " accumulation not found in GLSL";
        ASSERT_FALSE(metalMath.empty()) << functionName << " accumulation not found in MSL";
        EXPECT_EQ(normalizeShaderMath(glslMath), normalizeShaderMath(metalMath))
            << functionName << " diverged between the GLSL and MSL fog clips";
    }
}

// Test E, part 4: stage 1's own-column drops. The world canvas drop is z-free —
// FIELD matter FOG_TO_TRIXEL paints is never removed, and a one-sided swap back
// to a Z metric would render a hollow box on one backend. The detached and
// detached route keeps the z-aware twin because it has no paint pass. The
// per-axis route is painted and therefore uses the same z-free nearest metric
// as the world route.
TEST(FogCrossSectionShaderParity, StageOneDropExpressionsAreIdenticalAcrossBackends) {
    const std::string glsl = readShaderSource(kGlslStage1BodyPath);
    const std::string metal = readShaderSource(kMetalStage1BodyPath);
    ASSERT_FALSE(glsl.empty()) << "could not read " << kGlslStage1BodyPath;
    ASSERT_FALSE(metal.empty()) << "could not read " << kMetalStage1BodyPath;

    const std::string glslSingle = extractSpan(glsl, "ownColumnHidden =", "ownColumnHidden =", ";");
    const std::string metalSingle =
        extractSpan(metal, "ownColumnHidden =", "ownColumnHidden =", ";");
    ASSERT_FALSE(glslSingle.empty()) << "single-canvas drop not found in GLSL";
    ASSERT_FALSE(metalSingle.empty()) << "single-canvas drop not found in MSL";
    EXPECT_EQ(normalizeKernelMath(glslSingle), normalizeKernelMath(metalSingle))
        << "the single-canvas drop diverged between backends";
    const std::string gridBranch = glslSingle.substr(glslSingle.find(':'));
    EXPECT_EQ(gridBranch.find("RevealZ"), std::string::npos)
        << "the non-detached single-canvas drop must be z-free: " << gridBranch;

    const std::string glslPerAxis =
        extractSpan(glsl, "perAxisRoute != 0) {", "if (!fogWholeBodyExempt", "return;");
    const std::string metalPerAxis =
        extractSpan(metal, "perAxisRoute != 0) {", "if (!fogWholeBodyExempt", "return;");
    ASSERT_FALSE(glslPerAxis.empty()) << "per-axis drop not found in GLSL";
    ASSERT_FALSE(metalPerAxis.empty()) << "per-axis drop not found in MSL";
    EXPECT_EQ(normalizeKernelMath(glslPerAxis), normalizeKernelMath(metalPerAxis))
        << "the per-axis drop diverged between backends";
    EXPECT_NE(glslPerAxis.find("fogColumnRevealNearest("), std::string::npos)
        << "the painted per-axis drop must match the world route's z-free metric: " << glslPerAxis;
    EXPECT_EQ(glslPerAxis.find("RevealZ"), std::string::npos)
        << "the painted per-axis drop must not depend on height: " << glslPerAxis;
}

// Test E, part 5: all fog routes call one backend-parity per-sample shading
// body, including the state-0 unexplored-colour anchor.
TEST(FogCrossSectionShaderParity, UnexploredColourAnchorIsIdenticalAcrossBackends) {
    const std::string glsl = readShaderSource(kGlslFogColorPath);
    const std::string metal = readShaderSource(kMetalFogColorPath);
    const std::string anchor = "const float t = state / kFogExploredValue;";
    const std::string glslAnchor = extractSpan(glsl, anchor, anchor, "}");
    const std::string metalAnchor = extractSpan(metal, anchor, anchor, "}");
    ASSERT_FALSE(glslAnchor.empty()) << "unexplored anchor not found in " << kGlslFogColorPath;
    ASSERT_FALSE(metalAnchor.empty()) << "unexplored anchor not found in " << kMetalFogColorPath;
    EXPECT_EQ(normalizeKernelMath(glslAnchor), normalizeKernelMath(metalAnchor))
        << "the unexplored-colour anchor diverged between backends";
    EXPECT_NE(glslAnchor.find("unexplored"), std::string::npos)
        << "the fog pass must anchor state 0 on its unexplored colour: " << glslAnchor;
}

// The reveal loop (from the grid read to the return) and the colour apply
// (from the state lerp to the alpha-preserving return) normalize equal.
TEST(FogCrossSectionShaderParity, CommonFogShadingIsIdenticalAcrossBackends) {
    const std::string glsl = readShaderSource(kGlslFogCommonPath);
    const std::string metal = readShaderSource(kMetalFogCommonPath);
    const std::string glslReveal = extractSpan(
        glsl,
        "FogReveal fogRevealSample(",
        "float state = gridState;",
        "return FogReveal"
    );
    const std::string metalReveal = extractSpan(
        metal,
        "FogReveal fogRevealSample(",
        "float state = gridState;",
        "return FogReveal"
    );
    ASSERT_FALSE(glslReveal.empty()) << "fogRevealSample loop not found in GLSL";
    ASSERT_FALSE(metalReveal.empty()) << "fogRevealSample loop not found in MSL";
    EXPECT_EQ(normalizeKernelMath(glslReveal), normalizeKernelMath(metalReveal))
        << "the shared fog reveal diverged between backends";

    const std::string glslApply =
        extractSpan(glsl, "vec4 fogApplyReveal(", "vec3 outColor", "return vec4(");
    const std::string metalApply =
        extractSpan(metal, "float4 fogApplyReveal(", "float3 outColor", "return float4(");
    ASSERT_FALSE(glslApply.empty()) << "fogApplyReveal body not found in GLSL";
    ASSERT_FALSE(metalApply.empty()) << "fogApplyReveal body not found in MSL";
    EXPECT_EQ(normalizeKernelMath(glslApply), normalizeKernelMath(metalApply))
        << "the shared fog colour apply diverged between backends";

    // The BODY apply and the overflow lane's BODY state decode.
    for (const auto &[glslName, metalName] :
         {std::pair{"vec4 fogApplyBody", "float4 fogApplyBody"},
          std::pair{"float fogOverflowBodyState", "float fogOverflowBodyState"}}) {
        SCOPED_TRACE(glslName);
        const std::string glslBody = extractFunctionBody(glsl, glslName);
        const std::string metalBody = extractFunctionBody(metal, metalName);
        ASSERT_FALSE(glslBody.empty()) << glslName << " not found in GLSL";
        ASSERT_FALSE(metalBody.empty()) << metalName << " not found in MSL";
        EXPECT_EQ(normalizeKernelMath(glslBody), normalizeKernelMath(metalBody))
            << glslName << " diverged between backends";
    }
    const std::string glslBodyApply = extractFunctionBody(glsl, "vec4 fogApplyBody");
    EXPECT_NE(
        glslBodyApply.find("fogStateColor(state, sourceColor.rgb, unexploredColor.rgb)"),
        std::string::npos
    ) << "a BODY takes the shared state curve at its own factor: "
      << glslBodyApply;
    EXPECT_EQ(glslBodyApply.find("hardDistPastRim"), std::string::npos)
        << "a BODY takes no rim fade and no cut cap";
}

// The paint pass admits an analytic source only where its mask intersects the
// sampled cell's mask (the window's green lane), on both backends; the old
// literal default-channel test is gone.
TEST(FogCrossSectionShaderParity, ChannelSourceSkipIsIdenticalAcrossBackends) {
    const std::string glsl = readShaderSource(kGlslFogCommonPath);
    const std::string metal = readShaderSource(kMetalFogCommonPath);
    const std::string glslSkip =
        extractSpan(glsl, "if ((visionCircleChannels", "if ((visionCircleChannels", "}");
    const std::string metalSkip = extractSpan(
        metal,
        "if ((fogObservers.visionCircleChannels",
        "if ((fogObservers.visionCircleChannels",
        "}"
    );
    ASSERT_FALSE(glslSkip.empty());
    ASSERT_FALSE(metalSkip.empty());
    EXPECT_EQ(normalizeKernelMath(glslSkip), normalizeKernelMath(metalSkip));
    EXPECT_NE(glslSkip.find("& cellChannels) == 0u"), std::string::npos)
        << "the skip must intersect the sampled cell's mask: " << glslSkip;
    for (const std::string *source : {&glsl, &metal}) {
        EXPECT_EQ(source->find("& 1u) == 0u"), std::string::npos)
            << "no literal default-channel admission remains";
        EXPECT_NE(source->find("fogTexelState(gridTexel.x)"), std::string::npos);
        EXPECT_NE(source->find("cellChannels = gridTexel.y"), std::string::npos);
    }
}

// The fog window is RG32UI on both backends: every tap family declares the
// integer image, the shared decode of the red lane normalizes equal, every
// threshold comparison routes through it, and the out-of-window read is
// (unexplored, default channel) in both twins.
TEST(FogCrossSectionShaderParity, IntegerFogWindowReadsAreIdenticalAcrossBackends) {
    const std::string kGlslCompactPath =
        std::string(IR_TEST_RENDER_SHADER_DIR) + "/c_voxel_visibility_compact.glsl";
    const std::string kMetalCompactPath =
        std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/c_voxel_visibility_compact.metal";
    const std::string kMetalCompactPathCopy = kMetalCompactPath;

    for (const std::string &path : {kGlslFogCommonPath, kGlslFaceSelectPath, kGlslCompactPath}) {
        const std::string source = readShaderSource(path);
        ASSERT_FALSE(source.empty()) << "could not read " << path;
        EXPECT_TRUE(
            std::regex_search(
                source,
                std::regex(
                    R"(layout\(rg32ui, binding = [A-Za-z_0-9]+\) readonly uniform uimage2D canvasFogOfWar;)"
                )
            )
        ) << path
          << " must declare the fog window as an rg32ui uimage2D";
        EXPECT_EQ(source.find("uniform image2D canvasFogOfWar"), std::string::npos)
            << path << " still declares a normalized fog image";
    }
    for (const std::string &path :
         {kMetalFogCommonPath,
          kMetalCompactPathCopy,
          kMetalStage1BodyPath,
          kMetalStage2BodyPath,
          kMetalFogPassPath,
          kMetalFogOverflowPath}) {
        const std::string source = readShaderSource(path);
        ASSERT_FALSE(source.empty()) << "could not read " << path;
        EXPECT_EQ(source.find("texture2d<float, access::read> canvasFogOfWar"), std::string::npos)
            << path << " still takes a float fog texture";
        EXPECT_NE(source.find("texture2d<uint, access::read> canvasFogOfWar"), std::string::npos)
            << path << " must take the fog window as texture2d<uint>";
    }
    {
        const std::string source = readShaderSource(kMetalFaceSelectPath);
        EXPECT_EQ(source.find("texture2d<float, access::read> fog"), std::string::npos);
        EXPECT_NE(source.find("texture2d<uint, access::read> fog"), std::string::npos);
    }

    const std::string glslDecode =
        extractFunctionBody(readShaderSource(kGlslIsoCommonPath), "float fogTexelState");
    const std::string metalDecode =
        extractFunctionBody(readShaderSource(kMetalIsoCommonPath), "float fogTexelState");
    ASSERT_FALSE(glslDecode.empty()) << "fogTexelState not found in ir_iso_common.glsl";
    ASSERT_FALSE(metalDecode.empty()) << "fogTexelState not found in ir_iso_common.metal";
    EXPECT_EQ(normalizeShaderMath(glslDecode), normalizeShaderMath(metalDecode));
    EXPECT_NE(normalizeShaderMath(glslDecode).find("/ 255.0"), std::string::npos)
        << "state consumers divide the red lane by 255: " << glslDecode;

    // Every threshold comparison decodes the red lane first.
    for (const std::string &path :
         {kGlslFaceSelectPath,
          kMetalFaceSelectPath,
          kGlslCompactPath,
          kMetalCompactPathCopy,
          kGlslStage1BodyPath,
          kMetalStage1BodyPath}) {
        std::istringstream lines(readShaderSource(path));
        std::string line;
        int comparisons = 0;
        while (std::getline(lines, line)) {
            const bool compares = line.find(">= kFogExploredThreshold") != std::string::npos ||
                                  line.find("< kFogExploredThreshold") != std::string::npos;
            const bool comment = line.find("//") != std::string::npos &&
                                 line.find("//") < line.find("kFogExploredThreshold");
            if (!compares || comment) {
                continue;
            }
            ++comparisons;
            EXPECT_NE(line.find("fogTexelState("), std::string::npos)
                << path << " compares an undecoded fog lane: " << line;
        }
        EXPECT_GT(comparisons, 0) << path << " has no threshold comparison to check";
    }

    // The out-of-window read and the two-lane fetch.
    const std::string glslTap =
        extractFunctionBody(readShaderSource(kGlslFogCommonPath), "fogTapTexel");
    const std::string metalTap =
        extractFunctionBody(readShaderSource(kMetalFogCommonPath), "fogTapTexel");
    ASSERT_FALSE(glslTap.empty());
    ASSERT_FALSE(metalTap.empty());
    EXPECT_NE(glslTap.find("return uvec2(0u, kFogChannelDefault);"), std::string::npos) << glslTap;
    EXPECT_NE(metalTap.find("return uint2(0u, kFogChannelDefault);"), std::string::npos)
        << metalTap;
    EXPECT_NE(glslTap.find(".rg;"), std::string::npos);
    EXPECT_NE(metalTap.find(".rg;"), std::string::npos);
}

TEST(FogCrossSectionShaderParity, DetachedCanvasCompositeIsIdenticalAcrossBackends) {
    const std::string glsl = readShaderSource(kGlslCompositePath);
    const std::string metal = readShaderSource(kMetalCompositePath);
    const std::string glslColor = readShaderSource(kGlslFogColorPath);
    const std::string metalColor = readShaderSource(kMetalFogColorPath);
    const std::string glslApply = extractSpan(
        glsl,
        "if (fogBodyFactorEncoded != 0u)",
        "if (fogBodyFactorEncoded != 0u)",
        "gl_FragDepth = depth;"
    );
    const std::string metalApply = extractSpan(
        metal,
        "if (frameData.fogBodyFactorEncoded != 0u)",
        "if (frameData.fogBodyFactorEncoded != 0u)",
        "out.depth = depth;"
    );
    ASSERT_FALSE(glslApply.empty()) << "fog BODY composite not found in GLSL";
    ASSERT_FALSE(metalApply.empty()) << "fog BODY composite not found in MSL";
    const std::string normalizedGlsl = normalizeKernelMath(
        std::regex_replace(glslApply, std::regex(R"(\bFragColor\b)"), "out.color")
    );
    EXPECT_EQ(normalizedGlsl, normalizeKernelMath(metalApply))
        << "the detached-canvas BODY composite diverged between backends";
    EXPECT_NE(glslApply.find("fogStateColor("), std::string::npos)
        << "the detached composite must reuse the shared fog curve";

    const std::string glslCurve = extractFunctionBody(glslColor, "vec3 fogStateColor");
    const std::string metalCurve = extractFunctionBody(metalColor, "float3 fogStateColor");
    ASSERT_FALSE(glslCurve.empty()) << "fogStateColor not found in GLSL";
    ASSERT_FALSE(metalCurve.empty()) << "fogStateColor not found in MSL";
    EXPECT_EQ(normalizeShaderMath(glslCurve), normalizeShaderMath(metalCurve))
        << "the factored fog colour curve diverged between backends";
}

// Every route skips the colour read-modify-write for a fully revealed sample:
// the early return sits between the reveal and the colour read, on both
// backends. The BODY branch returns ahead of the FIELD reveal, and its own
// colour read sits behind its `< 1.0` guard.
TEST(FogCrossSectionShaderParity, FullyRevealedSamplesSkipTheColourWrite) {
    const std::tuple<std::string, std::string, std::string> kernels[] = {
        {kGlslFogPassPath, "imageLoad(trixelColors", "if (decodeFogBody(rawId))"},
        {kMetalFogPassPath, "trixelColors.read(", "if (decodeFogBody(rawId))"},
        {kGlslFogOverflowPath,
         "unpackColor(colorPacked)",
         "if (fogClassByte != kFogOverflowFieldByte)"},
        {kMetalFogOverflowPath,
         "unpackColor(colorPacked)",
         "if (fogClassByte != kFogOverflowFieldByte)"},
    };
    for (const auto &[path, colourRead, bodyBranch] : kernels) {
        const std::string kernel = readShaderSource(path);
        ASSERT_FALSE(kernel.empty()) << "could not read " << path;
        const std::size_t bodyAt = kernel.find(bodyBranch);
        ASSERT_NE(bodyAt, std::string::npos) << path << " lost its BODY branch";
        const std::size_t bodyGuardAt = kernel.find("if (bodyState < 1.0", bodyAt);
        const std::size_t bodyColourAt = kernel.find(colourRead, bodyAt);
        ASSERT_NE(bodyGuardAt, std::string::npos) << path << " lost the BODY early-out";
        EXPECT_LT(bodyGuardAt, bodyColourAt)
            << path << " reads a BODY colour before its fully-revealed early-out";

        const std::size_t revealAt = kernel.find("fogRevealSample(");
        const std::size_t earlyOutAt = kernel.find("if (reveal.state >= 1.0");
        const std::size_t colourAt = kernel.find(colourRead, revealAt);
        ASSERT_NE(revealAt, std::string::npos) << path << " no longer calls fogRevealSample";
        ASSERT_NE(earlyOutAt, std::string::npos) << path << " lost its fully-revealed early-out";
        ASSERT_NE(colourAt, std::string::npos) << path << " colour read not found";
        EXPECT_LT(bodyColourAt, revealAt) << path << " BODY branch must precede the FIELD reveal";
        EXPECT_LT(revealAt, earlyOutAt) << path;
        EXPECT_LT(earlyOutAt, colourAt)
            << path << " reads the colour before the fully-revealed early-out";
    }
}

TEST(FogCrossSectionShaderParity, OverflowFogClassEncodingIsIdenticalAcrossBackends) {
    const std::string glslStage = readShaderSource(kGlslStage1BodyPath);
    const std::string metalStage = readShaderSource(kMetalStage1BodyPath);
    const std::string glslAppend = extractSpan(
        glslStage,
        "const uint fogClassByte",
        "const uint fogClassByte",
        ";\n            overflowAppendTap"
    );
    const std::string metalAppend = extractSpan(
        metalStage,
        "const uint fogClassByte",
        "const uint fogClassByte",
        ";\n            overflowAppendTap"
    );
    ASSERT_FALSE(glslAppend.empty()) << "overflow fog class append not found in GLSL";
    ASSERT_FALSE(metalAppend.empty()) << "overflow fog class append not found in MSL";
    EXPECT_EQ(normalizeKernelMath(glslAppend), normalizeKernelMath(metalAppend));
    EXPECT_NE(
        glslAppend.find(
            "encodeFogOverflowClassByte(\n                fogWholeBodyExempt, "
            "(voxels[voxelIndex].reserved >> 4u) & 0xFFu"
        ),
        std::string::npos
    ) << "overflow class byte must carry the BODY bit and the reserved-word factor: "
      << glslAppend;

    const std::string glslOverflow = readShaderSource(kGlslFogOverflowPath);
    const std::string metalOverflow = readShaderSource(kMetalFogOverflowPath);
    const std::string glslDecode = extractSpan(
        glslOverflow,
        "const uint fogClassByte",
        "const uint fogClassByte",
        "\n        return;\n    }\n"
    );
    const std::string metalDecode = extractSpan(
        metalOverflow,
        "const uint fogClassByte",
        "const uint fogClassByte",
        "\n        return;\n    }\n"
    );
    ASSERT_FALSE(glslDecode.empty()) << "overflow fog class decode not found in GLSL";
    ASSERT_FALSE(metalDecode.empty()) << "overflow fog class decode not found in MSL";
    EXPECT_EQ(normalizeKernelMath(glslDecode), normalizeKernelMath(metalDecode));
    EXPECT_NE(glslDecode.find("fogClassByte != kFogOverflowFieldByte"), std::string::npos)
        << "overflow fog route must read every non-FIELD class byte as a BODY: " << glslDecode;
    EXPECT_NE(glslDecode.find("fogOverflowBodyState(fogClassByte)"), std::string::npos)
        << glslDecode;
    EXPECT_NE(glslDecode.find("fogApplyBody(bodyState, "), std::string::npos) << glslDecode;
}

// Test E, part 6: the line-of-sight gate. The pure helpers of the
// ir_fog_los include pair reduce to the same maths on both backends, their
// constants agree with each other and with the component they mirror, and both
// shared fog reveals scale a gated source by the gate and declare the mask
// lane and the parameter tail — so removing the gate from one backend fails
// here.
namespace {

const std::string kGlslFogLosPath = std::string(IR_TEST_RENDER_SHADER_DIR) + "/ir_fog_los.glsl";
const std::string kMetalFogLosPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/ir_fog_los.metal";

// normalizeShaderMath plus the integer-vector spellings (MSL int2 -> ivec2)
// and the texture argument Metal threads through the gate helpers.
std::string normalizeLosMath(const std::string &source) {
    const std::string noTexture =
        std::regex_replace(source, std::regex(R"(,\s*fogLineOfSight\s*\))"), ")");
    return std::regex_replace(
        normalizeShaderMath(noTexture),
        std::regex(R"(\bint([234])\b)"),
        "ivec$1"
    );
}

// The gate call site with the dialect-only differences removed: the Metal
// observer-struct qualifier and the texture argument.
std::string normalizeGateCallSite(const std::string &source) {
    std::string folded = std::regex_replace(source, std::regex(R"(\bfogObservers\.)"), "");
    folded = std::regex_replace(folded, std::regex(R"(,\s*fogLineOfSight\s*\))"), ")");
    return std::regex_replace(folded, std::regex(R"(\s+)"), " ");
}

} // namespace

TEST(FogCrossSectionShaderParity, LosGateIsIdenticalAcrossBackends) {
    const std::string glsl = readShaderSource(kGlslFogLosPath);
    const std::string metal = readShaderSource(kMetalFogLosPath);
    ASSERT_FALSE(glsl.empty()) << "could not read " << kGlslFogLosPath;
    ASSERT_FALSE(metal.empty()) << "could not read " << kMetalFogLosPath;

    const std::pair<const char *, double> expected[] = {
        {"kFogLosCellsPerUnit", static_cast<double>(IRComponents::kFogLosCellsPerUnit)},
        {"kFogLosFieldHalfExtent", static_cast<double>(IRComponents::kFogLosFieldHalfExtent)},
        {"kFogLosTextureSize", static_cast<double>(IRComponents::kFogLosTextureSize)},
        {"kFogLosLevelCount", static_cast<double>(IRComponents::kFogLosLevelCount)},
        {"kFogLosLevelBias", static_cast<double>(IRComponents::kFogLosLevelBias)},
        {"kFogLosClearanceTolerance", static_cast<double>(IRComponents::kFogLosClearanceTolerance)},
        {"kFogLosDiscMargin", static_cast<double>(IRPrefab::Fog::kFogLosDiscMargin)},
        {"kFogLosRimFadeCells", static_cast<double>(IRPrefab::Fog::kFogLosRimFadeCells)},
        {"kFogLosMarchNudge", static_cast<double>(IRPrefab::Fog::kFogLosMarchNudge)},
        {"kFogLosMaxMarchSteps", 4096.0},
    };
    for (const auto &[name, mirrored] : expected) {
        double glslValue = 0.0;
        double metalValue = 0.0;
        ASSERT_TRUE(readShaderConstant(glsl, name, glslValue)) << name << " missing from GLSL";
        ASSERT_TRUE(readShaderConstant(metal, name, metalValue)) << name << " missing from MSL";
        EXPECT_DOUBLE_EQ(glslValue, metalValue) << name << " diverged between backends";
        EXPECT_NEAR(glslValue, mirrored, 1e-9) << name << " changed without this test's mirror";
    }
    EXPECT_GE(static_cast<int>(4096), IRPrefab::Fog::kFogLosMaxMarchSteps)
        << "the shader march must allow at least the CPU march's steps";

    for (const char *helper :
         {"fogLosSourceGated",
          "fogLosCellInField",
          "fogLosLevelRowOffset",
          "fogLosBlockMin",
          "fogLosFieldMin",
          "fogLosBlockTop",
          "fogLosTopPlane",
          "fogLosReach",
          "fogLosEye",
          "fogLosTraceClearance",
          "fogLosVisibilityFromClearance",
          "fogLosVisibility",
          "fogLosCanonicalSample"}) {
        const std::string glslBody = extractFunctionBody(glsl, helper);
        const std::string metalBody = extractFunctionBody(metal, helper);
        ASSERT_FALSE(glslBody.empty()) << helper << " not found in ir_fog_los.glsl";
        ASSERT_FALSE(metalBody.empty()) << helper << " not found in ir_fog_los.metal";
        EXPECT_EQ(normalizeLosMath(glslBody), normalizeLosMath(metalBody))
            << helper << " diverged between the GLSL and MSL line-of-sight gates";
    }

    const std::regex gate(
        R"(losVisibility = fogLosVisibility\( ?fogLosEye\(visionCircles\[i\], heights\.x, )"
        R"(losParams\[i\]\.x\), losSample, losParams\[i\]\.y, losFieldMin ?\);)"
    );
    // Both kernels anchor the field on the window their grid tap reads.
    const std::regex anchor(
        R"(losFieldMin = fogLosFieldMin\( ?i(vec|nt)2\(windowOriginX, windowOriginY\), )"
        R"(fogSize\.x ?\);)"
    );
    for (const std::string &path : {kGlslFogCommonPath, kMetalFogCommonPath}) {
        const std::string kernel = readShaderSource(path);
        ASSERT_FALSE(kernel.empty()) << "could not read " << path;
        EXPECT_TRUE(std::regex_search(normalizeGateCallSite(kernel), gate))
            << path << " lost its line-of-sight gate";
        EXPECT_TRUE(std::regex_search(normalizeGateCallSite(kernel), anchor))
            << path << " no longer anchors the line-of-sight field on the fog window";
        EXPECT_TRUE(
            std::regex_search(
                kernel,
                std::regex(R"(int visionCircleCount;\s*(//[^\n]*\s*)*int losSourceMask;)")
            )
        ) << path
          << " no longer reads the mask lane right after the count";
        EXPECT_TRUE(
            std::regex_search(
                kernel,
                std::regex(
                    R"(unexploredColor;\s*(//[^\n]*\s*)*(vec4|float4) losParams\[kMaxFogVisionCircles\];)"
                )
            )
        ) << path
          << " no longer declares the line-of-sight parameter tail after the unexplored colour";
    }
}

// Test E, part 10: the reveal-surface ceiling and treatment. The pure helpers
// reduce to the same maths on both backends and take the integer-only hash
// form, the cut tone agrees with the C++ default, both fog passes declare
// the appended ceiling and treatment lanes after the channel masks, and the
// stage-1 mirrors declare the ceilings at that same offset while the
// compact pass stops before them.
TEST(FogCrossSectionShaderParity, RevealSurfaceHelpersAreIdenticalAcrossBackends) {
    const std::string glslIso = readShaderSource(kGlslIsoCommonPath);
    const std::string metalIso = readShaderSource(kMetalIsoCommonPath);
    const std::string glslCeiling = extractFunctionBody(glslIso, "fogCeilingVisibility");
    const std::string metalCeiling = extractFunctionBody(metalIso, "fogCeilingVisibility");
    ASSERT_FALSE(glslCeiling.empty()) << "fogCeilingVisibility not found in GLSL";
    ASSERT_FALSE(metalCeiling.empty()) << "fogCeilingVisibility not found in MSL";
    EXPECT_EQ(normalizeShaderMath(glslCeiling), normalizeShaderMath(metalCeiling));
    EXPECT_NE(glslCeiling.find("if (ceiling.x < 0.0)"), std::string::npos)
        << "a negative height must be the off sentinel: " << glslCeiling;
    EXPECT_NE(
        glslCeiling.find("1.0 - smoothstep(ceiling.x, ceiling.x + ceiling.y, dzUp)"),
        std::string::npos
    ) << "the fade must reach zero exactly at ceiling + fade: "
      << glslCeiling;

    const std::string glsl = readShaderSource(kGlslFogCommonPath);
    const std::string metal = readShaderSource(kMetalFogCommonPath);
    for (const char *helper :
         {"fogRevealSurfaceHash01", "fogRevealSurfaceBandWeight", "fogCutCapBlend"}) {
        SCOPED_TRACE(helper);
        const std::string glslBody = extractFunctionBody(glsl, helper);
        const std::string metalBody = extractFunctionBody(metal, helper);
        ASSERT_FALSE(glslBody.empty()) << helper << " not found in GLSL";
        ASSERT_FALSE(metalBody.empty()) << helper << " not found in MSL";
        EXPECT_EQ(normalizeShaderMath(glslBody), normalizeShaderMath(metalBody))
            << helper << " diverged between backends";
    }
    const std::string hash = extractFunctionBody(glsl, "fogRevealSurfaceHash01");
    for (const char *forbidden : {"sin(", "fract(", "frameCount", "yaw"}) {
        EXPECT_EQ(hash.find(forbidden), std::string::npos)
            << "the dissolve hash must be integer-only over the world voxel: " << hash;
    }
    EXPECT_NE(hash.find("0x7FEB352Du"), std::string::npos) << hash;

    double glslTone = 0.0;
    double metalTone = 0.0;
    ASSERT_TRUE(readShaderConstant(glsl, "kFogCutTone", glslTone));
    ASSERT_TRUE(readShaderConstant(metal, "kFogCutTone", metalTone));
    EXPECT_DOUBLE_EQ(glslTone, metalTone);
    EXPECT_EQ(static_cast<float>(glslTone), IRComponents::kFogCutTone)
        << "the shader cut tone must stay the treatment's C++ default";

    const std::regex tail(
        R"(visionCircleChannels\[2\];\s*(//[^\n]*\s*)*(vec4|float4) visionCircleCeilings\[kMaxFogVisionCircles\];)"
        R"(\s*(//[^\n]*\s*)*(vec4|float4) revealSurfaceTreatment;\s*\};)"
    );
    for (const std::string &path : {kGlslFogCommonPath, kMetalFogCommonPath}) {
        EXPECT_TRUE(std::regex_search(readShaderSource(path), tail))
            << path << " must declare the ceilings and the treatment as the block tail";
    }
    const std::regex stageTail(
        R"(visionCircleChannels\[2\];\s*(//[^\n]*\s*)*(vec4|float4) visionCircleCeilings\[kMaxFogVisionCircles\];\s*\};)"
    );
    for (const std::string &path : {kGlslFaceSelectPath, kMetalFaceSelectPath}) {
        EXPECT_TRUE(std::regex_search(readShaderSource(path), stageTail))
            << path << " must reach the ceilings through the fog passes' tail";
    }
    for (const char *path :
         {"/c_voxel_visibility_compact.glsl", "/metal/c_voxel_visibility_compact.metal"}) {
        const std::string compact = readShaderSource(std::string(IR_TEST_RENDER_SHADER_DIR) + path);
        ASSERT_FALSE(compact.empty()) << path;
        EXPECT_EQ(compact.find("visionCircleCeilings"), std::string::npos)
            << path << " reads no ceiling: its cull stays a z-free superset";
    }
}

// Test E, part 11: the ceiling scales each source before the maximum, in the
// shared reveal and in stage 1's unpainted-route drop; the treatment reads the
// product of the ceiling and line-of-sight factors, dissolves before the
// state update, and caps through the helper the radial cap also uses; the
// BODY apply stays untreated.
TEST(FogCrossSectionShaderParity, CeilingScalesEachSourceBeforeTheMaximumOnBothBackends) {
    for (const std::string &path : {kGlslFogCommonPath, kMetalFogCommonPath}) {
        SCOPED_TRACE(path);
        const std::string kernel = readShaderSource(path);
        const std::string loop = normalizeKernelMath(
            extractSpan(kernel, "fogRevealSample(", "float state = gridState;", "return FogReveal")
        );
        ASSERT_FALSE(loop.empty());
        for (const char *expected :
             {"ceilingVisibility = fogCeilingVisibility(visionCircleCeilings[i], dzUp);",
              "ceilingReveal = reveal * ceilingVisibility;",
              "contribution = losVisibility * ceilingReveal;",
              "surfaceVisibility = ceilingVisibility * losVisibility;",
              "if (hash01 > mix(1.0, surfaceVisibility, revealSurfaceTreatment.y)) { continue; }",
              "if (contribution > state) { state = contribution; styledBand = bandWeight; }"}) {
            EXPECT_NE(loop.find(expected), std::string::npos)
                << "missing `" << expected << "` in " << loop;
        }
        EXPECT_EQ(loop.find("state = max("), std::string::npos)
            << "the state update must carry the band of the winning contribution";
        const std::string apply =
            normalizeKernelMath(extractSpan(kernel, "fogApplyReveal(", "outColor", "return "));
        EXPECT_NE(
            apply.find(
                "fogCutCapBlend(outColor, sourceColor.rgb, kFogCutTone, reveal.state, capBlend)"
            ),
            std::string::npos
        ) << apply;
        EXPECT_NE(
            apply.find(
                "if (reveal.styledBand > 0.0) { outColor = fogCutCapBlend(outColor, "
                "sourceColor.rgb, revealSurfaceTreatment.z, reveal.state, reveal.styledBand); }"
            ),
            std::string::npos
        ) << apply;
        const std::string body = extractFunctionBody(kernel, "fogApplyBody");
        EXPECT_EQ(body.find("styledBand"), std::string::npos) << "a BODY pixel takes no treatment";
        EXPECT_EQ(body.find("revealSurfaceTreatment"), std::string::npos);
    }

    const std::string glslStage = readShaderSource(kGlslStage1BodyPath);
    const std::string metalStage = readShaderSource(kMetalStage1BodyPath);
    const std::string glslDrop = extractRevealAccumulation(glslStage, "float fogColumnRevealZ");
    const std::string metalDrop = extractRevealAccumulation(metalStage, "float fogColumnRevealZ");
    ASSERT_FALSE(glslDrop.empty()) << "fogColumnRevealZ accumulation not found in GLSL";
    ASSERT_FALSE(metalDrop.empty()) << "fogColumnRevealZ accumulation not found in MSL";
    EXPECT_EQ(normalizeShaderMath(glslDrop), normalizeShaderMath(metalDrop))
        << "the unpainted-route drop diverged between backends";
    EXPECT_NE(
        normalizeShaderMath(glslDrop).find("* fogCeilingVisibility(visionCircleCeilings[i], dzUp)"),
        std::string::npos
    ) << "the drop must scale each source by its ceiling inside the max: "
      << glslDrop;
}

TEST(FogCrossSectionShaderParity, CeilingShadowCasterPolicyAndBakeRoutesStayBackendSymmetric) {
    const std::string glsl = readShaderSource(kGlslFogShadowCasterPath);
    const std::string metal = readShaderSource(kMetalFogShadowCasterPath);
    const std::string glslBody = extractFunctionBody(glsl, "fogFieldCastsSunShadow");
    const std::string metalBody = extractFunctionBody(metal, "fogFieldCastsSunShadow");
    ASSERT_FALSE(glslBody.empty());
    ASSERT_FALSE(metalBody.empty());
    for (const char *policy : {"fogCeilingVisibility(", "return true", "return !covered"}) {
        EXPECT_NE(glslBody.find(policy), std::string::npos) << policy;
        EXPECT_NE(metalBody.find(policy), std::string::npos) << policy;
    }

    for (const char *stem :
         {"c_bake_voxel_sun_faces",
          "c_bake_box_sun_shadow",
          "c_bake_sun_shadow_map",
          "c_resolve_world_placed_depth",
          "c_shapes_to_trixel_body"}) {
        SCOPED_TRACE(stem);
        const std::string glslRoute =
            readShaderSource(std::string(IR_TEST_RENDER_SHADER_DIR) + "/" + stem + ".glsl");
        const std::string metalRoute =
            readShaderSource(std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/" + stem + ".metal");
        EXPECT_NE(glslRoute.find("fogFieldCastsSunShadow("), std::string::npos);
        EXPECT_NE(metalRoute.find("fogFieldCastsSunShadow("), std::string::npos);
    }
}

// Test E, part 7: the BODY branch of the fog pass — the pixel takes the
// carrier factor as its state and skips the field, the height terms, the rim
// fade and the cut cap — is identical on both backends.
TEST(FogCrossSectionShaderParity, FogPassBodyBranchIsIdenticalAcrossBackends) {
    const std::string glsl = readShaderSource(kGlslFogPassPath);
    const std::string metal = readShaderSource(kMetalFogPassPath);
    // The branch closes at the first 4-space-indented brace after its start;
    // its inner early return sits deeper.
    const std::string glslBody =
        extractSpan(glsl, "if (decodeFogBody(rawId))", "if (decodeFogBody(rawId))", "\n    }\n");
    const std::string metalBody =
        extractSpan(metal, "if (decodeFogBody(rawId))", "if (decodeFogBody(rawId))", "\n    }\n");
    ASSERT_FALSE(glslBody.empty()) << "BODY branch not found in " << kGlslFogPassPath;
    ASSERT_FALSE(metalBody.empty()) << "BODY branch not found in " << kMetalFogPassPath;
    // The colour image access is the one dialect difference left after
    // normalization (imageLoad / imageStore against read / write); both fold
    // onto READ / WRITE so the math around them compares.
    const std::string glslMath = std::regex_replace(
        std::regex_replace(
            normalizeKernelMath(glslBody),
            std::regex(R"(imageLoad\(trixelColors, pixel\))"),
            "READ"
        ),
        std::regex(R"(imageStore\(trixelColors, pixel, (.*)\);)"),
        "WRITE($1);"
    );
    const std::string metalMath = std::regex_replace(
        std::regex_replace(
            normalizeKernelMath(metalBody),
            std::regex(R"(trixelColors\.read\(uvec2\(pixel\)\))"),
            "READ"
        ),
        std::regex(R"(trixelColors\.write\((.*), uvec2\(pixel\)\);)"),
        "WRITE($1);"
    );
    EXPECT_EQ(glslMath, metalMath) << "the BODY branch diverged between backends";
    EXPECT_NE(glslBody.find("fogApplyBody(bodyState, "), std::string::npos)
        << "the BODY branch must shade through the shared BODY apply: " << glslBody;
    EXPECT_NE(glslBody.find("decodeFogBodyFactor(rawId)) / 255.0"), std::string::npos)
        << "the BODY state must be the carrier factor over 255: " << glslBody;
    EXPECT_EQ(glslBody.find("fogTap"), std::string::npos) << "a BODY pixel takes no grid tap";
    EXPECT_EQ(glslBody.find("visionCircle"), std::string::npos)
        << "a BODY pixel evaluates no circle";
}

// Test E, part 8: the entity-id carrier twins — the BODY bit + factor fold and
// their decodes — are identical on both backends, and stage 2 folds the voxel
// reserved word's class bit and factor field through them identically.
TEST(FogCrossSectionShaderParity, FogBodyCarrierTwinsAreIdenticalAcrossBackends) {
    const std::string glslIso = readShaderSource(kGlslIsoCommonPath);
    const std::string metalIso = readShaderSource(kMetalIsoCommonPath);
    for (const char *function :
         {"decodeFogBody",
          "decodeFogBodyFactor",
          "encodeEntityIdFogBody",
          "encodeEntityIdFogWholeBody",
          "encodeFogOverflowClassByte"}) {
        SCOPED_TRACE(function);
        const std::string glslBody = extractFunctionBody(glslIso, function);
        const std::string metalBody = extractFunctionBody(metalIso, function);
        ASSERT_FALSE(glslBody.empty()) << function << " missing from GLSL";
        ASSERT_FALSE(metalBody.empty()) << function << " missing from MSL";
        EXPECT_EQ(normalizeShaderMath(glslBody), normalizeShaderMath(metalBody))
            << function << " diverged between backends";
    }
    double glslShift = 0.0;
    double metalShift = 0.0;
    ASSERT_TRUE(readShaderConstant(glslIso, "kEntityIdFogBodyFactorShiftInHighWord", glslShift));
    ASSERT_TRUE(readShaderConstant(metalIso, "kEntityIdFogBodyFactorShiftInHighWord", metalShift));
    EXPECT_EQ(glslShift, 20.0);
    EXPECT_EQ(metalShift, 20.0);
    double glslFieldByte = 0.0;
    double metalFieldByte = 0.0;
    ASSERT_TRUE(readShaderConstant(glslIso, "kFogOverflowFieldByte", glslFieldByte));
    ASSERT_TRUE(readShaderConstant(metalIso, "kFogOverflowFieldByte", metalFieldByte));
    EXPECT_EQ(glslFieldByte, 255.0);
    EXPECT_EQ(metalFieldByte, 255.0);

    const std::string glslStage2 = readShaderSource(kGlslStage2BodyPath);
    const std::string metalStage2 = readShaderSource(kMetalStage2BodyPath);
    const std::string glslFold =
        extractSpan(glslStage2, "encodeEntityIdFogBody(", "encodeEntityIdFogBody(", ";");
    const std::string metalFold =
        extractSpan(metalStage2, "encodeEntityIdFogBody(", "encodeEntityIdFogBody(", ";");
    ASSERT_FALSE(glslFold.empty()) << "stage-2 fold not found in " << kGlslStage2BodyPath;
    ASSERT_FALSE(metalFold.empty()) << "stage-2 fold not found in " << kMetalStage2BodyPath;
    EXPECT_EQ(normalizeKernelMath(glslFold), normalizeKernelMath(metalFold))
        << "the stage-2 fold diverged between backends";
    EXPECT_NE(glslFold.find("reserved >> 4u) & 0xFFu"), std::string::npos)
        << "stage 2 must fold reserved bits 11:4 as the factor: " << glslFold;
}

TEST(FogCrossSectionShaderParity, ShapeRasterFoldsBodyClassAndFactorAcrossBackends) {
    const std::string glsl = readShaderSource(kGlslShapeBodyPath);
    const std::string metal = readShaderSource(kMetalShapeBodyPath);
    const std::string glslFold =
        extractSpan(glsl, "const uvec2 packedEntityId", "const uvec2 packedEntityId", ";");
    const std::string metalFold =
        extractSpan(metal, "const uint2 packedEntityId", "const uint2 packedEntityId", ";");

    ASSERT_FALSE(glslFold.empty()) << "shape carrier fold not found in " << kGlslShapeBodyPath;
    ASSERT_FALSE(metalFold.empty()) << "shape carrier fold not found in " << kMetalShapeBodyPath;
    EXPECT_EQ(normalizeShaderMath(glslFold), normalizeShaderMath(metalFold));
    EXPECT_NE(glslFold.find("encodeEntityIdFogBody"), std::string::npos);
    EXPECT_NE(glslFold.find("encodeEntityIdAnalyticSurface"), std::string::npos);
    EXPECT_NE(glslFold.find("(shape.flags >> 16u) & 0xFFu"), std::string::npos);
}

// Test E, part 9: the cut-face widening is gated off for a BODY voxel on
// both backends.
TEST(FogCrossSectionShaderParity, CutFaceRuleSkipsBodyVoxelsOnBothBackends) {
    const std::string glsl = readShaderSource(kGlslFaceSelectPath);
    const std::string metal = readShaderSource(kMetalFaceSelectPath);
    const std::string glslGate =
        extractSpan(glsl, "sel.isCutFace = false;", "if (!sel.keepFace", "{");
    const std::string metalGate =
        extractSpan(metal, "sel.isCutFace = false;", "if (!sel.keepFace", "{");
    ASSERT_FALSE(glslGate.empty()) << "cut-face gate not found in GLSL";
    ASSERT_FALSE(metalGate.empty()) << "cut-face gate not found in MSL";
    EXPECT_EQ(normalizeKernelMath(glslGate), normalizeKernelMath(metalGate))
        << "the cut-face gate diverged between backends";
    EXPECT_NE(glslGate.find("(reserved & 8u) == 0u"), std::string::npos)
        << "the cut rule must skip reserved bit 3 (kFogBody): " << glslGate;
}

// ---------------------------------------------------------------------------
// Tests A-D — headless GPU, OpenGL only.
// ---------------------------------------------------------------------------

#if defined(IR_GRAPHICS_OPENGL)

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <irreden/math/sdf.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/fog_line_of_sight.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/texture.hpp>

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace {

// Mirror of the probe kernel's own constants. A divergence leaves the tail of
// the readback at its seed value, which every test below would report as a
// wildly out-of-range reveal rather than passing silently.
constexpr int kProbeHalfExtent = 32;
constexpr int kProbeDim = kProbeHalfExtent * 2;
constexpr int kProbeColumnCount = kProbeDim * kProbeDim;
constexpr int kProbeLocalSize = 8;

// The probe's fog window: a small edge (the smallest the toroidal tap can
// exercise a wrap on), with the origin lanes set per run.
constexpr int kProbeWindowEdge = 256;
const IRMath::ivec2 kProbeDefaultWindowOrigin{-kProbeWindowEdge / 2, -kProbeWindowEdge / 2};
const IRMath::ivec2 kProbeDefaultOrigin{-kProbeHalfExtent, -kProbeHalfExtent};

constexpr std::uint32_t kBindingFogGridImage = 0;  // IR_VOXEL_FOG_GRID_BINDING in the probe
constexpr std::uint32_t kBindingProbeOut = 1;      // std430 binding in the probe
constexpr std::uint32_t kBindingProbeIn = 2;       // std430 binding in the probe
constexpr std::uint32_t kBindingFogObservers = 27; // std140 binding in ir_voxel_face_select.glsl

// The probe uploads the ENGINE's own observer struct rather than a hand-rolled
// mirror. `FrameDataFogObservers` is the per-frame upload source of truth the
// live fog system feeds the shader's `FogObserverData` block, so the probe
// consumes the clip through the same layout production does — a std140 drift
// that would break the real path breaks this test too, instead of a private
// mirror keeping it green.
using IRComponents::FrameDataFogObservers;

// One record per probed column, matching the probe kernel's std430 struct.
struct FogColumnProbe {
    float revealCenter;       // fogColumnReveal — the z-free cut-face test
    float revealNearest;      // fogColumnRevealNearest — the shipped KEEP metric
    float revealFloorCenter;  // FOG_TO_TRIXEL's curve at the column centre
    float revealFloorNearest; // …at the cell point nearest the circle
};
static_assert(sizeof(FogColumnProbe) == 16, "probe record must match the std430 struct");

// A column is kept by stage 1 iff its nearest-cell-point reveal is above zero
// (c_voxel_to_trixel_stage_1_body.glsl: `… <= 0.0` returns without emitting).
bool columnIsKept(const FogColumnProbe &probe) {
    return probe.revealNearest > 0.0f;
}

// Brings up a hidden OpenGL 4.5 core context and the probe's GPU resources,
// mirroring gpu_compute_dispatch_test.cpp's fixture (including its clean skip
// on display-less hosts, so the always-run CPU suite stays green there).
class FogCrossSectionTest : public ::testing::Test {
  protected:
    void SetUp() override {
        if (!glfwInit()) {
            GTEST_SKIP() << "glfwInit failed — no display / headless host without a GPU.";
        }
        m_glfwInitialized = true;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // headless: never shown
        m_window = glfwCreateWindow(16, 16, "ir-fog-cross-section-test", nullptr, nullptr);
        if (m_window == nullptr) {
            GTEST_SKIP() << "OpenGL 4.5 core context unavailable on this host.";
        }
        glfwMakeContextCurrent(m_window);

        using namespace IRRender;
        const std::string probePath =
            std::string(IR_TEST_GPU_SHADER_DIR) + "/c_fog_cross_section_probe.glsl";
        m_probeProgram = std::make_unique<ShaderProgram>(
            std::vector{ShaderStage{probePath.c_str(), ShaderType::COMPUTE}}
        );

        // Fog grid: all zeros == UNEXPLORED everywhere, so no column takes the
        // `>= kFogExploredThreshold` grid-memory short-circuit and every probed
        // reveal is the analytic one under test. A column outside the window
        // reads unexplored too, so the default window placement changes no
        // analytic result; the window-tap test moves it deliberately.
        m_fogGrid = std::make_unique<Texture2D>(
            TextureKind::TEXTURE_2D,
            kProbeWindowEdge,
            kProbeWindowEdge,
            TextureFormat::RG32UI
        );
        const std::vector<IRPrefab::Fog::FogWindowTexel> unexplored(
            static_cast<std::size_t>(kProbeWindowEdge) * kProbeWindowEdge
        );
        m_fogGrid->subImage2D(
            0,
            0,
            kProbeWindowEdge,
            kProbeWindowEdge,
            PixelDataFormat::RG_INTEGER,
            PixelDataType::UINT32,
            unexplored.data()
        );

        const FrameDataFogObservers seedObservers{};
        m_observers = std::make_unique<Buffer>(
            &seedObservers,
            sizeof(FrameDataFogObservers),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBindingFogObservers
        );

        const IRMath::ivec2 seedProbeOrigin = kProbeDefaultOrigin;
        m_probeIn = std::make_unique<Buffer>(
            &seedProbeOrigin,
            sizeof(seedProbeOrigin),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBindingProbeIn
        );

        // Seed the output with a value no reveal can take, so a probe record the
        // dispatch never wrote is unmistakable rather than reading as 0.0
        // ("fully hidden") and quietly satisfying half the assertions.
        const std::vector<FogColumnProbe> seedProbes(kProbeColumnCount, kUnwrittenProbe);
        m_probeOut = std::make_unique<Buffer>(
            seedProbes.data(),
            seedProbes.size() * sizeof(FogColumnProbe),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBindingProbeOut
        );
    }

    void TearDown() override {
        // GPU resources release through the context, so they must go first.
        m_probeOut.reset();
        m_probeIn.reset();
        m_observers.reset();
        m_fogGrid.reset();
        m_probeProgram.reset();
        if (m_window != nullptr) {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
        if (m_glfwInitialized) {
            glfwTerminate();
            m_glfwInitialized = false;
        }
    }

    // Uploads one vision circle (centerX, centerY, radius, edgeSoftness) with
    // all-zero height penalties — which is what keeps the z-free curves this
    // probe reads bit-identical to the stage body's unpainted-route Z twin —
    // plus the window origin lanes, then dispatches over the column domain
    // starting at @p probeOrigin and reads the records back. A zero-radius
    // circle registers no source, so the grid alone decides each reveal.
    std::vector<FogColumnProbe> runProbe(
        IRMath::vec4 circle,
        IRMath::ivec2 windowOrigin = kProbeDefaultWindowOrigin,
        IRMath::ivec2 probeOrigin = kProbeDefaultOrigin
    ) {
        using namespace IRRender;

        FrameDataFogObservers observers{};
        observers.visionCircles_[0] = circle;
        observers.visionCircleCount_ = circle.z > 0.0f ? 1 : 0;
        observers.windowOriginX_ = windowOrigin.x;
        observers.windowOriginY_ = windowOrigin.y;
        m_observers->subData(0, sizeof(FrameDataFogObservers), &observers);
        m_probeIn->subData(0, sizeof(probeOrigin), &probeOrigin);

        const std::vector<FogColumnProbe> seedProbes(kProbeColumnCount, kUnwrittenProbe);
        m_probeOut->subData(0, seedProbes.size() * sizeof(FogColumnProbe), seedProbes.data());

        m_probeProgram->use();
        m_fogGrid
            ->bindAsImage(kBindingFogGridImage, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
        m_observers->bindBase(BufferTarget::UNIFORM, kBindingFogObservers);
        m_probeIn->bindBase(BufferTarget::SHADER_STORAGE, kBindingProbeIn);
        m_probeOut->bindBase(BufferTarget::SHADER_STORAGE, kBindingProbeOut);

        const int groups = kProbeDim / kProbeLocalSize;
        ENG_API->glDispatchCompute(groups, groups, 1);
        ENG_API->glMemoryBarrier(GL_ALL_BARRIER_BITS);
        ENG_API->glFinish(); // a test, not a hot path — block for the readback

        std::vector<FogColumnProbe> readback(kProbeColumnCount, kUnwrittenProbe);
        m_probeOut->getSubData(0, readback.size() * sizeof(FogColumnProbe), readback.data());
        return readback;
    }

    // Writes one grid texel: world column @p column's state at its toroidal
    // address `floorMod(column, edge)`.
    void writeGridTexel(IRMath::ivec2 column, std::uint8_t state) {
        using namespace IRRender;
        const IRMath::ivec2 texel = IRPrefab::Fog::detail::windowTexel(column, kProbeWindowEdge);
        const IRPrefab::Fog::FogWindowTexel value{state, IRComponents::kFogChannelDefault};
        m_fogGrid->subImage2D(
            texel.x,
            texel.y,
            1,
            1,
            PixelDataFormat::RG_INTEGER,
            PixelDataType::UINT32,
            &value
        );
    }

    static int record(int localX, int localY) {
        return localY * kProbeDim + localX;
    }

    static int columnX(int record) {
        return record % kProbeDim - kProbeHalfExtent;
    }

    static int columnY(int record) {
        return record / kProbeDim - kProbeHalfExtent;
    }

    static IRMath::vec2 columnCentre(int record) {
        return IRMath::vec2(
            static_cast<float>(columnX(record)),
            static_cast<float>(columnY(record))
        );
    }

    // The point of this column's unit cell nearest the circle centre — the same
    // clamp `fogColumnRevealNearest` performs, so the CPU-side geometry the
    // tests reason about matches the metric the shader keys on.
    static IRMath::vec2 nearestCellPoint(int record, IRMath::vec2 centre) {
        const IRMath::vec2 column = columnCentre(record);
        const IRMath::vec2 half{kFogColumnCellHalf};
        return IRMath::clamp(centre, column - half, column + half);
    }

    static float nearestCellDistance(int record, IRMath::vec2 centre) {
        return IRMath::length(nearestCellPoint(record, centre) - centre);
    }

    static float columnCentreDistance(int record, IRMath::vec2 centre) {
        return IRMath::length(columnCentre(record) - centre);
    }

    static constexpr FogColumnProbe kUnwrittenProbe{-1.0f, -1.0f, -1.0f, -1.0f};

    GLFWwindow *m_window = nullptr;
    bool m_glfwInitialized = false;
    std::unique_ptr<IRRender::ShaderProgram> m_probeProgram;
    std::unique_ptr<IRRender::Texture2D> m_fogGrid;
    std::unique_ptr<IRRender::Buffer> m_observers;
    std::unique_ptr<IRRender::Buffer> m_probeIn;
    std::unique_ptr<IRRender::Buffer> m_probeOut;
};

// The grid tap indexes the toroidal window at the origin the lanes carry: with
// the lanes at (1024, -3072) and one explored texel written at
// floorMod(col, 256) for col = (1100, -2950), the probe's tap at that column
// reads it; the column one window width over is outside the window and reads
// unexplored, not visible; and the same column under the legacy origin lanes
// is outside the window (the control that fails on a `col + 128` tap, which
// would read the texel at (1100 + 128, -2950 + 128) instead).
TEST_F(FogCrossSectionTest, GridTapFollowsTheWindowOrigin) {
    const IRMath::ivec2 lanes{1024, -3072};
    const IRMath::ivec2 column{1100, -2950};
    const IRMath::vec4 noCircle{0.0f, 0.0f, 0.0f, 0.0f};
    writeGridTexel(column, IRComponents::kFogStateVisible);

    const IRMath::ivec2 probeAt = column - IRMath::ivec2(kProbeHalfExtent);
    const std::vector<FogColumnProbe> probes = runProbe(noCircle, lanes, probeAt);
    EXPECT_FLOAT_EQ(probes[record(kProbeHalfExtent, kProbeHalfExtent)].revealCenter, 1.0f)
        << "the written texel was not read back at its column";
    EXPECT_FLOAT_EQ(probes[record(kProbeHalfExtent, kProbeHalfExtent)].revealNearest, 1.0f);
    EXPECT_FLOAT_EQ(probes[record(kProbeHalfExtent + 1, kProbeHalfExtent)].revealCenter, 0.0f)
        << "the neighbouring column is unexplored";
    int explored = 0;
    for (const FogColumnProbe &probe : probes) {
        explored += probe.revealCenter > 0.0f ? 1 : 0;
    }
    EXPECT_EQ(explored, 1) << "exactly one column of the domain is explored";

    const IRMath::ivec2 outside = column + IRMath::ivec2(kProbeWindowEdge, 0);
    const std::vector<FogColumnProbe> wrapped =
        runProbe(noCircle, lanes, outside - IRMath::ivec2(kProbeHalfExtent));
    EXPECT_FLOAT_EQ(wrapped[record(kProbeHalfExtent, kProbeHalfExtent)].revealCenter, 0.0f)
        << "a column one window width over shares the texel but is outside the window";

    const std::vector<FogColumnProbe> control =
        runProbe(noCircle, kProbeDefaultWindowOrigin, probeAt);
    EXPECT_FLOAT_EQ(control[record(kProbeHalfExtent, kProbeHalfExtent)].revealCenter, 0.0f)
        << "under the legacy lanes the column is outside the window";
    for (const FogColumnProbe &probe : control) {
        EXPECT_FLOAT_EQ(probe.revealCenter, 0.0f);
    }
}

// A hard disc (edgeSoftness 0) placed off-lattice so the rim crosses cells at
// every angle rather than landing on cell centres. Radius 10 keeps the whole
// keep-ring (radius + kFogColumnKeepAa + kFogHiddenKeepCells = 19) inside the
// 32-column probe half-extent, so both the kept and dropped populations are
// represented.
constexpr float kDiscRadius = 10.0f;
const IRMath::vec2 kDiscCentre{0.37f, -0.61f};
const IRMath::vec4 kHardDisc{kDiscCentre.x, kDiscCentre.y, kDiscRadius, 0.0f};

} // namespace

// Test A — no boundary black. The property that makes a hard-blacked object
// boundary unrepresentable is that stage 1 never drops a column the disc
// reaches: any column whose unit cell overlaps the reveal region is KEPT, so
// its footprint rasters in full and FOG_TO_TRIXEL trims it per pixel on the
// analytic edge. Under a centre-only clip the object instead ended
// on the voxel lattice and the faces past it were hard-blacked.
TEST_F(FogCrossSectionTest, PartiallyRevealedColumnsAreNeverDropped) {
    const std::vector<FogColumnProbe> probes = runProbe(kHardDisc);

    int reachedColumns = 0;
    int droppedReached = 0;
    for (int record = 0; record < kProbeColumnCount; ++record) {
        const FogColumnProbe &probe = probes[record];
        ASSERT_GE(probe.revealNearest, 0.0f)
            << "probe record " << record << " was never written by the dispatch";
        if (probe.revealFloorNearest <= 0.0f) {
            continue; // the disc does not reach any point of this column's cell
        }
        ++reachedColumns;
        if (!columnIsKept(probe)) {
            ++droppedReached;
        }
    }

    ASSERT_GT(reachedColumns, 0) << "probe domain contains no partially revealed columns";
    EXPECT_EQ(droppedReached, 0)
        << droppedReached << " of " << reachedColumns
        << " columns the disc reaches were dropped by the stage-1 clip — their faces would "
           "reach FOG_TO_TRIXEL missing and the object would end on the voxel lattice (#2102).";
}

// Test A, non-vacuity control. The assertion above is only meaningful if the
// centre-only clip would actually fail it: a centre-evaluated test
// (`fogColumnReveal <= 0`) DOES drop columns the disc reaches. Without this,
// `PartiallyRevealedColumnsAreNeverDropped` would pass against a clip that
// simply never drops anything.
TEST_F(FogCrossSectionTest, CentreOnlyClipWouldDropReachedColumns) {
    const std::vector<FogColumnProbe> probes = runProbe(kHardDisc);

    int reachedButCentreHidden = 0;
    for (const FogColumnProbe &probe : probes) {
        if (probe.revealFloorNearest > 0.0f && probe.revealCenter <= 0.0f) {
            ++reachedButCentreHidden;
        }
    }
    EXPECT_GT(reachedButCentreHidden, 0)
        << "no column is reached-but-centre-hidden, so Test A cannot distinguish the shipped "
           "nearest-cell clip from the pre-#2102 centre-only one";
}

// Test B — smooth partial coverage, no pop. Sweeping the observer across a
// column must move that column's opacity continuously; a 0 -> full jump is the
// visible "pop" the analytic reveal exists to remove. Run on a SOFT disc, which
// is the Mode B path where the curve itself carries the softening. (A hard disc
// gets its per-pixel continuity from FOG_TO_TRIXEL's `aa = max(softness,
// worldPerPixel)` floor instead, which this probe deliberately samples at
// aa = 0 and therefore cannot see.)
TEST_F(FogCrossSectionTest, RevealIsContinuousUnderAnObserverSweep) {
    constexpr float kSoftness = 2.0f;
    constexpr float kStep = 0.05f;
    constexpr int kSteps = 81; // ±2 world units of observer travel, in 0.05 steps
    // The probed column sits ON the rim when the observer is at the origin
    // (kDiscRadius away along +x), so sliding the observer along x sweeps this
    // column's radial distance through [R - 2, R + 2] — the full softening band
    // of a softness-2 disc, hidden side to revealed side.
    constexpr int kProbedColumnX = static_cast<int>(kDiscRadius);
    constexpr int kProbedColumnY = 0;
    // The reveal curve's steepest slope is 1.5 / (2 * softness) per world unit,
    // so a kStep sweep can move it by at most ~0.019 — an order of magnitude
    // under this bar, which is set to catch a POP, not to pin the exact curve.
    constexpr float kMaxStepDelta = 0.15f;

    const int record =
        (kProbedColumnY + kProbeHalfExtent) * kProbeDim + (kProbedColumnX + kProbeHalfExtent);

    float previous = -1.0f;
    float minimum = 2.0f;
    float maximum = -1.0f;
    for (int step = 0; step < kSteps; ++step) {
        // Slide the observer along +x toward the probed column; radius is fixed,
        // only the centre moves, so the rim crosses the column mid-sweep.
        const float offset = static_cast<float>(step - kSteps / 2) * kStep;
        const std::vector<FogColumnProbe> probes = runProbe(
            IRMath::vec4(offset, static_cast<float>(kProbedColumnY), kDiscRadius, kSoftness)
        );
        const float reveal = probes[record].revealFloorCenter;

        if (step > 0) {
            EXPECT_LE(IRMath::abs(reveal - previous), kMaxStepDelta)
                << "reveal popped from " << previous << " to " << reveal << " at sweep step "
                << step << " — partial coverage is discontinuous";
        }
        previous = reveal;
        minimum = reveal < minimum ? reveal : minimum;
        maximum = reveal > maximum ? reveal : maximum;
    }

    // Non-vacuity: the sweep must actually traverse the rim, or "continuous"
    // would be satisfied by a curve that never moved.
    EXPECT_LT(minimum, 0.2f) << "sweep never reached the hidden side of the rim";
    EXPECT_GT(maximum, 0.8f) << "sweep never reached the revealed side of the rim";
}

// Test C — no top-face gaps. A floor of columns straddling the disc must be
// revealed as a solid region, not a region with interior holes: the kept set is
// a radial threshold on the nearest-cell distance, so no dropped column can sit
// closer to the observer than a kept one. An interior hole is exactly that
// inversion.
TEST_F(FogCrossSectionTest, KeptColumnsFormAHoleFreeRadialRegion) {
    const std::vector<FogColumnProbe> probes = runProbe(kHardDisc);

    float farthestKept = -1.0f;
    float nearestDropped = std::numeric_limits<float>::max();
    int keptCount = 0;
    int droppedCount = 0;
    for (int record = 0; record < kProbeColumnCount; ++record) {
        const float distance = nearestCellDistance(record, kDiscCentre);
        if (columnIsKept(probes[record])) {
            ++keptCount;
            farthestKept = distance > farthestKept ? distance : farthestKept;
        } else {
            ++droppedCount;
            nearestDropped = distance < nearestDropped ? distance : nearestDropped;
        }
    }

    ASSERT_GT(keptCount, 0) << "no column kept — the probe domain misses the disc";
    ASSERT_GT(droppedCount, 0) << "no column dropped — the probe domain is entirely inside the "
                                  "keep ring, so the ordering below is vacuous";
    EXPECT_LE(farthestKept, nearestDropped)
        << "a dropped column (nearest-cell distance " << nearestDropped
        << ") sits closer to the observer than a kept one (" << farthestKept
        << ") — the revealed region has interior holes";
}

// Test D — floor/object edge agreement. On a hard disc the object clip's strict
// step and FOG_TO_TRIXEL's curve sampled at aa = 0 are the same threshold at the
// same radius; the off-lattice disc puts no column exactly on the rim, where
// only the strict step is defined. Asserted in its exact form (equality of the
// evaluations), which is stronger than the ±1px tolerance the property is
// usually stated with.
TEST_F(FogCrossSectionTest, ObjectClipAndFloorRevealTraceOneCurve) {
    const std::vector<FogColumnProbe> probes = runProbe(kHardDisc);

    for (int record = 0; record < kProbeColumnCount; ++record) {
        ASSERT_FLOAT_EQ(probes[record].revealCenter, probes[record].revealFloorCenter)
            << "the object clip and the floor reveal disagree at column (" << columnX(record)
            << ", " << columnY(record) << ") — they are no longer the same analytic curve";
    }
}

// Test D, the edge's position: `reveal >= 0.5` is exactly `inside the radius`,
// independent of the softening width (smoothstep is 0.5 at its midpoint). That
// is what pins the floor edge and the object edge to the SAME world circle
// rather than merely to the same formula evaluated at different radii.
TEST_F(FogCrossSectionTest, RevealMidpointSitsOnTheCircleRadius) {
    for (const float softness : {0.0f, 2.0f}) {
        const std::vector<FogColumnProbe> probes =
            runProbe(IRMath::vec4(kDiscCentre.x, kDiscCentre.y, kDiscRadius, softness));
        for (int record = 0; record < kProbeColumnCount; ++record) {
            const float distance = columnCentreDistance(record, kDiscCentre);
            if (IRMath::abs(distance - kDiscRadius) < 1e-3f) {
                continue; // on the midpoint itself; float ties are not a property
            }
            const bool insideRadius = distance < kDiscRadius;
            EXPECT_EQ(probes[record].revealFloorCenter >= 0.5f, insideRadius)
                << "reveal midpoint left the radius at distance " << distance << " (softness "
                << softness << ")";
        }
    }
}

// Test E, part 4: the GL dispatch agrees with the CPU mirror of the same
// formula. Together with the source-parity tests above — which pin the MSL
// twin to the same expression — this is what a GL-only host can assert about
// the Metal backend it cannot dispatch.
TEST_F(FogCrossSectionTest, GpuRevealMatchesTheCpuOracle) {
    const std::vector<FogColumnProbe> probes = runProbe(kHardDisc);

    for (int record = 0; record < kProbeColumnCount; ++record) {
        const IRMath::vec2 column = columnCentre(record);
        EXPECT_NEAR(
            probes[record].revealCenter,
            discRevealAtDistance(IRMath::length(column - kDiscCentre), kHardDisc.z, kHardDisc.w),
            1e-5f
        ) << "GPU own-column reveal diverged from the CPU oracle at ("
          << column.x << ", " << column.y << ")";

        EXPECT_NEAR(
            probes[record].revealNearest,
            visionCircleReveal(
                nearestCellPoint(record, kDiscCentre),
                kHardDisc,
                kFogColumnKeepAa + kFogHiddenKeepCells
            ),
            1e-5f
        ) << "GPU keep metric diverged from the CPU oracle at ("
          << column.x << ", " << column.y << ")";
    }
}

// The hard-disc rim tie. An integer-centred disc of integer radius 5 puts twelve
// columns exactly on the rim — (±5,0), (0,±5), (±3,±4), (±4,±3) — and the strict
// rule hides every one of them. smoothstep(R, R, d) would leave the tie to the
// driver — NVIDIA keeps the column where Metal hides it — so a detached solid
// straddling the rim would stand a stray column past its fog cut wall on one
// backend only. The expectation is exact integer arithmetic: each tie's squared
// length is a perfect square, so `length` returns the radius itself.
TEST_F(FogCrossSectionTest, HardDiscHidesColumnsExactlyOnTheRim) {
    constexpr int kTieRadius = 5;
    constexpr int kTieRadiusSquared = kTieRadius * kTieRadius;
    const std::vector<FogColumnProbe> probes =
        runProbe(IRMath::vec4(0.0f, 0.0f, static_cast<float>(kTieRadius), 0.0f));

    int rimColumns = 0;
    for (int record = 0; record < kProbeColumnCount; ++record) {
        const int x = columnX(record);
        const int y = columnY(record);
        const int distanceSquared = x * x + y * y;
        if (distanceSquared == kTieRadiusSquared) {
            ++rimColumns;
        }
        const float expected = distanceSquared < kTieRadiusSquared ? 1.0f : 0.0f;
        EXPECT_EQ(probes[record].revealCenter, expected)
            << "column (" << x << ", " << y << ") at squared distance " << distanceSquared
            << " from a radius-" << kTieRadius << " hard disc";
    }
    EXPECT_EQ(rimColumns, 12) << "the probe domain lost the rim tie columns";
}

namespace {

constexpr std::uint32_t kBindingLosTexture = 0;  // IR_FOG_LOS_BINDING in the probe
constexpr std::uint32_t kBindingLosProbeOut = 1; // std430 binding in the probe
constexpr std::uint32_t kBindingLosProbeIn = 2;  // std430 binding in the probe
constexpr int kLosProbeLocalSize = 64;           // local_size_x in the probe

// The fixture: flat ground (top plane 4), the fog_demo ridge as the subdivided
// raster draws it (x -0.5..1.5, y -7.5..8.5, top plane -0.5) and a
// free-standing flagged SDF pillar, seen from a soft-edged source with its eye
// 2 above its observer: on the ground at (-6, 0), or on the ridge top at
// (0.5, 0). The ground eye sits below every occluder top, so it has no far
// shadow edge for softness to grade; the ridge eye does. Each arm runs with
// the fog window centred on the scene: at the world origin, or with the
// whole scene translated by kLosFarShift, where a world-centred field would
// hold none of it.
constexpr float kLosGround = 4.0f;
constexpr float kLosRidgeTop = -0.5f;
constexpr float kLosEyeHeight = 2.0f;

struct LosProbeSource {
    IRMath::vec4 circle_;
    float observerZ_;
};
const LosProbeSource kLosGroundSource{IRMath::vec4(-6.0f, 0.0f, 14.0f, 2.0f), 4.5f};
const LosProbeSource kLosRidgeSource{IRMath::vec4(0.5f, 0.0f, 14.0f, 2.0f), kLosRidgeTop};

struct LosProbeCounts {
    int occludedInDisc_ = 0;
    int visibleInDisc_ = 0;
    int partialInDisc_ = 0;
    int faceVisible_ = 0;
    int faceHidden_ = 0;
};
constexpr int kLosSubdivisions = 8;
// The probed sample heights above the ground plane.
constexpr float kLosLevelLift[] = {0.0f, 2.0f, 6.0f};
// A window edge the 1280x720 canvas yields, and a scene translation that puts
// the source and the ridge several field half extents from the world origin.
constexpr int kLosWindowEdge = 1152;
const IRMath::ivec2 kLosFarShift(640, -392);

struct FogLosProbeHeader {
    IRMath::vec4 circle_;
    IRMath::vec4 source_;
    std::int32_t losSourceMask_;
    std::int32_t sampleCount_;
    std::int32_t windowOriginX_;
    std::int32_t windowOriginY_;
    std::int32_t windowEdge_;
    std::int32_t pad_[3];
};
static_assert(sizeof(FogLosProbeHeader) == 64, "must match the probe's std430 header");

struct FogLosProbeRecord {
    float visibility_;
    float reveal_;
    float clearance_;
    float bandClearance_;
    IRMath::vec4 canonical_;
};
static_assert(sizeof(FogLosProbeRecord) == 32, "must match the probe's std430 struct");

// The fixture's column field at lower corner @p fieldMin, with every occluder
// translated by @p shift.
std::vector<float> losProbeField(bool withOccluders, IRMath::ivec2 shift, IRMath::ivec2 fieldMin) {
    std::vector<float> field(IRComponents::kFogLosFieldFloatCount, kLosGround);
    const IRMath::vec3 offset(static_cast<float>(shift.x), static_cast<float>(shift.y), 0.0f);
    if (!withOccluders) {
        IRPrefab::Fog::buildLosPyramid(field);
        return field;
    }
    IRPrefab::Fog::LosRasterFrame frame;
    frame.subdivisions_ = kLosSubdivisions;
    for (int row = -7; row <= 8; ++row) {
        for (const float x : {-0.5f, 0.5f}) {
            IRMath::vec3 boxMin;
            IRMath::vec3 boxMax;
            IRPrefab::Fog::losVoxelBox(
                IRMath::vec3(x, static_cast<float>(row) - 0.5f, kLosRidgeTop) + offset,
                frame,
                boxMin,
                boxMax
            );
            IRPrefab::Fog::stampLosBox(
                field,
                fieldMin,
                IRMath::vec2(boxMin),
                IRMath::vec2(boxMax),
                boxMin.z
            );
        }
    }
    IRComponents::C_ShapeDescriptor pillar{
        IRMath::SDF::ShapeType::BOX,
        IRMath::vec4(3.0f, 3.0f, 10.0f, 0.0f),
        IRMath::Color{255, 255, 255, 255}
    };
    IRPrefab::Fog::stampLosShape(
        field,
        fieldMin,
        pillar,
        IRMath::vec3(-8.0f, 4.0f, -1.0f) + offset,
        IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f),
        frame
    );
    IRPrefab::Fog::buildLosPyramid(field);
    return field;
}

// Uploads a packed column field into the probe's line-of-sight image.
void uploadLosField(IRRender::Texture2D &texture, const std::vector<float> &field) {
    texture.subImage2D(
        0,
        0,
        IRComponents::kFogLosTextureSize,
        IRComponents::kFogLosTextureHeight,
        IRRender::PixelDataFormat::RGBA,
        IRRender::PixelDataType::FLOAT32,
        field.data()
    );
}

} // namespace

// Same rule, both sides: the GPU gate (the real ir_fog_los.glsl) over the
// production-built column field agrees with the CPU march on every probe —
// the hard verdicts exactly, the soft factors and the gated reveal to 1e-3
// — at fractional positions on both sides of every boundary. Non-vacuity:
// the fixture has occluded, visible and (under softness) partial in-disc
// probes; without the ridge and the pillar nothing is occluded. The face arm
// hands the probe raster-recovered face pixels and checks the canonical
// sample lands on the face plane the CPU raster stamped. Softness grades only
// a far shadow edge, so the ground eye stays a step under it and the ridge-top
// eye produces the partial samples. Every arm also runs with the scene and the
// window translated off the world origin, where the field follows the window.
TEST_F(FogCrossSectionTest, GpuOcclusionMatchesTheCpuOracle) {
    using namespace IRRender;
    using IRComponents::C_CanvasFogOfWar;
    using IRComponents::FogLosColumnField;

    const std::string probePath = std::string(IR_TEST_GPU_SHADER_DIR) + "/c_fog_los_probe.glsl";
    ShaderProgram program{std::vector{ShaderStage{probePath.c_str(), ShaderType::COMPUTE}}};
    Texture2D losTexture{
        TextureKind::TEXTURE_2D,
        IRComponents::kFogLosTextureSize,
        IRComponents::kFogLosTextureHeight,
        TextureFormat::RGBA32F
    };

    const auto runOcclusionProbe = [&](const LosProbeSource &source,
                                       IRMath::ivec2 shift,
                                       bool withOccluders,
                                       float softness,
                                       LosProbeCounts &counts) {
        counts = LosProbeCounts{};
        const IRMath::vec2 offset(static_cast<float>(shift.x), static_cast<float>(shift.y));
        const IRMath::vec4 circle = source.circle_ + IRMath::vec4(offset.x, offset.y, 0.0f, 0.0f);
        FrameDataFogObservers observers{};
        const int slot = C_CanvasFogOfWar::addVisionCircle(
            observers,
            circle.x,
            circle.y,
            circle.z,
            circle.w,
            source.observerZ_,
            0.0f,
            0.0f,
            0.0f
        );
        ASSERT_EQ(slot, 0);
        C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, slot, kLosEyeHeight, softness);

        // The window the fog pass would show over this scene, and the field
        // anchored with it.
        const IRMath::ivec2 windowOrigin =
            IRPrefab::Fog::detail::windowOriginForCentre(offset, kLosWindowEdge);
        const IRMath::ivec2 fieldMin =
            FogLosColumnField::fieldMinForWindow(windowOrigin, kLosWindowEdge);
        const std::vector<float> field = losProbeField(withOccluders, shift, fieldMin);
        uploadLosField(losTexture, field);
        const FogLosColumnField columns{field.data(), fieldMin};

        // Point samples at fractional positions on and above the ground, plus
        // raster-style face pixels: the ridge's -X face and top face with the
        // cardinal raster's in-depth-plane offsets applied. A point inside a
        // column is placed on its top plane, where the oracle evaluates it; the
        // shader only ever marches to surfaces.
        std::vector<IRMath::vec4> samples;
        for (const float lift : kLosLevelLift) {
            for (int y = -32; y < 32; ++y) {
                for (int x = -32; x < 32; ++x) {
                    for (const IRMath::vec2 frac :
                         {IRMath::vec2(0.25f, 0.5f), IRMath::vec2(0.75f, 0.125f)}) {
                        const IRMath::vec2 xy =
                            IRMath::vec2(static_cast<float>(x), static_cast<float>(y)) + frac +
                            offset;
                        const float ownTop = columns.topPlane(
                            FogLosColumnField::halfCellOf(xy.x),
                            FogLosColumnField::halfCellOf(xy.y)
                        );
                        samples.emplace_back(
                            xy.x,
                            xy.y,
                            IRMath::min(kLosGround - lift, ownTop),
                            -1.0f
                        );
                    }
                }
            }
        }
        const std::size_t pointCount = samples.size();
        constexpr float kMicro = 1.0f / static_cast<float>(kLosSubdivisions);
        for (int row = -7; row <= 8; ++row) {
            for (int u = 0; u < kLosSubdivisions; ++u) {
                const float y =
                    -7.5f + static_cast<float>(row + 7) + static_cast<float>(u) * kMicro + offset.y;
                for (int v = 0; v < 4 * kLosSubdivisions; ++v) {
                    const float z = kLosRidgeTop + static_cast<float>(v) * kMicro;
                    samples.emplace_back(
                        -0.5f - 2.0f / 3.0f * kMicro + offset.x,
                        y + kMicro / 3.0f,
                        z + kMicro / 3.0f,
                        0.0f
                    );
                }
                for (int v = 0; v < 2 * kLosSubdivisions; ++v) {
                    const float x = -0.5f + static_cast<float>(v) * kMicro + offset.x;
                    samples.emplace_back(x, y, kLosRidgeTop, 4.0f);
                }
            }
        }

        const FogLosProbeHeader header{
            circle,
            IRMath::vec4(
                source.observerZ_,
                kLosEyeHeight,
                softness,
                static_cast<float>(kLosSubdivisions)
            ),
            observers.losSourceMask_,
            static_cast<std::int32_t>(samples.size()),
            windowOrigin.x,
            windowOrigin.y,
            kLosWindowEdge,
            {0, 0, 0}
        };
        std::vector<std::uint8_t> input(sizeof(header) + samples.size() * sizeof(IRMath::vec4));
        std::memcpy(input.data(), &header, sizeof(header));
        std::memcpy(
            input.data() + sizeof(header),
            samples.data(),
            samples.size() * sizeof(IRMath::vec4)
        );
        Buffer probeIn{
            input.data(),
            input.size(),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBindingLosProbeIn
        };
        const FogLosProbeRecord unwritten{-1.0f, -1.0f, -1.0f, -1.0f, IRMath::vec4(-1.0f)};
        const std::vector<FogLosProbeRecord> seed(samples.size(), unwritten);
        Buffer probeOut{
            seed.data(),
            seed.size() * sizeof(FogLosProbeRecord),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBindingLosProbeOut
        };

        program.use();
        losTexture
            .bindAsImage(kBindingLosTexture, TextureAccess::READ_ONLY, TextureFormat::RGBA32F);
        probeIn.bindBase(BufferTarget::SHADER_STORAGE, kBindingLosProbeIn);
        probeOut.bindBase(BufferTarget::SHADER_STORAGE, kBindingLosProbeOut);
        const int groups = IRMath::divCeil(static_cast<int>(samples.size()), kLosProbeLocalSize);
        ENG_API->glDispatchCompute(groups, 1, 1);
        ENG_API->glMemoryBarrier(GL_ALL_BARRIER_BITS);
        ENG_API->glFinish();

        std::vector<FogLosProbeRecord> readback(samples.size(), unwritten);
        probeOut.getSubData(0, readback.size() * sizeof(FogLosProbeRecord), readback.data());

        for (std::size_t i = 0; i < pointCount; ++i) {
            const IRMath::vec3 position(samples[i]);
            const float cpuVisibility =
                IRPrefab::Fog::losVisibility(columns, observers, 0, position);
            ASSERT_NEAR(readback[i].visibility_, cpuVisibility, 1e-3f)
                << "GPU and CPU line-of-sight gates disagree at (" << position.x << ", "
                << position.y << ", " << position.z << ")";
            EXPECT_NEAR(
                readback[i].reveal_,
                IRPrefab::Fog::evalVisionReveal(observers, columns, position),
                1e-3f
            ) << "GPU gated reveal diverged from the oracle at ("
              << position.x << ", " << position.y << ", " << position.z << ")";
            if (IRPrefab::Fog::evalVisionReveal(observers, position) > 0.0f) {
                if (cpuVisibility <= 0.0f) {
                    ++counts.occludedInDisc_;
                } else if (cpuVisibility >= 1.0f) {
                    ++counts.visibleInDisc_;
                } else {
                    ++counts.partialInDisc_;
                }
            }
        }
        for (std::size_t i = pointCount; i < samples.size(); ++i) {
            const IRMath::vec4 sample = samples[i];
            const IRMath::vec3 canonical(readback[i].canonical_);
            if (sample.w == 0.0f) {
                EXPECT_NEAR(
                    canonical.x,
                    -0.5f - IRComponents::kFogLosClearanceTolerance * 20.0f + offset.x,
                    0.03f
                ) << "an -X face pixel snaps onto the face plane and steps out of it";
            } else {
                EXPECT_NEAR(canonical.z, kLosRidgeTop - 0.02f, 1e-4f)
                    << "a top face pixel rests a hair above its plane";
            }
            const float cpuVisibility =
                IRPrefab::Fog::losVisibility(columns, observers, 0, canonical);
            ASSERT_NEAR(readback[i].visibility_, cpuVisibility, 1e-3f)
                << "the face arm diverged at (" << sample.x << ", " << sample.y << ", " << sample.z
                << ")";
            ++(cpuVisibility > 0.0f ? counts.faceVisible_ : counts.faceHidden_);
        }
    };

    // The far arm is the window re-anchor's check: its source and ridge lie
    // outside a world-centred field, so without the anchor nothing occludes.
    for (const IRMath::ivec2 shift : {IRMath::ivec2(0), kLosFarShift}) {
        SCOPED_TRACE(testing::Message() << "scene shift (" << shift.x << ", " << shift.y << ")");
        LosProbeCounts counts;
        runOcclusionProbe(kLosGroundSource, shift, true, IRComponents::kFogLosHardGate, counts);
        EXPECT_GT(counts.occludedInDisc_, 0) << "the ridge and pillar occlude nothing in the disc";
        EXPECT_GT(counts.visibleInDisc_, 0) << "the fixture reveals nothing";
        EXPECT_EQ(counts.partialInDisc_, 0) << "the hard gate is a step";
        EXPECT_GT(counts.faceVisible_, 0) << "the ridge's facing wall is never visible";
        EXPECT_GT(counts.faceHidden_, 0) << "the ridge's top, seen from below, is never hidden";

        runOcclusionProbe(kLosGroundSource, shift, true, 1.0f, counts);
        EXPECT_GT(counts.occludedInDisc_, 0);
        EXPECT_GT(counts.visibleInDisc_, 0);
        EXPECT_EQ(counts.partialInDisc_, 0) << "an eye below every occluder top has no far edge";

        runOcclusionProbe(kLosRidgeSource, shift, true, 1.0f, counts);
        EXPECT_GT(counts.occludedInDisc_, 0) << "the ridge hides nothing at its base";
        EXPECT_GT(counts.visibleInDisc_, 0);
        EXPECT_GT(counts.partialInDisc_, 0) << "softness 1 grades no sample past the far edge";

        runOcclusionProbe(kLosGroundSource, shift, false, IRComponents::kFogLosHardGate, counts);
        EXPECT_EQ(counts.occludedInDisc_, 0) << "flat ground occluded itself";
        EXPECT_GT(counts.visibleInDisc_, 0);
    }
}

namespace {

constexpr std::uint32_t kBindingSurfaceGridImage = 2; // canvasFogOfWar in ir_fog_common.glsl
constexpr std::uint32_t kBindingSurfaceLosImage = 4;  // IR_FOG_LOS_BINDING in the probe
constexpr std::uint32_t kBindingSurfaceProbeOut = 1;  // std430 binding in the probe
constexpr std::uint32_t kBindingSurfaceProbeIn = 2;   // std430 binding in the probe
constexpr int kSurfaceProbeLocalSize = 64;            // local_size_x in the probe

// An ungated soft-edged source standing on the ground plane, so a sample's
// lift above the ground is its dzUp.
const LosProbeSource kSurfaceGroundSource{IRMath::vec4(0.5f, 0.0f, 14.0f, 2.0f), kLosGround};

struct FogSurfaceProbeHeader {
    std::int32_t sampleCount_;
    std::int32_t pad_[3];
};
static_assert(sizeof(FogSurfaceProbeHeader) == 16, "must match the probe's std430 header");

struct FogSurfaceArm {
    const char *label_;
    LosProbeSource source_;
    bool gated_ = false;
    float softness_ = IRComponents::kFogLosHardGate;
    bool occluders_ = false;
    float ceilingHeight_ = IRComponents::kFogVisionCeilingOff;
    float fadeHeight_ = 0.0f;
    bool treated_ = false;
    float density_ = 0.0f;
    bool fullCompetitor_ = false;
};

struct FogSurfaceCounts {
    int hidden_ = 0;
    int visible_ = 0;
    int partial_ = 0;
    int styled_ = 0;
    int dissolved_ = 0;
    int flips_ = 0;
};

} // namespace

// The shared FIELD reveal (the real ir_fog_common.glsl) agrees with the CPU
// paint oracle on every probed sample — the state to 1e-3 and the styled band
// to 5e-3 — across a hard ceiling, a soft ceiling, a line-of-sight gate alone,
// the two combined, the treatment retaining every voxel, a fully visible
// competitor suppressing it, and a density-1 dissolve. Non-vacuity: each arm
// asserts the sample classes it exists to exercise. A dissolve decision
// sitting within float rounding of its threshold may flip between the two
// evaluations; at most two such flips are tolerated and every other band
// agrees.
TEST_F(FogCrossSectionTest, GpuRevealSurfaceMatchesTheCpuOracle) {
    using namespace IRRender;
    using IRComponents::C_CanvasFogOfWar;
    using IRComponents::FogLosColumnField;

    const std::string probePath =
        std::string(IR_TEST_GPU_SHADER_DIR) + "/c_fog_reveal_surface_probe.glsl";
    ShaderProgram program{std::vector{ShaderStage{probePath.c_str(), ShaderType::COMPUTE}}};
    Texture2D losTexture{
        TextureKind::TEXTURE_2D,
        IRComponents::kFogLosTextureSize,
        IRComponents::kFogLosTextureHeight,
        TextureFormat::RGBA32F
    };
    const IRMath::ivec2 windowOrigin = kProbeDefaultWindowOrigin;
    const IRMath::ivec2 fieldMin =
        FogLosColumnField::fieldMinForWindow(windowOrigin, kProbeWindowEdge);

    const auto runArm = [&](const FogSurfaceArm &arm, FogSurfaceCounts &counts) {
        counts = FogSurfaceCounts{};
        FrameDataFogObservers observers{};
        observers.windowOriginX_ = windowOrigin.x;
        observers.windowOriginY_ = windowOrigin.y;
        const int slot = C_CanvasFogOfWar::addVisionCircle(
            observers,
            arm.source_.circle_.x,
            arm.source_.circle_.y,
            arm.source_.circle_.z,
            arm.source_.circle_.w,
            arm.source_.observerZ_,
            0.0f,
            0.0f,
            0.0f
        );
        ASSERT_EQ(slot, 0);
        if (arm.gated_) {
            C_CanvasFogOfWar::setVisionCircleLineOfSight(
                observers,
                slot,
                kLosEyeHeight,
                arm.softness_
            );
        }
        C_CanvasFogOfWar::setVisionCircleCeiling(
            observers,
            slot,
            arm.ceilingHeight_,
            arm.fadeHeight_
        );
        if (arm.fullCompetitor_) {
            ASSERT_EQ(
                C_CanvasFogOfWar::addVisionCircle(
                    observers,
                    arm.source_.circle_.x,
                    arm.source_.circle_.y,
                    arm.source_.circle_.z,
                    arm.source_.circle_.w,
                    arm.source_.observerZ_,
                    0.0f,
                    0.0f,
                    0.0f
                ),
                1
            );
        }
        if (arm.treated_) {
            C_CanvasFogOfWar::setRevealSurfaceTreatment(observers, arm.density_);
        }
        FrameDataFogObservers untreated = observers;
        C_CanvasFogOfWar::clearRevealSurfaceTreatment(untreated);
        m_observers->subData(0, sizeof(FrameDataFogObservers), &observers);

        const std::vector<float> field = losProbeField(arm.occluders_, IRMath::ivec2(0), fieldMin);
        uploadLosField(losTexture, field);
        const FogLosColumnField columns{field.data(), fieldMin};

        std::vector<IRMath::vec4> samples;
        for (int lift = 0; lift < 8; ++lift) {
            for (int y = -12; y < 12; ++y) {
                for (int x = -12; x < 12; ++x) {
                    for (const IRMath::vec2 frac :
                         {IRMath::vec2(0.25f, 0.5f), IRMath::vec2(0.75f, 0.125f)}) {
                        const IRMath::vec2 xy =
                            IRMath::vec2(static_cast<float>(x), static_cast<float>(y)) + frac;
                        const float ownTop = columns.topPlane(
                            FogLosColumnField::halfCellOf(xy.x),
                            FogLosColumnField::halfCellOf(xy.y)
                        );
                        samples.emplace_back(
                            xy.x,
                            xy.y,
                            IRMath::min(kLosGround - static_cast<float>(lift), ownTop),
                            0.0f
                        );
                    }
                }
            }
        }

        const FogSurfaceProbeHeader header{static_cast<std::int32_t>(samples.size()), {0, 0, 0}};
        std::vector<std::uint8_t> input(sizeof(header) + samples.size() * sizeof(IRMath::vec4));
        std::memcpy(input.data(), &header, sizeof(header));
        std::memcpy(
            input.data() + sizeof(header),
            samples.data(),
            samples.size() * sizeof(IRMath::vec4)
        );
        Buffer probeIn{
            input.data(),
            input.size(),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBindingSurfaceProbeIn
        };
        const std::vector<IRMath::vec4> seed(samples.size(), IRMath::vec4(-1.0f));
        Buffer probeOut{
            seed.data(),
            seed.size() * sizeof(IRMath::vec4),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBindingSurfaceProbeOut
        };

        program.use();
        m_fogGrid->bindAsImage(
            kBindingSurfaceGridImage,
            TextureAccess::READ_ONLY,
            TextureFormat::RG32UI
        );
        losTexture
            .bindAsImage(kBindingSurfaceLosImage, TextureAccess::READ_ONLY, TextureFormat::RGBA32F);
        m_observers->bindBase(BufferTarget::UNIFORM, kBindingFogObservers);
        probeIn.bindBase(BufferTarget::SHADER_STORAGE, kBindingSurfaceProbeIn);
        probeOut.bindBase(BufferTarget::SHADER_STORAGE, kBindingSurfaceProbeOut);
        const int groups =
            IRMath::divCeil(static_cast<int>(samples.size()), kSurfaceProbeLocalSize);
        ENG_API->glDispatchCompute(groups, 1, 1);
        ENG_API->glMemoryBarrier(GL_ALL_BARRIER_BITS);
        ENG_API->glFinish();

        std::vector<IRMath::vec4> readback(samples.size(), IRMath::vec4(-1.0f));
        probeOut.getSubData(0, readback.size() * sizeof(IRMath::vec4), readback.data());

        for (std::size_t i = 0; i < samples.size(); ++i) {
            const IRMath::vec3 position(samples[i]);
            const IRPrefab::Fog::RevealSurfaceSample cpu = IRPrefab::Fog::evalRevealSurface(
                observers,
                columns,
                IRComponents::kFogStateUnexplored,
                position
            );
            const IRPrefab::Fog::RevealSurfaceSample plain = IRPrefab::Fog::evalRevealSurface(
                untreated,
                columns,
                IRComponents::kFogStateUnexplored,
                position
            );
            const float gpuState = readback[i].x;
            const float gpuBand = readback[i].w;
            const bool bandAgrees = IRMath::abs(gpuBand - cpu.styledBand_) <= 5e-3f;
            if (!bandAgrees && arm.density_ > 0.0f &&
                (gpuBand == 0.0f || cpu.styledBand_ == 0.0f)) {
                ++counts.flips_;
            } else {
                ASSERT_NEAR(gpuState, cpu.state_, 1e-3f)
                    << arm.label_ << ": GPU state diverged from the oracle at (" << position.x
                    << ", " << position.y << ", " << position.z << ")";
                EXPECT_TRUE(bandAgrees)
                    << arm.label_ << ": GPU band " << gpuBand << " vs oracle " << cpu.styledBand_
                    << " at (" << position.x << ", " << position.y << ", " << position.z << ")";
            }
            EXPECT_FLOAT_EQ(readback[i].y, 0.0f) << "the probe grid is unexplored everywhere";
            if (plain.state_ <= 0.0f) {
                ++counts.hidden_;
            } else if (plain.state_ >= 1.0f) {
                ++counts.visible_;
            } else {
                ++counts.partial_;
            }
            if (gpuBand > 0.0f) {
                ++counts.styled_;
            }
            if (plain.state_ > 0.0f && plain.state_ < 1.0f && gpuBand == 0.0f &&
                gpuState < plain.state_ - 1e-3f) {
                ++counts.dissolved_;
            }
        }
    };

    FogSurfaceCounts counts;
    runArm({"hard ceiling", kSurfaceGroundSource, false, 0.0f, false, 2.0f, 0.0f}, counts);
    EXPECT_GT(counts.hidden_, 0) << "the hard ceiling hides nothing";
    EXPECT_GT(counts.visible_, 0);
    EXPECT_EQ(counts.partial_, 0) << "a hard ceiling has no partial band";
    EXPECT_EQ(counts.styled_, 0);

    runArm({"soft ceiling", kSurfaceGroundSource, false, 0.0f, false, 2.0f, 3.0f}, counts);
    EXPECT_GT(counts.hidden_, 0);
    EXPECT_GT(counts.visible_, 0);
    EXPECT_GT(counts.partial_, 0) << "the fade grades no sample";
    EXPECT_EQ(counts.styled_, 0) << "the treatment is off";

    runArm({"line of sight only", kLosRidgeSource, true, 1.0f, true}, counts);
    EXPECT_GT(counts.hidden_, 0) << "the ridge occludes nothing";
    EXPECT_GT(counts.visible_, 0);
    EXPECT_GT(counts.partial_, 0) << "softness 1 grades no sample past the far edge";
    EXPECT_EQ(counts.styled_, 0);

    runArm({"ceiling and line of sight", kLosRidgeSource, true, 1.0f, true, 1.0f, 3.0f}, counts);
    EXPECT_GT(counts.hidden_, 0);
    EXPECT_GT(counts.partial_, 0);
    EXPECT_EQ(counts.styled_, 0);

    runArm(
        {"treatment retains every voxel",
         kSurfaceGroundSource,
         false,
         0.0f,
         false,
         2.0f,
         3.0f,
         true,
         0.0f},
        counts
    );
    EXPECT_GT(counts.partial_, 0);
    EXPECT_EQ(counts.styled_, counts.partial_) << "density 0 styles every partial sample";
    EXPECT_EQ(counts.dissolved_, 0);
    EXPECT_EQ(counts.flips_, 0);

    runArm(
        {"full competitor suppresses the treatment",
         kSurfaceGroundSource,
         false,
         0.0f,
         false,
         2.0f,
         3.0f,
         true,
         0.0f,
         true},
        counts
    );
    EXPECT_GT(counts.visible_, 0);
    EXPECT_EQ(counts.partial_, 0) << "the unceilinged twin reveals every sample the band would";
    EXPECT_EQ(counts.styled_, 0) << "a fully visible competitor suppresses the treatment";

    runArm({"dissolve", kSurfaceGroundSource, false, 0.0f, false, 2.0f, 3.0f, true, 1.0f}, counts);
    EXPECT_GT(counts.partial_, 0);
    EXPECT_GT(counts.styled_, 0) << "density 1 retains nothing";
    EXPECT_GT(counts.dissolved_, 0) << "density 1 dissolves nothing";
    EXPECT_LE(counts.flips_, 2);

    runArm(
        {"ceiling, line of sight and treatment",
         kLosRidgeSource,
         true,
         1.0f,
         true,
         1.0f,
         3.0f,
         true,
         0.5f},
        counts
    );
    EXPECT_GT(counts.partial_, 0);
    EXPECT_GT(counts.styled_, 0);
    EXPECT_LE(counts.flips_, 2);
}

#else // Metal / other backends

// The GPU half needs the hidden GL 4.5 context; keep a registered placeholder
// so the suite appears (as skipped) in the cross-backend inventory rather than
// silently vanishing. The FogCrossSectionShaderParity tests above still run
// here, and are the half that pins THIS backend's copy of the clip.
TEST(FogCrossSectionTest, SkippedOnNonOpenGLBackend) {
    GTEST_SKIP() << "The FogCrossSection GPU probe targets the OpenGL backend "
                    "(hidden GL 4.5 context); source parity is covered by "
                    "FogCrossSectionShaderParity.";
}

#endif // IR_GRAPHICS_OPENGL
