// Shader-free contract of FRAMEBUFFER_POST_EFFECTS: the blur weights, the
// bloom level sizes and prefilter, the grade, the frame plan that decides what
// a tick draws, the state validation, the std140 frame-data packing, and the
// constants shared with ir_post_effects_common.{glsl,metal}. The pass's draw
// schedule against recording encoders is in prefab_system_probe_test.cpp.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/post_effects.hpp>
#include <irreden/render/post_effects_math.hpp>
#include <irreden/render/systems/system_framebuffer_post_effects.hpp>
#include <irreden/time/time_manager.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using IRComponents::BloomBlend;
using IRComponents::C_PostEffectsState;
using IRMath::ivec2;
using IRMath::vec3;
using IRMath::vec4;
namespace PostEffects = IRPrefab::PostEffects;

void expectNear(vec3 actual, vec3 expected, float tolerance = 1.0e-5f) {
    EXPECT_NEAR(actual.r, expected.r, tolerance);
    EXPECT_NEAR(actual.g, expected.g, tolerance);
    EXPECT_NEAR(actual.b, expected.b, tolerance);
}

// ---- blur ---------------------------------------------------------------------

TEST(PostEffectsMathTest, BlurTapsCoverTheRadiusAndCapAtTheShaderLoopBound) {
    EXPECT_EQ(PostEffects::blurTaps(0.0f), 0);
    EXPECT_EQ(PostEffects::blurTaps(-1.0f), 0);
    EXPECT_EQ(PostEffects::blurTaps(0.25f), 1);
    EXPECT_EQ(PostEffects::blurTaps(2.0f), 2);
    EXPECT_EQ(PostEffects::blurTaps(2.01f), 3);
    EXPECT_EQ(PostEffects::blurTaps(PostEffects::kMaxBlurRadius), PostEffects::kMaxBlurTaps);
    EXPECT_EQ(PostEffects::blurTaps(1000.0f), PostEffects::kMaxBlurTaps);
}

TEST(PostEffectsMathTest, BlurWeightsAreNormalisedMonotoneAndZeroPastTheTaps) {
    for (const float radius : {0.25f, 1.0f, 2.0f, 7.3f, 32.0f}) {
        const auto weights = PostEffects::blurWeights(radius);
        const int taps = PostEffects::blurTaps(radius);
        float total = weights[0];
        for (int offset = 1; offset <= taps; ++offset) {
            const auto index = static_cast<std::size_t>(offset);
            EXPECT_GT(weights[index], 0.0f) << radius << " @" << offset;
            EXPECT_LE(weights[index], weights[index - 1]) << radius << " @" << offset;
            total += 2.0f * weights[index];
        }
        EXPECT_NEAR(total, 1.0f, 1.0e-5f) << radius;
        for (int offset = taps + 1; offset < PostEffects::kBlurWeightCount; ++offset) {
            EXPECT_EQ(weights[static_cast<std::size_t>(offset)], 0.0f) << radius << " @" << offset;
        }
    }
    // The radius is two standard deviations: at the edge tap of an integral
    // radius the weight is exp(-2) of the centre.
    const auto weights = PostEffects::blurWeights(8.0f);
    EXPECT_NEAR(weights[8] / weights[0], 0.1353353f, 1.0e-5f);
}

TEST(PostEffectsMathTest, ZeroRadiusWeightsAreTheIdentityKernel) {
    const auto weights = PostEffects::blurWeights(0.0f);
    EXPECT_EQ(weights[0], 1.0f);
    for (int offset = 1; offset < PostEffects::kBlurWeightCount; ++offset) {
        EXPECT_EQ(weights[static_cast<std::size_t>(offset)], 0.0f);
    }
}

PostEffects::Image gradientImage(ivec2 size) {
    PostEffects::Image image = PostEffects::Image::ofSize(size);
    for (int y = 0; y < size.y; ++y) {
        for (int x = 0; x < size.x; ++x) {
            image.store(
                ivec2(x, y),
                vec4(
                    static_cast<float>(x) / static_cast<float>(size.x),
                    static_cast<float>(y) / static_cast<float>(size.y),
                    0.25f,
                    1.0f
                )
            );
        }
    }
    return image;
}

