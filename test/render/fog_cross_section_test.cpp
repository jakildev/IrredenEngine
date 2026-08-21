// FogCrossSection — the formal harness for the per-fragment fog clip
// (see #2102).
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

// Extracts the `float reveal = 0.0 … return reveal;` accumulation out of a
// column-reveal function. That span is the portion the two backends express
// identically — the surrounding grid-memory / out-of-range short-circuits
// differ by necessity (`imageLoad` vs `fog.read`, `imageSize` vs
// `get_width`), so comparing whole bodies there would fail on dialect alone.
//
// The anchor is `<name>(`, not the bare name: both files mention
// `fogColumnRevealNearestZ` (the #2260 Z twin, which lives in the stage body)
// in a comment ABOVE these definitions, and a bare-name search matches that
// prefix — landing the span on fogColumnReveal in BOTH files, so the test
// compares one function to itself and passes no matter how far the twins have
// drifted. Verified by mutation: with the anchor fixed, a one-sided edit to
// the GLSL nearest-cell clamp fails this test.
std::string extractRevealAccumulation(const std::string &source, const std::string &functionName) {
    const std::size_t functionAt = source.find(functionName + "(");
    if (functionAt == std::string::npos) {
        return {};
    }
    const std::size_t startAt = source.find("float reveal = 0.0", functionAt);
    if (startAt == std::string::npos) {
        return {};
    }
    const std::size_t endAt = source.find("return reveal;", startAt);
    if (endAt == std::string::npos) {
        return {};
    }
    return source.substr(startAt, endAt - startAt);
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
// kFogHiddenKeepCells (the keep-ring width the #2124 cross-section cut depends
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
// the #2102 "one formula, no CPU/GPU or GL/Metal drift" claim in code — both
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

// ---------------------------------------------------------------------------
// Tests A-D — headless GPU, OpenGL only.
// ---------------------------------------------------------------------------

#if defined(IR_GRAPHICS_OPENGL)

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <irreden/render/buffer.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/texture.hpp>

#include <cstdint>
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
    // probe reads bit-identical to the stage body's Z twins — then dispatches
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
// analytic edge. Under the pre-#2102 centre-only clip the object instead ended
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
// pre-#2102 clip would actually fail it: a centre-evaluated test
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
