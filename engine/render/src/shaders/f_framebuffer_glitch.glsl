#version 450 core

#include "ir_glitch_common.glsl"

// Period-quantised glitch over the composed main framebuffer, drawn into the
// pass's output image texel for texel for the blit and the recorder. Mirrors
// `IRPrefab::Glitch::samplePlan` (glitch_math.hpp) step for step over the
// per-frame tables the CPU uploads: tear, cells and depth tear move the source
// texel in whole trixels; hold and depth hold choose the image read; split
// offsets the red and blue reads; bars add last.

in vec2 TexelCoords;
flat in float DepthCenter;

out vec4 FragColor;

layout (binding = 0) uniform sampler2D mainColor;
layout (binding = 1) uniform sampler2D mainDepth;
layout (binding = 2) uniform sampler2D historyColor;

layout (std140, binding = 2) uniform FrameDataFramebufferGlitch {
    mat4 mvpMatrix;
    // Period tick low/high words, then hold tick low/high words.
    uvec4 tickWords;
    // seed, cell threshold, depth probe start, depth enabled.
    uvec4 hashInputs;
    // Framebuffer pixels per trixel (xy) and per cell (zw).
    ivec4 trixelStep;
    // Band counts: tear, hold, cells; then the bar count.
    ivec4 counts;
    // Split and depth-tear x offsets in framebuffer pixels.
    ivec4 offsets;
    // Depth-hold blend, depth half-width (normalized depth), bar strength.
    vec4 strengths;
    // Rows [x, y) of each band; tear bands carry their x shift in z.
    ivec4 tearBands[6];
    ivec4 holdBands[6];
    ivec4 cellBands[6];
    ivec4 barRows;
    ivec4 barColours;
};

float depthAt(ivec2 texel) {
    return texelFetch(mainDepth, texel, 0).r;
}

vec4 compositeAt(ivec2 texel, bool holdBand, float depthHoldBlend) {
    vec4 colour = holdBand ? texelFetch(historyColor, texel, 0) : texelFetch(mainColor, texel, 0);
    if (depthHoldBlend > 0.0) {
        colour = mix(colour, texelFetch(historyColor, texel, 0), depthHoldBlend);
    }
    return colour;
}

void main() {
    ivec2 size = textureSize(mainColor, 0);
    ivec2 maxTexel = size - ivec2(1);
    ivec2 pixel = clamp(ivec2(floor(TexelCoords)), ivec2(0), maxTexel);
    int row = pixel.y / trixelStep.y;
    ivec2 source = pixel;

    for (int band = 0; band < kGlitchMaxBands; ++band) {
        if (band >= counts.x) {
            break;
        }
        if (row >= tearBands[band].x && row < tearBands[band].y) {
            source.x += tearBands[band].z;
            break;
        }
    }

    if (counts.z > 0) {
        ivec2 cellPx = trixelStep.zw;
        int cellRow = pixel.y / cellPx.y;
        for (int band = 0; band < kGlitchMaxBands; ++band) {
            if (band >= counts.z) {
                break;
            }
            if (cellRow >= cellBands[band].x && cellRow < cellBands[band].y) {
                uint h = glitchHash(hashInputs.x, tickWords.xy, kGlitchEffectCells, glitchCellLane(pixel / cellPx));
                if ((h & 0xFFFFu) < hashInputs.y) {
                    source += glitchCellNeighbour(h >> 16) * cellPx;
                }
                break;
            }
        }
    }

    float depthHoldBlend = 0.0;
    if (hashInputs.w != 0u) {
        float center = DepthCenter;
        float pixelDepth = depthAt(pixel);
        if (center < 1.0 && pixelDepth < 1.0 && abs(pixelDepth - center) <= strengths.y) {
            source.x += offsets.y;
            depthHoldBlend = strengths.x;
        }
    }
    source = clamp(source, ivec2(0), maxTexel);

    bool holdBand = false;
    for (int band = 0; band < kGlitchMaxBands; ++band) {
        if (band >= counts.y) {
            break;
        }
        if (row >= holdBands[band].x && row < holdBands[band].y) {
            holdBand = true;
            break;
        }
    }

    vec4 colour = compositeAt(source, holdBand, depthHoldBlend);
    if (offsets.x != 0) {
        colour.r = compositeAt(clamp(source + ivec2(offsets.x, 0), ivec2(0), maxTexel), holdBand, depthHoldBlend).r;
        colour.b = compositeAt(clamp(source - ivec2(offsets.x, 0), ivec2(0), maxTexel), holdBand, depthHoldBlend).b;
    }

    for (int bar = 0; bar < kGlitchMaxBars; ++bar) {
        if (bar >= counts.w) {
            break;
        }
        if (row == barRows[bar]) {
            colour.rgb += glitchBarColour(barColours[bar]) * strengths.z;
        }
    }

    FragColor = vec4(clamp(colour.rgb, 0.0, 1.0), colour.a);
}
