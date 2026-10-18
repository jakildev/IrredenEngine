// Shared tick / hash ABI for every glitch pass. Twin of
// metal/ir_glitch_common.metal; CPU mirror and contract in
// engine/prefabs/irreden/render/glitch_math.hpp, whose constants must match
// these exactly. Ticks arrive as two uint words (two's-complement low, high)
// computed on the CPU; no shader divides floating-point time. Choices that are
// uniform across a frame (bands, offsets, bar rows) are evaluated on the CPU
// through the same ABI and uploaded; only per-texel choices hash here.

#include "ir_iso_common.glsl"

const uint kGlitchEffectTear = 0u;
const uint kGlitchEffectHold = 1u;
const uint kGlitchEffectSplit = 2u;
const uint kGlitchEffectBars = 3u;
const uint kGlitchEffectCells = 4u;
const uint kGlitchEffectDepth = 5u;

const uint kGlitchLaneBandBase = 0u;
const uint kGlitchLaneTearOffsetBase = 16u;
const uint kGlitchLaneCellSize = 32u;
const uint kGlitchLaneCellBase = 33u;
const uint kGlitchLaneSplitSign = 48u;
const uint kGlitchLaneBarCount = 64u;
const uint kGlitchLaneBarBase = 65u;
const uint kGlitchLaneDepthMode = 80u;
const uint kGlitchLaneDepthProbe = 81u;
const uint kGlitchLaneDepthWidth = 82u;
const uint kGlitchLaneDepthOffset = 83u;

const int kGlitchMaxBands = 6;
const int kGlitchMaxBandHalfRows = 6;
const int kGlitchMaxBars = 3;
const int kGlitchBarPaletteSize = 4;
const int kGlitchDepthProbeGrid = 4;
const int kGlitchDepthProbeCount = 16;

const uint kGlitchCellHashX = 0x8DA6B343u;
const uint kGlitchCellHashY = 0xD8163841u;

uint glitchHash(uint seed, uvec2 tickWords, uint effect, uint lane) {
    return hash3(hash3(seed, tickWords.x, tickWords.y), effect, lane);
}

// Rows [x, y) of band `band` of `effect` among `rowCount` rows.
ivec2 glitchBandSpan(uint seed, uvec2 tickWords, uint effect, int band, int rowCount) {
    uint lane = kGlitchLaneBandBase + 2u * uint(band);
    int center = int(glitchHash(seed, tickWords, effect, lane) % uint(rowCount));
    int halfRows = 1 + int(glitchHash(seed, tickWords, effect, lane + 1u) % uint(kGlitchMaxBandHalfRows));
    return ivec2(center - halfRows, center + halfRows);
}

uint glitchCellLane(ivec2 cell) {
    return kGlitchLaneCellBase ^ (uint(cell.x) * kGlitchCellHashX) ^ (uint(cell.y) * kGlitchCellHashY);
}

// One of the eight neighbours of a cell, row-major from (-1, -1), skipping
// the cell itself.
ivec2 glitchCellNeighbour(uint index) {
    int slot = int(index % 8u);
    int k = slot < 4 ? slot : slot + 1;
    return ivec2(k % 3 - 1, k / 3 - 1);
}

ivec2 glitchDepthProbePixel(int probe, ivec2 framebufferSize) {
    return ivec2(
        (2 * (probe % kGlitchDepthProbeGrid) + 1) * framebufferSize.x / (2 * kGlitchDepthProbeGrid),
        (2 * (probe / kGlitchDepthProbeGrid) + 1) * framebufferSize.y / (2 * kGlitchDepthProbeGrid)
    );
}

vec3 glitchBarColour(int index) {
    int slot = index % kGlitchBarPaletteSize;
    if (slot == 0) {
        return vec3(1.0, 0.1, 0.9);
    }
    if (slot == 1) {
        return vec3(0.1, 1.0, 1.0);
    }
    if (slot == 2) {
        return vec3(1.0, 0.9, 0.1);
    }
    return vec3(0.3, 1.0, 0.2);
}