TEST(PostEffectsMathTest, BlurAxisKeepsAFlatImageAndSpreadsAnImpulseSymmetrically) {
    const auto weights = PostEffects::blurWeights(3.0f);
    const int taps = PostEffects::blurTaps(3.0f);

    PostEffects::Image flat = PostEffects::Image::ofSize(ivec2(9, 5));
    for (std::size_t i = 0; i < flat.rgba_.size(); ++i) {
        flat.rgba_[i] = static_cast<std::uint8_t>(i % 4 == 3 ? 255 : 120);
    }
    EXPECT_EQ(PostEffects::blurAxis(flat, ivec2(1, 0), weights, taps).rgba_, flat.rgba_);
    EXPECT_EQ(PostEffects::blurAxis(flat, ivec2(0, 1), weights, taps).rgba_, flat.rgba_);

    PostEffects::Image impulse = PostEffects::Image::ofSize(ivec2(15, 3));
    impulse.store(ivec2(7, 1), vec4(1.0f));
    const PostEffects::Image spread = PostEffects::blurAxis(impulse, ivec2(1, 0), weights, taps);
    for (int offset = 0; offset <= taps; ++offset) {
        const std::uint8_t expected =
            IRMath::roundFloatToByte(weights[static_cast<std::size_t>(offset)]);
        EXPECT_EQ(spread.rgba_[spread.index(ivec2(7 + offset, 1))], expected) << offset;
        EXPECT_EQ(spread.rgba_[spread.index(ivec2(7 - offset, 1))], expected) << offset;
    }
    EXPECT_EQ(spread.rgba_[spread.index(ivec2(7 + taps + 1, 1))], 0);
    EXPECT_EQ(spread.rgba_[spread.index(ivec2(7, 0))], 0) << "a horizontal pass reads one row";
}

TEST(PostEffectsMathTest, ImageFetchClampsAtTheEdge) {
    const PostEffects::Image image = gradientImage(ivec2(4, 4));
    EXPECT_EQ(image.fetch(ivec2(-3, 1)), image.fetch(ivec2(0, 1)));
    EXPECT_EQ(image.fetch(ivec2(9, 9)), image.fetch(ivec2(3, 3)));
}

// ---- bloom --------------------------------------------------------------------

TEST(PostEffectsMathTest, BloomLevelsHalveFromHalfResolutionAndNeverReachZero) {
    const ivec2 size(1296, 730);
    EXPECT_EQ(PostEffects::bloomLevelSize(size, 0), ivec2(648, 365));
    EXPECT_EQ(PostEffects::bloomLevelSize(size, 1), ivec2(324, 182));
    EXPECT_EQ(PostEffects::bloomLevelSize(size, 5), ivec2(20, 11));
    EXPECT_EQ(PostEffects::bloomLevelSize(ivec2(5, 3), 0), ivec2(2, 1));
    EXPECT_EQ(PostEffects::bloomLevelSize(ivec2(5, 3), 4), ivec2(1, 1));
    // The shaders derive a level from the one above it by the same rule.
    for (int level = 1; level < PostEffects::kMaxBloomLevels; ++level) {
        const ivec2 above = PostEffects::bloomLevelSize(size, level - 1);
        EXPECT_EQ(
            PostEffects::bloomLevelSize(size, level),
            IRMath::max(ivec2(above.x >> 1, above.y >> 1), ivec2(1))
        );
    }
}

TEST(PostEffectsMathTest, FilterWeightsSumToTheirDivisors) {
    int down = 0;
    for (const int weight : PostEffects::kDownsampleWeights) {
        down += weight;
    }
    EXPECT_EQ(down, PostEffects::kDownsampleWeightSum);
    int up = 0;
    for (const int weight : PostEffects::kUpsampleWeights) {
        up += weight;
    }
    EXPECT_EQ(up, PostEffects::kUpsampleWeightSum);
    // A 2x bilinear upsample (weights 1/4, 3/4 for an even destination)
    // convolved with a (1, 2, 1) / 4 tent.
    const float bilinear[2] = {0.25f, 0.75f};
    const float tent[3] = {0.25f, 0.5f, 0.25f};
    float expected[4] = {};
    for (int pair = 0; pair < 2; ++pair) {
        for (int tap = 0; tap < 3; ++tap) {
            expected[pair + tap] += bilinear[pair] * tent[tap];
        }
    }
    for (int tap = 0; tap < 4; ++tap) {
        EXPECT_FLOAT_EQ(
            static_cast<float>(PostEffects::kUpsampleWeights[static_cast<std::size_t>(tap)]) /
                static_cast<float>(PostEffects::kUpsampleWeightSum),
            expected[tap]
        ) << tap;
    }
}

