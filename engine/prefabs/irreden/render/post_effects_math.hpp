#ifndef POST_EFFECTS_MATH_H
#define POST_EFFECTS_MATH_H

#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_post_effects_state.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// The arithmetic `FRAMEBUFFER_POST_EFFECTS` shares with its shaders. The
// twins `ir_post_effects_common.glsl` / `ir_post_effects_common.metal` carry
// the same constants and the same per-texel functions; this header is their
// CPU mirror, so a headless test can pin the weights and a fixture can replay
// a frame. Every image read on either side is a whole-texel fetch at an
// integer coordinate clamped to the image, and every image is RGBA8, so a
// stage's result is quantised before the next stage reads it.
// Contract: docs/design/post-effects.md.
namespace IRPrefab::PostEffects {

inline constexpr int kMaxBloomLevels = 6;
inline constexpr float kMaxBloomIntensity = 8.0f;
inline constexpr float kMaxBlurRadius = 32.0f;
// Taps on one side of the centre texel.
inline constexpr int kMaxBlurTaps = 32;
inline constexpr int kBlurWeightCount = kMaxBlurTaps + 1;
// The weights upload as vec4s.
inline constexpr int kBlurWeightVectors = (kBlurWeightCount + 3) / 4;
inline constexpr float kMaxExposureStops = 8.0f;
inline constexpr float kMinGamma = 0.1f;
inline constexpr float kMaxGamma = 8.0f;
inline constexpr float kMaxGain = 8.0f;
inline constexpr float kMaxSaturation = 4.0f;
inline constexpr float kMaxContrast = 4.0f;

// Per-axis weights of the 4x4 downsample over source texels `2d - 1 .. 2d + 2`.
inline constexpr std::array<int, 4> kDownsampleWeights{1, 3, 3, 1};
inline constexpr int kDownsampleWeightSum = 8;
// Per-axis weights of the 2x tent upsample over coarse texels `k - 2 .. k + 1`
// for an even destination coordinate (`k = d >> 1`); an odd one reads
// `k - 1 .. k + 2` with the weights reversed.
inline constexpr std::array<int, 4> kUpsampleWeights{1, 5, 7, 3};
inline constexpr int kUpsampleWeightSum = 16;
inline constexpr float kPrefilterEpsilon = 1.0e-5f;
// Rec. 709.
inline constexpr IRMath::vec3 kLumaWeights{0.2126f, 0.7152f, 0.0722f};

static_assert(kMaxBlurTaps >= static_cast<int>(kMaxBlurRadius), "a tap per pixel of radius");
static_assert(kBlurWeightVectors == 9, "the shader blocks declare nine weight vectors");

// Taps on one side of the centre for @p radius; 0 draws no blur.
inline int blurTaps(float radius) {
    if (!(radius > 0.0f)) {
        return 0;
    }
    return IRMath::min(static_cast<int>(IRMath::ceil(radius)), kMaxBlurTaps);
}

// Gaussian weights for offsets `0 .. blurTaps(radius)`, normalised so the
// centre plus twice the rest sums to 1; the radius is two standard deviations.
// Entries past the tap count are zero.
inline std::array<float, kBlurWeightCount> blurWeights(float radius) {
    std::array<float, kBlurWeightCount> weights{};
    const int taps = blurTaps(radius);
    if (taps == 0) {
        weights[0] = 1.0f;
        return weights;
    }
    const float sigma = radius * 0.5f;
    float total = 0.0f;
    for (int offset = 0; offset <= taps; ++offset) {
        const auto distance = static_cast<float>(offset);
        weights[static_cast<std::size_t>(offset)] =
            IRMath::exp(-(distance * distance) / (2.0f * sigma * sigma));
        total += weights[static_cast<std::size_t>(offset)] * (offset == 0 ? 1.0f : 2.0f);
    }
    for (int offset = 0; offset <= taps; ++offset) {
        weights[static_cast<std::size_t>(offset)] /= total;
    }
    return weights;
}

// Size of bloom level @p level for a framebuffer of @p size: level 0 is half
// resolution and each further level halves again, never below one texel.
inline IRMath::ivec2 bloomLevelSize(IRMath::ivec2 size, int level) {
    return IRMath::max(
        IRMath::ivec2(size.x >> (level + 1), size.y >> (level + 1)),
        IRMath::ivec2(1)
    );
}

// What one RENDER tick draws. Pure, so the off and per-effect schedule is
// testable headless.
struct FramePlan {
    bool blur_ = false;
    bool bloom_ = false;
    bool grade_ = false;
    int blurTaps_ = 0;
    int bloomLevels_ = 0;

    bool enabled() const {
        return blur_ || bloom_ || grade_;
    }
};

// A switched-on effect that cannot change a texel (zero radius, zero
// intensity) draws nothing.
inline FramePlan planFrame(const IRComponents::C_PostEffectsState *state) {
    FramePlan plan;
    if (state == nullptr) {
        return plan;
    }
    plan.blurTaps_ = state->blurEnabled_ ? blurTaps(state->blurRadius_) : 0;
    plan.blur_ = plan.blurTaps_ > 0;
    plan.bloom_ = state->bloomEnabled_ && state->bloomIntensity_ > 0.0f;
    plan.bloomLevels_ = plan.bloom_ ? state->bloomLevels_ : 0;
    plan.grade_ = state->gradeEnabled_;
    return plan;
}

inline bool isEnabled(const IRComponents::C_PostEffectsState &state) {
    return planFrame(&state).enabled();
}

// Scales @p colour by how far its largest channel clears the threshold, with
// a quadratic shoulder of width @p knee below it.
inline IRMath::vec3 bloomPrefilter(IRMath::vec3 colour, float threshold, float knee) {
    const float brightness = IRMath::max(colour.r, IRMath::max(colour.g, colour.b));
    float soft = IRMath::clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee + kPrefilterEpsilon);
    const float contribution =
        IRMath::max(soft, brightness - threshold) / IRMath::max(brightness, kPrefilterEpsilon);
    return colour * contribution;
}

