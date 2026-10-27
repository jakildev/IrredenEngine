#include <metal_stdlib>
using namespace metal;

#include "ir_glitch_common.metal"

// Period-quantised glitch over the composed main framebuffer, drawn into the
// pass's output image texel for texel; the ordinary blit then places that
// image on screen. Mirrors shaders/v_framebuffer_glitch.glsl +
// f_framebuffer_glitch.glsl and `IRPrefab::Glitch::samplePlan`
// (glitch_math.hpp) step for step.

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoords [[attribute(1)]];
};

struct FrameDataFramebufferGlitch {
    float4x4 mvpMatrix;
    // Period tick low/high words, then hold tick low/high words.
    uint4 tickWords;
    // seed, cell threshold, depth probe start, depth enabled.
    uint4 hashInputs;
    // Framebuffer pixels per trixel (xy) and per cell (zw).
    int4 trixelStep;
    // Band counts: tear, hold, cells; then the bar count.
    int4 counts;
    // Split and depth-tear x offsets in framebuffer pixels.
    int4 offsets;
    // Depth-hold blend, depth half-width (normalized depth), bar strength.
    float4 strengths;
    // Rows [x, y) of each band; tear bands carry their x shift in z.
    int4 tearBands[6];
    int4 holdBands[6];
    int4 cellBands[6];
    int4 barRows;
    int4 barColours;
};

struct VertexOut {
    float4 position [[position]];
    // Main-framebuffer texel coordinates. Derived in the vertex stage from the
    // colour's size so that stage reads the composite too: a pass whose vertex
    // stage reads no texture can start before the composite finishes, and its
    // GPU timestamp then includes that wait.
    float2 texelCoords;
    // Centre of the depth effect's interval: the first non-background depth
    // among the probes, or 1.0 when every probe is background. Uniform across
    // the pass, so the probe scan runs per vertex rather than per fragment.
    float depthCenter [[flat]];
};

vertex VertexOut v_framebuffer_glitch(
    VertexIn in [[stage_in]],
    texture2d<float> mainColor [[texture(0)]],
    depth2d<float> mainDepth [[texture(1)]],
    constant FrameDataFramebufferGlitch &frameData [[buffer(2)]]
) {
    VertexOut out;
    const int2 size = int2(mainColor.get_width(), mainColor.get_height());
    out.position = frameData.mvpMatrix * float4(in.position, 1.0, 1.0);
    out.position.y = -out.position.y;
    // The output is the composite's twin, so the texel read is the texel
    // written: no origin flip here, unlike a stage that samples for the screen.
    out.texelCoords = in.texCoords * float2(size);
    out.depthCenter = 1.0f;
    if (frameData.hashInputs.w != 0u) {
        for (int i = 0; i < kGlitchDepthProbeCount; ++i) {
            const int probe = (int(frameData.hashInputs.z) + i) % kGlitchDepthProbeCount;
            const float probeDepth = mainDepth.read(uint2(glitchDepthProbePixel(probe, size)));
            if (probeDepth < 1.0f) {
                out.depthCenter = probeDepth;
                break;
            }
        }
    }
    return out;
}

static float4 glitchCompositeAt(
    texture2d<float> mainColor,
    texture2d<float> historyColor,
    int2 texel,
    bool holdBand,
    float depthHoldBlend
) {
    float4 colour = holdBand ? historyColor.read(uint2(texel)) : mainColor.read(uint2(texel));
    if (depthHoldBlend > 0.0f) {
        colour = mix(colour, historyColor.read(uint2(texel)), depthHoldBlend);
    }
    return colour;
}

fragment float4 f_framebuffer_glitch(
    VertexOut in [[stage_in]],
    texture2d<float> mainColor [[texture(0)]],
    depth2d<float> mainDepth [[texture(1)]],
    texture2d<float> historyColor [[texture(2)]],
    constant FrameDataFramebufferGlitch &frameData [[buffer(2)]]
) {
    const int2 size = int2(mainColor.get_width(), mainColor.get_height());
    const int2 maxTexel = size - int2(1);
    const int2 pixel = clamp(int2(floor(in.texelCoords)), int2(0), maxTexel);
    const int row = pixel.y / frameData.trixelStep.y;
    int2 source = pixel;

    for (int band = 0; band < kGlitchMaxBands; ++band) {
        if (band >= frameData.counts.x) {
            break;
        }
        if (row >= frameData.tearBands[band].x && row < frameData.tearBands[band].y) {
            source.x += frameData.tearBands[band].z;
            break;
        }
    }

    if (frameData.counts.z > 0) {
        const int2 cellPx = frameData.trixelStep.zw;
        const int cellRow = pixel.y / cellPx.y;
        for (int band = 0; band < kGlitchMaxBands; ++band) {
            if (band >= frameData.counts.z) {
                break;
            }
            if (cellRow >= frameData.cellBands[band].x && cellRow < frameData.cellBands[band].y) {
                const uint h = glitchHash(
                    frameData.hashInputs.x,
                    frameData.tickWords.xy,
                    kGlitchEffectCells,
                    glitchCellLane(pixel / cellPx)
                );
                if ((h & 0xFFFFu) < frameData.hashInputs.y) {
                    source += glitchCellNeighbour(h >> 16) * cellPx;
                }
                break;
            }
        }
    }

    float depthHoldBlend = 0.0f;
    if (frameData.hashInputs.w != 0u) {
        const float center = in.depthCenter;
        const float pixelDepth = mainDepth.read(uint2(pixel));
        if (center < 1.0f && pixelDepth < 1.0f &&
            abs(pixelDepth - center) <= frameData.strengths.y) {
            source.x += frameData.offsets.y;
            depthHoldBlend = frameData.strengths.x;
        }
    }
    source = clamp(source, int2(0), maxTexel);

    bool holdBand = false;
    for (int band = 0; band < kGlitchMaxBands; ++band) {
        if (band >= frameData.counts.y) {
            break;
        }
        if (row >= frameData.holdBands[band].x && row < frameData.holdBands[band].y) {
            holdBand = true;
            break;
        }
    }

    float4 colour = glitchCompositeAt(mainColor, historyColor, source, holdBand, depthHoldBlend);
    const int splitOffset = frameData.offsets.x;
    if (splitOffset != 0) {
        colour.r = glitchCompositeAt(
                       mainColor,
                       historyColor,
                       clamp(source + int2(splitOffset, 0), int2(0), maxTexel),
                       holdBand,
                       depthHoldBlend
                   )
                       .r;
        colour.b = glitchCompositeAt(
                       mainColor,
                       historyColor,
                       clamp(source - int2(splitOffset, 0), int2(0), maxTexel),
                       holdBand,
                       depthHoldBlend
                   )
                       .b;
    }

    for (int bar = 0; bar < kGlitchMaxBars; ++bar) {
        if (bar >= frameData.counts.w) {
            break;
        }
        if (row == frameData.barRows[bar]) {
            colour.rgb += glitchBarColour(frameData.barColours[bar]) * frameData.strengths.z;
        }
    }

    return float4(clamp(colour.rgb, float3(0.0f), float3(1.0f)), colour.a);
}
