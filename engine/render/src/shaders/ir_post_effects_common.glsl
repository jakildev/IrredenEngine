// Shared frame data and per-texel math for FRAMEBUFFER_POST_EFFECTS. Twin of
// metal/ir_post_effects_common.metal; CPU mirror and contract in
// engine/prefabs/irreden/render/post_effects_math.hpp, whose constants must
// match these exactly. Every image read is a whole-texel fetch at an integer
// coordinate clamped to the image, never a filtered lookup, so both backends
// and the CPU mirror read the same texels.

const int kPostEffectsMaxBlurTaps = 32;
const int kPostEffectsDownsampleWeightSum = 8;
const int kPostEffectsUpsampleWeightSum = 16;
const float kPostEffectsPrefilterEpsilon = 1.0e-5;
const vec3 kPostEffectsLumaWeights = vec3(0.2126, 0.7152, 0.0722);
const int kPostEffectsBloomBlendScreen = 1;

layout (std140, binding = 2) uniform FrameDataPostEffects {
    mat4 mvpMatrix;
    // Bloom threshold, knee, intensity, scatter.
    vec4 bloomParams;
    // Exposure multiplier, saturation, contrast.
    vec4 gradeScalars;
    vec4 gradeLift;
    vec4 gradeInverseGamma;
    vec4 gradeGain;
    // Blur taps on one side of the centre, bloom on, bloom blend, grade on.
    ivec4 switches;
    // Blur weight for offset i is blurWeights[i / 4][i % 4].
    vec4 blurWeights[9];
};

vec4 postEffectsFetch(sampler2D image, ivec2 texel) {
    return texelFetch(image, clamp(texel, ivec2(0), textureSize(image, 0) - ivec2(1)), 0);
}

// Per-axis weight of tap `index` (0..3) of the 4x4 downsample over source
// texels `2d - 1 .. 2d + 2`.
int postEffectsDownsampleWeight(int index) {
    return (index == 0 || index == 3) ? 1 : 3;
}

// Per-axis weight of tap `index` (0..3) of the 2x tent upsample. An even
// destination coordinate reads coarse texels `k - 2 .. k + 1` with weights
// (1, 5, 7, 3); an odd one reads `k - 1 .. k + 2` with them reversed.
int postEffectsUpsampleWeight(int index, int parity) {
    int evenIndex = parity == 0 ? index : 3 - index;
    return evenIndex == 0 ? 1 : (evenIndex == 1 ? 5 : (evenIndex == 2 ? 7 : 3));
}

// Scales `colour` by how far its largest channel clears the threshold, with a
// quadratic shoulder of width `knee` below it.
vec3 postEffectsBloomPrefilter(vec3 colour, float threshold, float knee) {
    float brightness = max(colour.r, max(colour.g, colour.b));
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + kPostEffectsPrefilterEpsilon);
    float contribution =
        max(soft, brightness - threshold) / max(brightness, kPostEffectsPrefilterEpsilon);
    return colour * contribution;
}

// Destination texel `destination` of the half-resolution image below
// `source`: the 4x4 block around it, prefiltered per source texel when
// `prefilter` is set.
vec4 postEffectsDownsample(sampler2D source, ivec2 destination, bool prefilter) {
    vec4 sum = vec4(0.0);
    for (int tapY = 0; tapY < 4; ++tapY) {
        for (int tapX = 0; tapX < 4; ++tapX) {
            vec4 colour = postEffectsFetch(source, destination * 2 + ivec2(tapX, tapY) - ivec2(1));
            if (prefilter) {
                colour.rgb = postEffectsBloomPrefilter(colour.rgb, bloomParams.x, bloomParams.y);
            }
            sum += colour * float(postEffectsDownsampleWeight(tapX) * postEffectsDownsampleWeight(tapY));
        }
    }
    return sum / float(kPostEffectsDownsampleWeightSum * kPostEffectsDownsampleWeightSum);
}

// Destination texel `destination` of the double-resolution image above
// `coarse`.
vec4 postEffectsUpsample(sampler2D coarse, ivec2 destination) {
    ivec2 parity = destination & ivec2(1);
    ivec2 first = (destination >> 1) - ivec2(2) + parity;
    vec4 sum = vec4(0.0);
    for (int tapY = 0; tapY < 4; ++tapY) {
        for (int tapX = 0; tapX < 4; ++tapX) {
            sum += postEffectsFetch(coarse, first + ivec2(tapX, tapY)) *
                   float(postEffectsUpsampleWeight(tapX, parity.x) * postEffectsUpsampleWeight(tapY, parity.y));
        }
    }
    return sum / float(kPostEffectsUpsampleWeightSum * kPostEffectsUpsampleWeightSum);
}

// Exposure, lift/gamma/gain, saturation, contrast, in that order. The result
// is not clamped.
vec3 postEffectsGrade(vec3 colour) {
    colour *= gradeScalars.x;
    colour = max(gradeGain.rgb * colour + gradeLift.rgb * (vec3(1.0) - colour), vec3(0.0));
    colour = pow(colour, gradeInverseGamma.rgb);
    float luma = dot(colour, kPostEffectsLumaWeights);
    colour = mix(vec3(luma), colour, gradeScalars.y);
    return (colour - vec3(0.5)) * gradeScalars.z + vec3(0.5);
}