// The grade's shader inputs.
struct GradeParams {
    float exposure_ = 1.0f;
    IRMath::vec3 lift_ = IRMath::vec3(0.0f);
    IRMath::vec3 inverseGamma_ = IRMath::vec3(1.0f);
    IRMath::vec3 gain_ = IRMath::vec3(1.0f);
    float saturation_ = 1.0f;
    float contrast_ = 1.0f;
};

inline GradeParams gradeParams(const IRComponents::C_PostEffectsState &state) {
    GradeParams params;
    params.exposure_ = IRMath::pow(2.0f, state.exposureStops_);
    params.lift_ = state.lift_;
    params.inverseGamma_ = IRMath::vec3(1.0f) / state.gamma_;
    params.gain_ = state.gain_;
    params.saturation_ = state.saturation_;
    params.contrast_ = state.contrast_;
    return params;
}

// Exposure, lift/gamma/gain, saturation, contrast, in that order. The result
// is not clamped; the resolve clamps once at the end.
inline IRMath::vec3 gradeColour(IRMath::vec3 colour, const GradeParams &params) {
    colour *= params.exposure_;
    colour = IRMath::max(
        params.gain_ * colour + params.lift_ * (IRMath::vec3(1.0f) - colour),
        IRMath::vec3(0.0f)
    );
    colour = IRMath::vec3(
        IRMath::pow(colour.r, params.inverseGamma_.r),
        IRMath::pow(colour.g, params.inverseGamma_.g),
        IRMath::pow(colour.b, params.inverseGamma_.b)
    );
    const float luma = IRMath::dot(colour, kLumaWeights);
    colour = IRMath::mix(IRMath::vec3(luma), colour, params.saturation_);
    return (colour - IRMath::vec3(0.5f)) * params.contrast_ + IRMath::vec3(0.5f);
}

// An RGBA8 image in texel rows, the layout a texture readback returns.
struct Image {
    IRMath::ivec2 size_ = IRMath::ivec2(0);
    std::vector<std::uint8_t> rgba_;

    static Image ofSize(IRMath::ivec2 size) {
        Image image;
        image.size_ = size;
        image.rgba_.assign(
            static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) * 4u,
            0u
        );
        return image;
    }

    // The texel at @p texel clamped to the image, as the shaders fetch it.
    IRMath::vec4 fetch(IRMath::ivec2 texel) const {
        const IRMath::ivec2 clamped =
            IRMath::clamp(texel, IRMath::ivec2(0), size_ - IRMath::ivec2(1));
        const std::uint8_t *bytes = &rgba_[index(clamped)];
        return IRMath::vec4(bytes[0], bytes[1], bytes[2], bytes[3]) / 255.0f;
    }

    void store(IRMath::ivec2 texel, IRMath::vec4 value) {
        std::uint8_t *bytes = &rgba_[index(texel)];
        for (int channel = 0; channel < 4; ++channel) {
            bytes[channel] = IRMath::roundFloatToByte(value[channel]);
        }
    }

    std::size_t index(IRMath::ivec2 texel) const {
        return (static_cast<std::size_t>(texel.y) * static_cast<std::size_t>(size_.x) +
                static_cast<std::size_t>(texel.x)) *
               4u;
    }
};

// One pass of the separable blur along @p axis, (1, 0) or (0, 1).
inline Image blurAxis(
    const Image &source,
    IRMath::ivec2 axis,
    const std::array<float, kBlurWeightCount> &weights,
    int taps
) {
    Image blurred = Image::ofSize(source.size_);
    for (int y = 0; y < source.size_.y; ++y) {
        for (int x = 0; x < source.size_.x; ++x) {
            const IRMath::ivec2 texel(x, y);
            IRMath::vec4 sum = source.fetch(texel) * weights[0];
            for (int offset = 1; offset <= taps; ++offset) {
                sum += (source.fetch(texel + axis * offset) + source.fetch(texel - axis * offset)) *
                       weights[static_cast<std::size_t>(offset)];
            }
            blurred.store(texel, sum);
        }
    }
    return blurred;
}

// The resolve of a frame with only the grade on: colour graded and clamped,
// alpha carried.
inline Image gradeImage(const Image &source, const GradeParams &params) {
    Image graded = Image::ofSize(source.size_);
    for (int y = 0; y < source.size_.y; ++y) {
        for (int x = 0; x < source.size_.x; ++x) {
            const IRMath::ivec2 texel(x, y);
            const IRMath::vec4 colour = source.fetch(texel);
            graded.store(texel, IRMath::vec4(gradeColour(IRMath::vec3(colour), params), colour.a));
        }
    }
    return graded;
}

} // namespace IRPrefab::PostEffects

#endif /* POST_EFFECTS_MATH_H */
