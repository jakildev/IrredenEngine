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

#include <cstddef>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>

namespace {

// ---------------------------------------------------------------------------
// Shared CPU mirrors + shader-source helpers (used by both halves).
// ---------------------------------------------------------------------------

// The GLSL/MSL `smoothstep` (clamped Hermite), mirrored so the oracle in Test E
// evaluates the same curve the GPU does. IRMath has no smoothstep wrapper, and
// this is a shader-language mirror rather than a general math primitive.
float smoothstepMirror(float edge0, float edge1, float x) {
    const float t = IRMath::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// CPU mirror of `fogVisionCircleReveal` (ir_iso_common.glsl) — the ONE analytic
// curve the floor's per-pixel reveal and the per-voxel object clip share.
// `circle` = (centerX, centerY, radius, edgeSoftness).
float visionCircleReveal(IRMath::vec2 worldXY, IRMath::vec4 circle, float aa) {
    const float dist = IRMath::length(worldXY - IRMath::vec2(circle));
    const float a = IRMath::max(circle.w, aa);
    return 1.0f - smoothstepMirror(circle.z - a, circle.z + a, dist);
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
constexpr int kFogOfWarHalfExtent = 128;
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
const std::string kGlslFogPassPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/c_fog_to_trixel.glsl";
const std::string kMetalFogPassPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/c_fog_to_trixel.metal";
const std::string kGlslFogOverflowPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/c_fog_overflow_faces.glsl";
const std::string kMetalFogOverflowPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/c_fog_overflow_faces.metal";

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
    const std::regex pattern(R"(\b)" + name + R"(\s*=\s*(-?[0-9]+(?:\.[0-9]*)?)f?\s*;)");
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
        {"kFogOfWarHalfExtent", static_cast<double>(kFogOfWarHalfExtent)},
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
    const std::string glsl = readShaderSource(kGlslFogCommonPath);
    const std::string metal = readShaderSource(kMetalFogCommonPath);
    const std::string anchor = "const float t = state / kFogExploredValue;";
    const std::string glslAnchor = extractSpan(glsl, anchor, anchor, "}");
    const std::string metalAnchor = extractSpan(metal, anchor, anchor, "}");
    ASSERT_FALSE(glslAnchor.empty()) << "unexplored anchor not found in " << kGlslFogCommonPath;
    ASSERT_FALSE(metalAnchor.empty()) << "unexplored anchor not found in " << kMetalFogCommonPath;
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
}

// Every route skips the colour read-modify-write for a fully revealed sample:
// the early return sits between the reveal and the colour read, on both
// backends.
TEST(FogCrossSectionShaderParity, FullyRevealedSamplesSkipTheColourWrite) {
    const std::pair<std::string, std::string> kernels[] = {
        {kGlslFogPassPath, "imageLoad(trixelColors"},
        {kMetalFogPassPath, "trixelColors.read("},
        {kGlslFogOverflowPath, "unpackColor(colorPacked)"},
        {kMetalFogOverflowPath, "unpackColor(colorPacked)"},
    };
    for (const auto &[path, colourRead] : kernels) {
        const std::string kernel = readShaderSource(path);
        ASSERT_FALSE(kernel.empty()) << "could not read " << path;
        const std::size_t revealAt = kernel.find("fogRevealSample(");
        const std::size_t earlyOutAt = kernel.find("if (reveal.state >= 1.0");
        const std::size_t colourAt = kernel.find(colourRead);
        ASSERT_NE(revealAt, std::string::npos) << path << " no longer calls fogRevealSample";
        ASSERT_NE(earlyOutAt, std::string::npos) << path << " lost its fully-revealed early-out";
        ASSERT_NE(colourAt, std::string::npos) << path << " colour read not found";
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
    EXPECT_NE(glslAppend.find("254u : 255u"), std::string::npos)
        << "overflow class byte must reserve 255 for FIELD and 254 for BODY";

    const std::string glslOverflow = readShaderSource(kGlslFogOverflowPath);
    const std::string metalOverflow = readShaderSource(kMetalFogOverflowPath);
    const std::string glslDecode = extractSpan(
        glslOverflow,
        "const uint fogClassByte",
        "const uint fogClassByte",
        "float aaFloor"
    );
    const std::string metalDecode = extractSpan(
        metalOverflow,
        "const uint fogClassByte",
        "const uint fogClassByte",
        "float aaFloor"
    );
    ASSERT_FALSE(glslDecode.empty()) << "overflow fog class decode not found in GLSL";
    ASSERT_FALSE(metalDecode.empty()) << "overflow fog class decode not found in MSL";
    EXPECT_EQ(normalizeKernelMath(glslDecode), normalizeKernelMath(metalDecode));
    EXPECT_NE(glslDecode.find("== 254u"), std::string::npos)
        << "overflow fog route must decode BODY from class byte 254";
}

// Test E, part 6: the line-of-sight gate. The pure helpers of the
// ir_fog_los include pair — the hard gate's and the smooth gate's — reduce to
// the same maths on both backends, their constants agree with each other and
// with the component they mirror, and both shared fog reveals carry the hard
// gate and the smooth branch at the head of their source loop and declare the
// mask lane and the softness tail — so removing either gate from one backend
// fails here.
namespace {

const std::string kGlslFogLosPath = std::string(IR_TEST_RENDER_SHADER_DIR) + "/ir_fog_los.glsl";
const std::string kMetalFogLosPath =
    std::string(IR_TEST_RENDER_SHADER_DIR) + "/metal/ir_fog_los.metal";

// normalizeShaderMath plus the integer-vector spellings (MSL int2 -> ivec2).
std::string normalizeLosMath(const std::string &source) {
    return std::regex_replace(
        normalizeShaderMath(source),
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
        {"kFogLosFieldSize", 256.0},
        {"kFogLosFieldHalfExtent", static_cast<double>(kFogOfWarHalfExtent)},
        {"kFogLosSourcesPerTile", 4.0},
    };
    for (const auto &[name, mirrored] : expected) {
        double glslValue = 0.0;
        double metalValue = 0.0;
        ASSERT_TRUE(readShaderConstant(glsl, name, glslValue)) << name << " missing from GLSL";
        ASSERT_TRUE(readShaderConstant(metal, name, metalValue)) << name << " missing from MSL";
        EXPECT_DOUBLE_EQ(glslValue, metalValue) << name << " diverged between backends";
        EXPECT_DOUBLE_EQ(glslValue, mirrored) << name << " changed without this test's mirror";
    }

    for (const char *helper :
         {"fogLosSourceGated",
          "fogLosCellInField",
          "fogLosTexel",
          "fogLosHorizonChannel",
          "fogLosSampleVisible",
          "fogLosTapVerdict",
          "fogLosBilinear",
          "fogLosSmoothVisibility",
          "fogLosSurfaceSample",
          "fogLosFaceVoxel",
          "fogLosFaceSample"}) {
        const std::string glslBody = extractFunctionBody(glsl, helper);
        const std::string metalBody = extractFunctionBody(metal, helper);
        ASSERT_FALSE(glslBody.empty()) << helper << " not found in ir_fog_los.glsl";
        ASSERT_FALSE(metalBody.empty()) << helper << " not found in ir_fog_los.metal";
        EXPECT_EQ(normalizeLosMath(glslBody), normalizeLosMath(metalBody))
            << helper << " diverged between the GLSL and MSL line-of-sight gates";
    }
    // Both loaders round-trip through the shared helpers, so the dialect-only
    // image read is the one line they may differ on.
    for (const std::string *source : {&glsl, &metal}) {
        const std::string body = extractFunctionBody(*source, "fogLosVisible");
        ASSERT_FALSE(body.empty());
        EXPECT_NE(body.find("fogLosCellInField(sampleVoxel.xy)"), std::string::npos);
        EXPECT_NE(body.find("fogLosTexel(sampleVoxel.xy, source)"), std::string::npos);
        EXPECT_NE(
            body.find("fogLosSampleVisible(fogLosHorizonChannel(texel, source), sampleVoxel.z)"),
            std::string::npos
        );
    }

    const std::regex gate(
        R"(if \(fogLosSourceGated\(losSourceMask, i\) && !fogWholeBody && softness < 0\.0f? && )"
        R"(!fogLosVisible\(surfaceVoxel, i\)\) \{ continue; \})"
    );
    const std::regex smoothBranch(
        R"(if \(fogLosSourceGated\(losSourceMask, i\) && !fogWholeBody && softness >= 0\.0f?\) \{)"
    );
    for (const std::string &path : {kGlslFogCommonPath, kMetalFogCommonPath}) {
        const std::string kernel = readShaderSource(path);
        ASSERT_FALSE(kernel.empty()) << "could not read " << path;
        EXPECT_TRUE(std::regex_search(normalizeGateCallSite(kernel), gate))
            << path << " lost its line-of-sight gate";
        EXPECT_TRUE(std::regex_search(normalizeGateCallSite(kernel), smoothBranch))
            << path << " lost its smooth line-of-sight branch";
        EXPECT_NE(kernel.find("losSoftness[i >> 2][i & 3]"), std::string::npos)
            << path << " no longer reads the per-source softness";
        EXPECT_TRUE(
            std::regex_search(
                kernel,
                std::regex(
                    R"((vec4|float4) unexploredColor;\s*(//[^\n]*\s*)*(vec4|float4) losSoftness\[2\];)"
                )
            )
        ) << path
          << " no longer declares the softness tail after the unexplored colour";
        EXPECT_TRUE(
            std::regex_search(
                kernel,
                std::regex(R"(int visionCircleCount;\s*(//[^\n]*\s*)*int losSourceMask;)")
            )
        ) << path
          << " no longer reads the mask lane right after the count";
    }
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

#include <bit>
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

// Matches the fog-of-war grid the world fog canvas binds (2 x kFogOfWarHalfExtent).
constexpr int kFogGridDim = kFogOfWarHalfExtent * 2;

constexpr std::uint32_t kBindingFogGridImage = 0; // IR_VOXEL_FOG_GRID_BINDING in the probe
constexpr std::uint32_t kBindingProbeOut = 1;     // std430 binding in the probe
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
        // reveal is the analytic one under test. Full 256² so the probe domain
        // is nowhere near the out-of-range-reads-as-visible edge either.
        m_fogGrid = std::make_unique<Texture2D>(
            TextureKind::TEXTURE_2D, kFogGridDim, kFogGridDim, TextureFormat::RGBA8
        );
        const std::vector<std::uint8_t> unexplored(
            static_cast<std::size_t>(kFogGridDim) * kFogGridDim * 4, 0u
        );
        m_fogGrid->subImage2D(
            0, 0, kFogGridDim, kFogGridDim, PixelDataFormat::RGBA, PixelDataType::UNSIGNED_BYTE,
            unexplored.data()
        );

        const FrameDataFogObservers seedObservers{};
        m_observers = std::make_unique<Buffer>(
            &seedObservers, sizeof(FrameDataFogObservers), BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM, kBindingFogObservers
        );

        // Seed the output with a value no reveal can take, so a probe record the
        // dispatch never wrote is unmistakable rather than reading as 0.0
        // ("fully hidden") and quietly satisfying half the assertions.
        const std::vector<FogColumnProbe> seedProbes(kProbeColumnCount, kUnwrittenProbe);
        m_probeOut = std::make_unique<Buffer>(
            seedProbes.data(), seedProbes.size() * sizeof(FogColumnProbe), BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE, kBindingProbeOut
        );
    }

    void TearDown() override {
        // GPU resources release through the context, so they must go first.
        m_probeOut.reset();
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
    // then dispatches
    // over the whole column domain and reads the records back.
    std::vector<FogColumnProbe> runProbe(IRMath::vec4 circle) {
        using namespace IRRender;

        FrameDataFogObservers observers{};
        observers.visionCircles_[0] = circle;
        observers.visionCircleCount_ = 1;
        m_observers->subData(0, sizeof(FrameDataFogObservers), &observers);

        const std::vector<FogColumnProbe> seedProbes(kProbeColumnCount, kUnwrittenProbe);
        m_probeOut->subData(
            0, seedProbes.size() * sizeof(FogColumnProbe), seedProbes.data()
        );

        m_probeProgram->use();
        m_fogGrid->bindAsImage(
            kBindingFogGridImage, TextureAccess::READ_ONLY, TextureFormat::RGBA8
        );
        m_observers->bindBase(BufferTarget::UNIFORM, kBindingFogObservers);
        m_probeOut->bindBase(BufferTarget::SHADER_STORAGE, kBindingProbeOut);

        const int groups = kProbeDim / kProbeLocalSize;
        ENG_API->glDispatchCompute(groups, groups, 1);
        ENG_API->glMemoryBarrier(GL_ALL_BARRIER_BITS);
        ENG_API->glFinish(); // a test, not a hot path — block for the readback

        std::vector<FogColumnProbe> readback(kProbeColumnCount, kUnwrittenProbe);
        m_probeOut->getSubData(
            0, readback.size() * sizeof(FogColumnProbe), readback.data()
        );
        return readback;
    }

    static int columnX(int record) {
        return record % kProbeDim - kProbeHalfExtent;
    }

    static int columnY(int record) {
        return record / kProbeDim - kProbeHalfExtent;
    }

    static IRMath::vec2 columnCentre(int record) {
        return IRMath::vec2(static_cast<float>(columnX(record)), static_cast<float>(columnY(record))
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
    std::unique_ptr<IRRender::Buffer> m_probeOut;
};

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
    const std::vector<FogColumnProbe> probes =
        runProbe(kHardDisc);

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
    const std::vector<FogColumnProbe> probes =
        runProbe(kHardDisc);

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
    const std::vector<FogColumnProbe> probes =
        runProbe(kHardDisc);

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

// Test D — floor/object edge agreement. The two edges coincide because they are
// not two curves: stage 1's own-column test and FOG_TO_TRIXEL's per-pixel
// reveal both call `fogVisionCircleReveal` on the same circle. Asserted in its
// exact form (equality of the evaluations), which is stronger than the ±1px
// tolerance the property is usually stated with.
TEST_F(FogCrossSectionTest, ObjectClipAndFloorRevealTraceOneCurve) {
    const std::vector<FogColumnProbe> probes =
        runProbe(kHardDisc);

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
            probes[record].revealCenter, visionCircleReveal(column, kHardDisc, 0.0f), 1e-5f
        ) << "GPU own-column reveal diverged from the CPU oracle at (" << column.x << ", "
          << column.y << ")";

        EXPECT_NEAR(
            probes[record].revealNearest,
            visionCircleReveal(
                nearestCellPoint(record, kDiscCentre), kHardDisc,
                kFogColumnKeepAa + kFogHiddenKeepCells
            ),
            1e-5f
        ) << "GPU keep metric diverged from the CPU oracle at (" << column.x << ", " << column.y
          << ")";
    }
}

namespace {

// Mirrors the probe kernel's own constants (c_fog_los_probe.glsl).
constexpr int kLosProbeHalfExtent = 32;
constexpr int kLosProbeDim = kLosProbeHalfExtent * 2;
constexpr int kLosProbeLevels = 3;
constexpr int kLosProbeRecords = kLosProbeLevels * kLosProbeDim * kLosProbeDim;
constexpr std::uint32_t kBindingLosTexture = 0;  // IR_FOG_LOS_BINDING in the probe
constexpr std::uint32_t kBindingLosProbeOut = 1; // std430 binding in the probe
constexpr std::uint32_t kBindingLosProbeIn = 2;  // std430 binding in the probe

// The fixture: flat ground (top voxel 4), the fog_demo ridge (x 0..1,
// y -7..8, four voxels up) and a free-standing flagged SDF pillar, seen from a
// soft-edged source at (-6, 0) with its eye 2 above observerZ 4.5.
constexpr int kLosGround = 4;
const IRMath::vec4 kLosCircle{-6.0f, 0.0f, 14.0f, 2.0f};
constexpr float kLosObserverZ = 4.5f;
constexpr float kLosEyeHeight = 2.0f;
// The probed sample heights: the column's top voxel, and 2 and 6 above it.
constexpr int kLosLevelLift[kLosProbeLevels] = {0, 2, 6};

struct FogLosProbeHeader {
    IRMath::vec4 circle_;
    std::int32_t losSourceMask_;
    std::int32_t pad_[3];
};
static_assert(sizeof(FogLosProbeHeader) == 32, "must match the probe's std430 header");

struct FogLosProbeRecord {
    std::int32_t visible_;
    float reveal_;
};
static_assert(sizeof(FogLosProbeRecord) == 8, "must match the probe's std430 struct");

std::vector<std::int32_t> losProbeColumns(bool withOccluders) {
    std::vector<std::int32_t> columns(IRComponents::kFogLosColumnCount, kLosGround);
    if (!withOccluders) {
        return columns;
    }
    for (int y = -7; y <= 8; ++y) {
        for (int x = 0; x <= 1; ++x) {
            columns[IRComponents::C_CanvasFogOfWar::flatIndex(x, y)] = kLosGround - 4;
        }
    }
    IRMath::SDF::forEachInteriorCell(
        IRMath::SDF::ShapeType::BOX,
        IRMath::vec4(3.0f, 3.0f, 10.0f, 0.0f),
        IRMath::vec3(-8.0f, 4.0f, -1.0f),
        IRMath::ivec3(-kFogOfWarHalfExtent, -kFogOfWarHalfExtent, -1000),
        IRMath::ivec3(kFogOfWarHalfExtent - 1, kFogOfWarHalfExtent - 1, 1000),
        [&](IRMath::ivec3 cell) { IRPrefab::Fog::stampLosColumn(columns, cell); }
    );
    return columns;
}

int losProbeRecord(int level, int x, int y) {
    return (level * kLosProbeDim + (y + kLosProbeHalfExtent)) * kLosProbeDim +
           (x + kLosProbeHalfExtent);
}

} // namespace

// Same rule, both sides: the GPU gate (the real ir_fog_los.glsl) over the
// production-built horizon texture agrees with the CPU field gate on every
// probe — tolerance 0, since both compare the same uploaded float with the
// same integer — and the gated reveal matches the field-aware oracle within
// the 1e-5 the ungated GpuRevealMatchesTheCpuOracle already allows the shared
// curve. Non-vacuity: the fixture has occluded and visible in-disc probes;
// without the ridge and the pillar nothing is occluded.
TEST_F(FogCrossSectionTest, GpuOcclusionMatchesTheCpuOracle) {
    using namespace IRRender;
    using IRComponents::C_CanvasFogOfWar;
    using IRComponents::FogLineOfSightField;

    const std::string probePath = std::string(IR_TEST_GPU_SHADER_DIR) + "/c_fog_los_probe.glsl";
    ShaderProgram program{std::vector{ShaderStage{probePath.c_str(), ShaderType::COMPUTE}}};
    Texture2D losTexture{
        TextureKind::TEXTURE_2D,
        IRComponents::kFogLosTextureWidth,
        IRComponents::kFogLosTextureHeight,
        TextureFormat::RGBA32F
    };

    FrameDataFogObservers observers{};
    IRComponents::FogLosEyeHeights eyes{};
    eyes.fill(IRComponents::kFogVisionLosOff);
    const int slot = C_CanvasFogOfWar::addVisionCircle(
        observers,
        eyes,
        kLosCircle.x,
        kLosCircle.y,
        kLosCircle.z,
        kLosCircle.w,
        kLosObserverZ,
        0.0f,
        0.0f,
        0.0f
    );
    ASSERT_EQ(slot, 0);
    C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, eyes, slot, kLosEyeHeight);

    const auto runOcclusionProbe = [&](bool withOccluders,
                                       int &occludedInDisc,
                                       int &visibleInDisc) {
        const std::vector<std::int32_t> columns = losProbeColumns(withOccluders);
        std::vector<float> horizons(IRComponents::kFogLosHorizonCount, 0.0f);
        IRPrefab::Fog::buildLosHorizons(observers, eyes, columns, horizons);
        losTexture.subImage2D(
            0,
            0,
            IRComponents::kFogLosTextureWidth,
            IRComponents::kFogLosTextureHeight,
            PixelDataFormat::RGBA,
            PixelDataType::FLOAT32,
            horizons.data()
        );

        std::vector<std::int32_t> sampleZ(kLosProbeRecords, 0);
        for (int level = 0; level < kLosProbeLevels; ++level) {
            for (int y = -kLosProbeHalfExtent; y < kLosProbeHalfExtent; ++y) {
                for (int x = -kLosProbeHalfExtent; x < kLosProbeHalfExtent; ++x) {
                    sampleZ[losProbeRecord(level, x, y)] =
                        columns[C_CanvasFogOfWar::flatIndex(x, y)] - kLosLevelLift[level];
                }
            }
        }
        const FogLosProbeHeader header{kLosCircle, observers.losSourceMask_, {0, 0, 0}};
        std::vector<std::uint8_t> input(sizeof(header) + sampleZ.size() * sizeof(std::int32_t));
        std::memcpy(input.data(), &header, sizeof(header));
        std::memcpy(
            input.data() + sizeof(header),
            sampleZ.data(),
            sampleZ.size() * sizeof(std::int32_t)
        );
        Buffer probeIn{
            input.data(),
            input.size(),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBindingLosProbeIn
        };
        const std::vector<FogLosProbeRecord> seed(kLosProbeRecords, FogLosProbeRecord{-1, -1.0f});
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
        const int groups = kLosProbeDim / kProbeLocalSize;
        ENG_API->glDispatchCompute(groups, groups, 1);
        ENG_API->glMemoryBarrier(GL_ALL_BARRIER_BITS);
        ENG_API->glFinish();

        std::vector<FogLosProbeRecord> readback(kLosProbeRecords, FogLosProbeRecord{-1, -1.0f});
        probeOut.getSubData(0, readback.size() * sizeof(FogLosProbeRecord), readback.data());

        const FogLineOfSightField field{horizons.data()};
        occludedInDisc = 0;
        visibleInDisc = 0;
        for (int level = 0; level < kLosProbeLevels; ++level) {
            for (int y = -kLosProbeHalfExtent; y < kLosProbeHalfExtent; ++y) {
                for (int x = -kLosProbeHalfExtent; x < kLosProbeHalfExtent; ++x) {
                    const int record = losProbeRecord(level, x, y);
                    const IRMath::ivec3 sample(x, y, sampleZ[record]);
                    const bool cpuVisible = field.visible(0, sample);
                    ASSERT_EQ(readback[record].visible_, cpuVisible ? 1 : 0)
                        << "GPU and CPU line-of-sight gates disagree at (" << x << ", " << y << ", "
                        << sample.z << ")";
                    const IRMath::vec3 position(sample);
                    EXPECT_NEAR(
                        readback[record].reveal_,
                        IRPrefab::Fog::evalVisionReveal(observers, field, position),
                        1e-5f
                    ) << "GPU gated reveal diverged from the oracle at ("
                      << x << ", " << y << ", " << sample.z << ")";
                    if (IRPrefab::Fog::evalVisionReveal(observers, position) > 0.0f) {
                        ++(cpuVisible ? visibleInDisc : occludedInDisc);
                    }
                }
            }
        }
    };

    int occluded = 0;
    int visible = 0;
    runOcclusionProbe(true, occluded, visible);
    EXPECT_GT(occluded, 0) << "the ridge and pillar occlude nothing in the disc";
    EXPECT_GT(visible, 0) << "the fixture reveals nothing";

    runOcclusionProbe(false, occluded, visible);
    EXPECT_EQ(occluded, 0) << "flat ground occluded itself";
    EXPECT_GT(visible, 0);
}

namespace {

constexpr int kSmoothProbeLocalSize = 64;
constexpr std::uint32_t kBindingSmoothProbeFaceIn = 3;  // std430 binding in the probe
constexpr std::uint32_t kBindingSmoothProbeFaceOut = 4; // std430 binding in the probe

struct FogLosSmoothProbeHeader {
    IRMath::vec4 circle_;
    float softness_;
    std::int32_t sampleCount_;
    std::int32_t faceCount_;
    std::int32_t pad_;
};
static_assert(sizeof(FogLosSmoothProbeHeader) == 32, "must match the smooth probe's std430 header");

// The world side face whose outward normal @p cardinal rotates onto @p view's.
IRMath::FaceId worldSideFaceOf(IRMath::FaceId view, IRMath::CardinalIndex cardinal) {
    const IRMath::ivec3 viewNormal(IRMath::faceOutwardNormal(view));
    for (int id = 0; id < 4; ++id) {
        const auto face = static_cast<IRMath::FaceId>(id);
        if (IRMath::rotateCardinalZ(IRMath::ivec3(IRMath::faceOutwardNormal(face)), cardinal) ==
            viewNormal) {
            return face;
        }
    }
    return IRMath::FaceId::NONE;
}

// Side-face pixels of a few voxels on both lattices, every cardinal, micro and
// plain rasters: (isoRel, rawDepth, worldFace) + (scale, microFaces, cardinal,
// viewFace). The emitters straddle the fixture's ridge, so the columns their
// faces look into are both occluded and visible.
std::vector<IRMath::ivec4> smoothProbeFacePixels() {
    std::vector<IRMath::ivec4> pixels;
    const IRMath::vec3 emitters[] = {
        IRMath::vec3(3.0f, -7.0f, 4.0f),
        IRMath::vec3(-0.5f, 0.5f, 2.5f)
    };
    for (int c = 0; c < 4; ++c) {
        const auto cardinal = static_cast<IRMath::CardinalIndex>(c);
        for (const IRMath::vec3 &emitter : emitters) {
            for (const int scale : {1, 8}) {
                const bool micro = scale > 1;
                const IRMath::ivec3 cell = IRMath::rotateCardinalZ(
                    IRMath::roundVec3HalfUp(emitter * static_cast<float>(scale)),
                    cardinal
                );
                for (int face = 0; face < 4; ++face) {
                    const bool xAxis = face < 2;
                    const int lift = (face & 1) != 0 && micro ? scale : 0;
                    for (int u = 0; u < scale; ++u) {
                        const IRMath::ivec3 microPos =
                            xAxis ? IRMath::ivec3(cell.x + lift, cell.y + u, cell.z + u)
                                  : IRMath::ivec3(cell.x + u, cell.y + lift, cell.z + u);
                        for (int sub = 0; sub < 2; ++sub) {
                            const IRMath::ivec2 iso =
                                IRMath::pos3DtoPos2DIso(microPos) +
                                (xAxis ? IRMath::ivec2(1, 1 + sub) : IRMath::ivec2(0, 1 + sub));
                            pixels.emplace_back(
                                iso.x,
                                iso.y,
                                microPos.x + microPos.y + microPos.z,
                                static_cast<int>(
                                    worldSideFaceOf(static_cast<IRMath::FaceId>(face), cardinal)
                                )
                            );
                            pixels.emplace_back(scale, micro ? 1 : 0, c, face);
                        }
                    }
                }
            }
        }
    }
    return pixels;
}

} // namespace

// The smooth gate, both sides: at fractional positions around the fixture's
// boundaries, for softness 0 and 1, the GPU visibility (the real ir_fog_los.glsl
// over the production-built texture) matches FogLineOfSightField::visibility
// and the gated reveal matches the field-aware oracle, both within 1e-3.
// Non-vacuity: each softness has occluded, visible and partial samples in the
// disc. At softness 0 every tap verdict is 0 or 1, so the partial samples are
// the fractional-XY ones whose four taps straddle a boundary — the bilinear
// blend is what they pin. The face arm runs the fog kernel's face chain from the
// world face over enumerated side-face pixels: the view face, the recovered
// voxel, and fogLosFaceSample's column and height match the CPU twins
// (losFaceVoxel, losFaceColumn) exactly, and the single-tap visibility matches
// that column's cellVerdict within 1e-3, with occluded and visible faces at
// each softness.
TEST_F(FogCrossSectionTest, GpuSmoothOcclusionMatchesTheCpuOracle) {
    using namespace IRRender;
    using IRComponents::C_CanvasFogOfWar;
    using IRComponents::FogLineOfSightField;

    const std::string probePath =
        std::string(IR_TEST_GPU_SHADER_DIR) + "/c_fog_los_smooth_probe.glsl";
    ShaderProgram program{std::vector{ShaderStage{probePath.c_str(), ShaderType::COMPUTE}}};
    Texture2D losTexture{
        TextureKind::TEXTURE_2D,
        IRComponents::kFogLosTextureWidth,
        IRComponents::kFogLosTextureHeight,
        TextureFormat::RGBA32F
    };

    const std::vector<std::int32_t> columns = losProbeColumns(true);
    std::vector<IRMath::vec4> positions;
    for (int y = -kLosProbeHalfExtent; y < kLosProbeHalfExtent; ++y) {
        for (int x = -kLosProbeHalfExtent; x < kLosProbeHalfExtent; ++x) {
            const float top = static_cast<float>(columns[C_CanvasFogOfWar::flatIndex(x, y)]);
            for (const IRMath::vec2 offset :
                 {IRMath::vec2(0.0f), IRMath::vec2(0.25f, 0.5f), IRMath::vec2(0.75f, 0.125f)}) {
                positions.emplace_back(
                    static_cast<float>(x) + offset.x,
                    static_cast<float>(y) + offset.y,
                    top,
                    0.0f
                );
            }
        }
    }
    const std::vector<IRMath::ivec4> facePixels = smoothProbeFacePixels();
    const int faceCount = static_cast<int>(facePixels.size() / 2);

    for (const float softness : {0.0f, 1.0f}) {
        FrameDataFogObservers observers{};
        IRComponents::FogLosEyeHeights eyes{};
        eyes.fill(IRComponents::kFogVisionLosOff);
        const int slot = C_CanvasFogOfWar::addVisionCircle(
            observers,
            eyes,
            kLosCircle.x,
            kLosCircle.y,
            kLosCircle.z,
            kLosCircle.w,
            kLosObserverZ,
            0.0f,
            0.0f,
            0.0f
        );
        ASSERT_EQ(slot, 0);
        C_CanvasFogOfWar::setVisionCircleLineOfSight(
            observers,
            eyes,
            slot,
            kLosEyeHeight,
            softness
        );

        std::vector<float> horizons(IRComponents::kFogLosHorizonCount, 0.0f);
        IRPrefab::Fog::buildLosHorizons(observers, eyes, columns, horizons);
        losTexture.subImage2D(
            0,
            0,
            IRComponents::kFogLosTextureWidth,
            IRComponents::kFogLosTextureHeight,
            PixelDataFormat::RGBA,
            PixelDataType::FLOAT32,
            horizons.data()
        );

        const FogLosSmoothProbeHeader
            header{kLosCircle, softness, static_cast<std::int32_t>(positions.size()), faceCount, 0};
        std::vector<std::uint8_t> input(sizeof(header) + positions.size() * sizeof(IRMath::vec4));
        std::memcpy(input.data(), &header, sizeof(header));
        std::memcpy(
            input.data() + sizeof(header),
            positions.data(),
            positions.size() * sizeof(IRMath::vec4)
        );
        Buffer probeIn{
            input.data(),
            input.size(),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBindingLosProbeIn
        };
        Buffer faceIn{
            facePixels.data(),
            facePixels.size() * sizeof(IRMath::ivec4),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBindingSmoothProbeFaceIn
        };
        const std::vector<IRMath::vec4> sampleSeed(positions.size(), IRMath::vec4(-1.0f));
        Buffer sampleOut{
            sampleSeed.data(),
            sampleSeed.size() * sizeof(IRMath::vec4),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBindingLosProbeOut
        };
        const std::vector<IRMath::ivec4> faceSeed(facePixels.size(), IRMath::ivec4(-9999));
        Buffer faceOut{
            faceSeed.data(),
            faceSeed.size() * sizeof(IRMath::ivec4),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBindingSmoothProbeFaceOut
        };

        program.use();
        losTexture
            .bindAsImage(kBindingLosTexture, TextureAccess::READ_ONLY, TextureFormat::RGBA32F);
        probeIn.bindBase(BufferTarget::SHADER_STORAGE, kBindingLosProbeIn);
        faceIn.bindBase(BufferTarget::SHADER_STORAGE, kBindingSmoothProbeFaceIn);
        sampleOut.bindBase(BufferTarget::SHADER_STORAGE, kBindingLosProbeOut);
        faceOut.bindBase(BufferTarget::SHADER_STORAGE, kBindingSmoothProbeFaceOut);
        const int threads = IRMath::max(static_cast<int>(positions.size()), faceCount);
        ENG_API->glDispatchCompute(
            (threads + kSmoothProbeLocalSize - 1) / kSmoothProbeLocalSize,
            1,
            1
        );
        ENG_API->glMemoryBarrier(GL_ALL_BARRIER_BITS);
        ENG_API->glFinish();

        std::vector<IRMath::vec4> samples(positions.size(), IRMath::vec4(-1.0f));
        sampleOut.getSubData(0, samples.size() * sizeof(IRMath::vec4), samples.data());
        std::vector<IRMath::ivec4> faces(facePixels.size(), IRMath::ivec4(-9999));
        faceOut.getSubData(0, faces.size() * sizeof(IRMath::ivec4), faces.data());

        const FogLineOfSightField field{horizons.data()};
        int occluded = 0;
        int visible = 0;
        int partial = 0;
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const IRMath::vec3 position(positions[i]);
            const float cpuVisibility = field.visibility(0, position, softness);
            ASSERT_NEAR(samples[i].x, cpuVisibility, 1e-3f)
                << "GPU and CPU smooth gates disagree at (" << position.x << ", " << position.y
                << ", " << position.z << ") softness " << softness;
            EXPECT_NEAR(
                samples[i].y,
                IRPrefab::Fog::evalVisionReveal(observers, field, position),
                1e-3f
            ) << "GPU smooth reveal diverged from the oracle at ("
              << position.x << ", " << position.y << ")";
            if (IRPrefab::Fog::evalVisionReveal(observers, position) > 0.0f) {
                if (cpuVisibility == 0.0f) {
                    ++occluded;
                } else if (cpuVisibility == 1.0f) {
                    ++visible;
                } else {
                    ++partial;
                }
            }
        }
        EXPECT_GT(occluded, 0) << "softness " << softness;
        EXPECT_GT(visible, 0) << "softness " << softness;
        EXPECT_GT(partial, 0) << "softness " << softness;

        int faceOccluded = 0;
        int faceVisible = 0;
        for (int i = 0; i < faceCount; ++i) {
            const auto index = static_cast<std::size_t>(2 * i);
            const IRMath::ivec4 pixel = facePixels[index];
            const IRMath::ivec4 raster = facePixels[index + 1];
            const auto viewFace = static_cast<IRMath::FaceId>(raster.w);
            const IRMath::ivec3 voxel = IRPrefab::Fog::losFaceVoxel(
                IRMath::ivec2(pixel),
                pixel.z,
                viewFace,
                raster.x,
                raster.y != 0,
                static_cast<IRMath::CardinalIndex>(raster.z)
            );
            const IRMath::ivec2 column =
                IRPrefab::Fog::losFaceColumn(voxel, static_cast<IRMath::FaceId>(pixel.w));
            const float cpuVisibility = field.cellVerdict(
                0,
                column.x,
                column.y,
                static_cast<float>(voxel.z),
                softness
            );
            const IRMath::ivec4 gpuVoxel = faces[index];
            const IRMath::ivec4 gpuSample = faces[index + 1];
            ASSERT_EQ(gpuVoxel.w, raster.w) << "GPU view face diverged on face pixel " << i;
            ASSERT_EQ(IRMath::ivec3(gpuVoxel), voxel)
                << "GPU and CPU face recovery disagree on face pixel " << i;
            ASSERT_EQ(IRMath::ivec2(gpuSample), column)
                << "GPU face sample column diverged on face pixel " << i;
            ASSERT_EQ(std::bit_cast<float>(gpuSample.z), static_cast<float>(voxel.z))
                << "GPU face sample height diverged on face pixel " << i;
            ASSERT_NEAR(std::bit_cast<float>(gpuSample.w), cpuVisibility, 1e-3f)
                << "GPU and CPU face visibility disagree on face pixel " << i << " softness "
                << softness;
            if (cpuVisibility == 0.0f) {
                ++faceOccluded;
            } else if (cpuVisibility == 1.0f) {
                ++faceVisible;
            }
        }
        EXPECT_GT(faceOccluded, 0) << "softness " << softness;
        EXPECT_GT(faceVisible, 0) << "softness " << softness;
    }
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
