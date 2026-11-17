// Shared frame data and per-texel math for FRAMEBUFFER_POST_EFFECTS. Twin of
// ../ir_post_effects_common.glsl; CPU mirror and contract in
// engine/prefabs/irreden/render/post_effects_math.hpp, whose constants must
// match these exactly. Every image read is a whole-texel fetch at an integer
// coordinate clamped to the image, never a filtered lookup, so both backends
// and the CPU mirror read the same texels.

#ifndef IR_POST_EFFECTS_COMMON_METAL_INCLUDED
#define IR_POST_EFFECTS_COMMON_METAL_INCLUDED

#include <metal_stdlib>
using namespace metal;

constant int kPostEffectsMaxBlurTaps = 32;
constant int kPostEffectsDownsampleWeightSum = 8;
constant int kPostEffectsUpsampleWeightSum = 16;
constant float kPostEffectsPrefilterEpsilon = 1.0e-5f;
constant float3 kPostEffectsLumaWeights = float3(0.2126f, 0.7152f, 0.0722f);
constant int kPostEffectsBloomBlendScreen = 1;

struct FrameDataPostEffects {
    float4x4 mvpMatrix;
    // Bloom threshold, knee, intensity, scatter.
    float4 bloomParams;
    // Exposure multiplier, saturation, contrast.
    float4 gradeScalars;
    float4 gradeLift;
    float4 gradeInverseGamma;
    float4 gradeGain;
    // Blur taps on one side of the centre, bloom on, bloom blend, grade on.
    int4 switches;
    // Blur weight for offset i is blurWeights[i / 4][i % 4].
    float4 blurWeights[9];
};

// The stage interface every pass shares: one vertex stage, one fragment per
// pass, each in its own library.
struct PostEffectsVertexOut {
    float4 position [[position]];
    // Texel coordinates of the pass's first input.
    float2 texelCoords;
};

static float4 postEffectsFetch(texture2d<float> image, int2 texel) {
    const int2 size = int2(image.get_width(), image.get_height());
    return image.read(uint2(clamp(texel, int2(0), size - int2(1))));
}

// Per-axis weight of tap `index` (0..3) of the 4x4 downsample over source
// texels `2d - 1 .. 2d + 2`.
static int postEffectsDownsampleWeight(int index) {
    return (index == 0 || index == 3) ? 1 : 3;
}

// Per-axis weight of tap `index` (0..3) of the 2x tent upsample. An even
// destination coordinate reads coarse texels `k - 2 .. k + 1` with weights
// (1, 5, 7, 3); an odd one reads `k - 1 .. k + 2` with them reversed.
static int postEffectsUpsampleWeight(int index, int parity) {
    const int evenIndex = parity == 0 ? index : 3 - index;
    return evenIndex == 0 ? 1 : (evenIndex == 1 ? 5 : (evenIndex == 2 ? 7 : 3));
}

// Scales `colour` by how far its largest channel clears the threshold, with a
// quadratic shoulder of width `knee` below it.
static float3 postEffectsBloomPrefilter(float3 colour, float threshold, float knee) {
    const float brightness = max(colour.r, max(colour.g, colour.b));
    float soft = clamp(brightness - threshold + knee, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee + kPostEffectsPrefilterEpsilon);
    const float contribution =
        max(soft, brightness - threshold) / max(brightness, kPostEffectsPrefilterEpsilon);
    return colour * contribution;
}

// Destination texel `destination` of the half-resolution image below
// `source`: the 4x4 block around it, prefiltered per source texel when
// `prefilter` is set.
static float4 postEffectsDownsample(
    texture2d<float> source,
    int2 destination,
    bool prefilter,
    constant FrameDataPostEffects &frameData
) {
    float4 sum = float4(0.0f);
    for (int tapY = 0; tapY < 4; ++tapY) {
        for (int tapX = 0; tapX < 4; ++tapX) {
            float4 colour = postEffectsFetch(source, destination * 2 + int2(tapX, tapY) - int2(1));
            if (prefilter) {
                colour.rgb = postEffectsBloomPrefilter(
                    colour.rgb,
                    frameData.bloomParams.x,
                    frameData.bloomParams.y
                );
            }
            sum += colour * float(postEffectsDownsampleWeight(tapX) * postEffectsDownsampleWeight(tapY));
        }
    }
    return sum / float(kPostEffectsDownsampleWeightSum * kPostEffectsDownsampleWeightSum);
}

// Destination texel `destination` of the double-resolution image above
// `coarse`.
static float4 postEffectsUpsample(texture2d<float> coarse, int2 destination) {
    const int2 parity = destination & int2(1);
    const int2 first = (destination >> 1) - int2(2) + parity;
    float4 sum = float4(0.0f);
    for (int tapY = 0; tapY < 4; ++tapY) {
        for (int tapX = 0; tapX < 4; ++tapX) {
            sum += postEffectsFetch(coarse, first + int2(tapX, tapY)) *
                   float(postEffectsUpsampleWeight(tapX, parity.x) * postEffectsUpsampleWeight(tapY, parity.y));
        }
    }
    return sum / float(kPostEffectsUpsampleWeightSum * kPostEffectsUpsampleWeightSum);
}

// Exposure, lift/gamma/gain, saturation, contrast, in that order. The result
// is not clamped.
static float3 postEffectsGrade(float3 colour, constant FrameDataPostEffects &frameData) {
    colour *= frameData.gradeScalars.x;
    colour = max(
        frameData.gradeGain.rgb * colour + frameData.gradeLift.rgb * (float3(1.0f) - colour),
        float3(0.0f)
    );
    colour = pow(colour, frameData.gradeInverseGamma.rgb);
    const float luma = dot(colour, kPostEffectsLumaWeights);
    colour = mix(float3(luma), colour, frameData.gradeScalars.y);
    return (colour - float3(0.5f)) * frameData.gradeScalars.z + float3(0.5f);
}

#endif // IR_POST_EFFECTS_COMMON_METAL_INCLUDED