TEST(PostEffectsMathTest, PrefilterCutsBelowTheShoulderAndPassesTheExcessAbove) {
    const vec3 dim(0.2f, 0.1f, 0.05f);
    expectNear(PostEffects::bloomPrefilter(dim, 0.7f, 0.2f), vec3(0.0f));

    // Well above the shoulder: scaled by (brightness - threshold) / brightness.
    const vec3 bright(1.0f, 0.5f, 0.25f);
    expectNear(PostEffects::bloomPrefilter(bright, 0.7f, 0.1f), bright * 0.3f);

    // With no knee the cut is hard at the threshold.
    expectNear(PostEffects::bloomPrefilter(vec3(0.7f), 0.7f, 0.0f), vec3(0.0f));
    expectNear(PostEffects::bloomPrefilter(vec3(0.8f), 0.7f, 0.0f), vec3(0.1f));

    // Inside the shoulder the response is the quadratic, continuous with the
    // linear part at threshold + knee.
    const float atShoulderTop = PostEffects::bloomPrefilter(vec3(0.9f), 0.7f, 0.2f).r;
    EXPECT_NEAR(atShoulderTop, 0.2f, 1.0e-4f);
    const float atThreshold = PostEffects::bloomPrefilter(vec3(0.7f), 0.7f, 0.2f).r;
    EXPECT_NEAR(atThreshold, 0.05f, 1.0e-4f);

    // A zero threshold passes everything; black stays black, never NaN.
    expectNear(PostEffects::bloomPrefilter(bright, 0.0f, 0.0f), bright);
    expectNear(PostEffects::bloomPrefilter(vec3(0.0f), 0.0f, 0.0f), vec3(0.0f));
}

// ---- grade --------------------------------------------------------------------

TEST(PostEffectsMathTest, DefaultGradeIsTheIdentity) {
    const PostEffects::GradeParams params = PostEffects::gradeParams(C_PostEffectsState{});
    for (const vec3 colour : {vec3(0.0f), vec3(1.0f), vec3(0.2f, 0.6f, 0.9f)}) {
        expectNear(PostEffects::gradeColour(colour, params), colour, 1.0e-6f);
    }
}

TEST(PostEffectsMathTest, GradeStagesApplyInTheDocumentedOrder) {
    const vec3 colour(0.2f, 0.4f, 0.1f);

    C_PostEffectsState exposure;
    exposure.exposureStops_ = 1.0f;
    expectNear(PostEffects::gradeColour(colour, PostEffects::gradeParams(exposure)), colour * 2.0f);

    C_PostEffectsState lift;
    lift.lift_ = vec3(0.1f, 0.0f, -0.1f);
    const PostEffects::GradeParams liftParams = PostEffects::gradeParams(lift);
    expectNear(PostEffects::gradeColour(vec3(0.0f), liftParams), vec3(0.1f, 0.0f, 0.0f));
    expectNear(PostEffects::gradeColour(vec3(1.0f), liftParams), vec3(1.0f));

    C_PostEffectsState gain;
    gain.gain_ = vec3(2.0f, 1.0f, 0.5f);
    expectNear(
        PostEffects::gradeColour(colour, PostEffects::gradeParams(gain)),
        vec3(0.4f, 0.4f, 0.05f)
    );

    C_PostEffectsState gamma;
    gamma.gamma_ = vec3(2.0f);
    expectNear(PostEffects::gradeColour(vec3(0.25f), PostEffects::gradeParams(gamma)), vec3(0.5f));

    C_PostEffectsState grey;
    grey.saturation_ = 0.0f;
    const float luma = 0.2126f * 0.2f + 0.7152f * 0.4f + 0.0722f * 0.1f;
    expectNear(PostEffects::gradeColour(colour, PostEffects::gradeParams(grey)), vec3(luma));

    C_PostEffectsState flat;
    flat.contrast_ = 0.0f;
    expectNear(PostEffects::gradeColour(colour, PostEffects::gradeParams(flat)), vec3(0.5f));

    // Exposure feeds the gain, the gain the gamma, the gamma the contrast.
    C_PostEffectsState chained;
    chained.exposureStops_ = 1.0f;
    chained.gain_ = vec3(0.5f);
    chained.gamma_ = vec3(2.0f);
    chained.contrast_ = 2.0f;
    expectNear(
        PostEffects::gradeColour(vec3(0.25f), PostEffects::gradeParams(chained)),
        vec3(0.5f)
    );
}

TEST(PostEffectsMathTest, GradeImageCarriesAlphaAndClampsColour) {
    PostEffects::Image image = PostEffects::Image::ofSize(ivec2(2, 1));
    image.store(ivec2(0, 0), vec4(0.5f, 0.5f, 0.5f, 0.25f));
    image.store(ivec2(1, 0), vec4(0.9f, 0.1f, 0.4f, 1.0f));
    C_PostEffectsState state;
    state.exposureStops_ = 2.0f;
    const PostEffects::Image graded =
        PostEffects::gradeImage(image, PostEffects::gradeParams(state));
    EXPECT_EQ(graded.rgba_[0], 255) << "0.5 * 4 clamps to white";
    EXPECT_EQ(graded.rgba_[3], image.rgba_[3]);
    EXPECT_EQ(
        graded.rgba_[4 + 1],
        IRMath::roundFloatToByte(static_cast<float>(image.rgba_[4 + 1]) / 255.0f * 4.0f)
    ) << "the grade reads the stored 8-bit texel";
    EXPECT_EQ(graded.rgba_[4 + 3], 255);
}

// ---- frame plan ---------------------------------------------------------------

TEST(PostEffectsMathTest, PlanIsOffWithoutStateAndWithEveryDefault) {
    EXPECT_FALSE(PostEffects::planFrame(nullptr).enabled());
    const C_PostEffectsState defaults;
    EXPECT_FALSE(PostEffects::planFrame(&defaults).enabled());
    EXPECT_FALSE(PostEffects::isEnabled(defaults));
}

TEST(PostEffectsMathTest, PlanSwitchesEachEffectIndependently) {
    C_PostEffectsState blur;
    blur.blurEnabled_ = true;
    blur.blurRadius_ = 4.5f;
    PostEffects::FramePlan plan = PostEffects::planFrame(&blur);
    EXPECT_TRUE(plan.blur_);
    EXPECT_EQ(plan.blurTaps_, 5);
    EXPECT_FALSE(plan.bloom_);
    EXPECT_FALSE(plan.grade_);
    EXPECT_EQ(plan.bloomLevels_, 0);

    C_PostEffectsState bloom;
    bloom.bloomEnabled_ = true;
    bloom.bloomLevels_ = 3;
    plan = PostEffects::planFrame(&bloom);
    EXPECT_TRUE(plan.bloom_);
    EXPECT_EQ(plan.bloomLevels_, 3);
    EXPECT_FALSE(plan.blur_);
    EXPECT_EQ(plan.blurTaps_, 0) << "a parked blur radius must not reach the shader";
    EXPECT_FALSE(plan.grade_);

    C_PostEffectsState grade;
    grade.gradeEnabled_ = true;
    plan = PostEffects::planFrame(&grade);
    EXPECT_TRUE(plan.grade_);
    EXPECT_FALSE(plan.blur_);
    EXPECT_FALSE(plan.bloom_);
}

TEST(PostEffectsMathTest, AnEffectThatCannotChangeATexelDrawsNothing) {
    C_PostEffectsState state;
    state.blurEnabled_ = true;
    state.blurRadius_ = 0.0f;
    state.bloomEnabled_ = true;
    state.bloomIntensity_ = 0.0f;
    EXPECT_FALSE(PostEffects::planFrame(&state).enabled());
}

// ---- frame data ---------------------------------------------------------------

TEST(PostEffectsFrameDataTest, PacksEveryFieldWhereTheShaderBlockReadsIt) {
    C_PostEffectsState state;
    state.bloomEnabled_ = true;
    state.bloomThreshold_ = 0.6f;
    state.bloomKnee_ = 0.15f;
    state.bloomIntensity_ = 1.5f;
    state.bloomScatter_ = 0.8f;
    state.bloomLevels_ = 4;
    state.bloomBlend_ = BloomBlend::SCREEN;
    state.blurEnabled_ = true;
    state.blurRadius_ = 6.0f;
    state.gradeEnabled_ = true;
    state.exposureStops_ = -1.0f;
    state.lift_ = vec3(0.01f, 0.02f, 0.03f);
    state.gamma_ = vec3(2.0f, 4.0f, 0.5f);
    state.gain_ = vec3(1.1f, 1.2f, 1.3f);
    state.saturation_ = 1.25f;
    state.contrast_ = 0.75f;
    const PostEffects::FramePlan plan = PostEffects::planFrame(&state);
    const IRMath::mat4 mvp = IRSystem::detail::framebufferSelfMvp(ivec2(320, 180));

    const IRSystem::FrameDataPostEffects data =
        IRSystem::packPostEffectsFrameData(state, plan, mvp);
    EXPECT_EQ(data.mvpMatrix_, mvp);
    EXPECT_EQ(data.bloomParams_, vec4(0.6f, 0.15f, 1.5f, 0.8f));
    EXPECT_EQ(data.gradeScalars_, vec4(0.5f, 1.25f, 0.75f, 0.0f));
    EXPECT_EQ(vec3(data.gradeLift_), state.lift_);
    EXPECT_EQ(vec3(data.gradeInverseGamma_), vec3(0.5f, 0.25f, 2.0f));
    EXPECT_EQ(vec3(data.gradeGain_), state.gain_);
    EXPECT_EQ(data.switches_, IRMath::ivec4(6, 1, 1, 1));

    const auto weights = PostEffects::blurWeights(6.0f);
    for (std::size_t offset = 0; offset < weights.size(); ++offset) {
        EXPECT_EQ(data.blurWeights_[offset / 4][static_cast<int>(offset % 4)], weights[offset])
            << offset;
    }
    // The block uploads as raw bytes: the weights start after the mvp's 16
    // floats and the six four-component fields' 24.
    const auto *floats = reinterpret_cast<const float *>(&data);
    EXPECT_EQ(floats[40 + 5], weights[5]);
}

TEST(PostEffectsFrameDataTest, SwitchedOffEffectsPackAsOff) {
    C_PostEffectsState state;
    state.gradeEnabled_ = true;
    state.blurRadius_ = 9.0f;
    state.bloomBlend_ = BloomBlend::SCREEN;
    const IRSystem::FrameDataPostEffects data = IRSystem::packPostEffectsFrameData(
        state,
        PostEffects::planFrame(&state),
        IRMath::mat4(1.0f)
    );
    EXPECT_EQ(data.switches_.x, 0) << "blur taps";
    EXPECT_EQ(data.switches_.y, 0) << "bloom";
    EXPECT_EQ(data.switches_.w, 1) << "grade";
}

// ---- state --------------------------------------------------------------------

class PostEffectsStateTest : public testing::Test {
  protected:
    IRTime::TimeManager m_time_manager;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(PostEffectsStateTest, DefaultsValidateAndReadBackWithoutCreatingState) {
    EXPECT_TRUE(PostEffects::validate(C_PostEffectsState{}).empty());
    EXPECT_EQ(PostEffects::stateOrNull(), nullptr);
    const C_PostEffectsState defaults = PostEffects::stateOrDefault();
    EXPECT_FALSE(defaults.bloomEnabled_);
    EXPECT_FALSE(defaults.blurEnabled_);
    EXPECT_FALSE(defaults.gradeEnabled_);
    EXPECT_EQ(PostEffects::stateOrNull(), nullptr);
}

TEST_F(PostEffectsStateTest, ValidateRejectsEveryOutOfRangeField) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const auto rejected = [](auto mutate) {
        C_PostEffectsState state;
        mutate(state);
        return !PostEffects::validate(state).empty();
    };
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) { s.bloomThreshold_ = 1.5f; }));
    EXPECT_TRUE(rejected([nan](C_PostEffectsState &s) { s.bloomThreshold_ = nan; }));
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) { s.bloomKnee_ = -0.1f; }));
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) { s.bloomIntensity_ = 8.5f; }));
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) { s.bloomScatter_ = 1.01f; }));
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) { s.bloomLevels_ = 0; }));
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) {
        s.bloomLevels_ = PostEffects::kMaxBloomLevels + 1;
    }));
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) {
        s.bloomBlend_ = static_cast<BloomBlend>(2);
    }));
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) { s.blurRadius_ = 32.5f; }));
    EXPECT_TRUE(rejected([inf](C_PostEffectsState &s) { s.blurRadius_ = inf; }));
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) { s.exposureStops_ = -9.0f; }));
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) { s.lift_.g = 1.5f; }));
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) { s.gamma_.b = 0.0f; }));
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) { s.gain_.r = -0.5f; }));
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) { s.saturation_ = 4.5f; }));
    EXPECT_TRUE(rejected([nan](C_PostEffectsState &s) { s.contrast_ = nan; }));
    // An out-of-range parameter is rejected whether or not its switch is on.
    EXPECT_TRUE(rejected([](C_PostEffectsState &s) {
        s.blurEnabled_ = false;
        s.blurRadius_ = -1.0f;
    }));
}

TEST_F(PostEffectsStateTest, SetStateStoresAValidCandidateAndAssertsOnAnInvalidOne) {
    C_PostEffectsState state;
    state.gradeEnabled_ = true;
    state.saturation_ = 1.5f;
    PostEffects::setState(state);
    ASSERT_NE(PostEffects::stateOrNull(), nullptr);
    EXPECT_TRUE(PostEffects::stateOrNull()->gradeEnabled_);
    EXPECT_FLOAT_EQ(PostEffects::stateOrNull()->saturation_, 1.5f);

    C_PostEffectsState invalid = state;
    invalid.gamma_ = vec3(0.0f);
    EXPECT_THROW(PostEffects::setState(invalid), std::runtime_error);
    EXPECT_FLOAT_EQ(PostEffects::stateOrNull()->gamma_.r, 1.0f);
}

TEST_F(PostEffectsStateTest, PublicationReadsFalseWhenTheSystemIsAbsent) {
    ASSERT_EQ(IRSystem::findSystem(IRSystem::FRAMEBUFFER_POST_EFFECTS), IRSystem::kNullSystemId);
    EXPECT_EQ(PostEffects::systemOrNull(), nullptr);
    EXPECT_FALSE(PostEffects::outputPublishedForCurrentRenderTick());
    EXPECT_EQ(PostEffects::outputColorOrNull(), nullptr);
}

// ---- shader twin constants ----------------------------------------------------

std::string readShader(const std::string &relative) {
    std::ifstream file(std::string(IR_TEST_RENDER_SHADER_DIR) + "/" + relative);
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

TEST(PostEffectsMathTest, ShaderTwinsCarryTheCpuConstantsAndTheBlockOrder) {
    const std::string constants[] = {
        "kPostEffectsMaxBlurTaps = " + std::to_string(PostEffects::kMaxBlurTaps) + ";",
        "kPostEffectsDownsampleWeightSum = " + std::to_string(PostEffects::kDownsampleWeightSum) +
            ";",
        "kPostEffectsUpsampleWeightSum = " + std::to_string(PostEffects::kUpsampleWeightSum) + ";",
        "kPostEffectsBloomBlendScreen = " + std::to_string(static_cast<int>(BloomBlend::SCREEN)) +
            ";",
        "kPostEffectsPrefilterEpsilon = 1.0e-5",
        "0.2126",
        "0.7152",
        "0.0722",
        "(index == 0 || index == 3) ? 1 : 3",
        "evenIndex == 0 ? 1 : (evenIndex == 1 ? 5 : (evenIndex == 2 ? 7 : 3))",
    };
    EXPECT_EQ(PostEffects::kPrefilterEpsilon, 1.0e-5f);
    EXPECT_EQ(PostEffects::kLumaWeights, vec3(0.2126f, 0.7152f, 0.0722f));
    EXPECT_EQ(PostEffects::kDownsampleWeights, (std::array<int, 4>{1, 3, 3, 1}));
    EXPECT_EQ(PostEffects::kUpsampleWeights, (std::array<int, 4>{1, 5, 7, 3}));
    static_assert(static_cast<int>(BloomBlend::ADD) == 0);

    // `FrameDataPostEffects` field order, the CPU struct's.
    const std::string fields[] = {
        " mvpMatrix;",
        " bloomParams;",
        " gradeScalars;",
        " gradeLift;",
        " gradeInverseGamma;",
        " gradeGain;",
        " switches;",
        " blurWeights[" + std::to_string(PostEffects::kBlurWeightVectors) + "];",
    };
    for (const char *twin : {"ir_post_effects_common.glsl", "metal/ir_post_effects_common.metal"}) {
        const std::string source = readShader(twin);
        ASSERT_FALSE(source.empty()) << twin << " not found";
        for (const std::string &line : constants) {
            EXPECT_NE(source.find(line), std::string::npos) << twin << " lacks `" << line << "`";
        }
        std::size_t cursor = source.find("FrameDataPostEffects {");
        ASSERT_NE(cursor, std::string::npos) << twin;
        for (const std::string &field : fields) {
            const std::size_t found = source.find(field, cursor);
            ASSERT_NE(found, std::string::npos) << twin << " lacks or misorders `" << field << "`";
            cursor = found;
        }
    }
}

} // namespace
