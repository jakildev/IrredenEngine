// Shared tick / hash ABI for every glitch pass. Twin of
// ../ir_glitch_common.glsl; CPU mirror and contract in
// engine/prefabs/irreden/render/glitch_math.hpp, whose constants must match
// these exactly. Ticks arrive as two uint words (two's-complement low, high)
// computed on the CPU; no shader divides floating-point time. Choices that are
// uniform across a frame (bands, offsets, bar rows) are evaluated on the CPU
// through the same ABI and uploaded; only per-texel choices hash here.

#ifndef IR_GLITCH_COMMON_METAL_INCLUDED
#define IR_GLITCH_COMMON_METAL_INCLUDED

#include <metal_stdlib>
using namespace metal;

#include "ir_iso_common.metal"

constant uint kGlitchEffectTear = 0u;
constant uint kGlitchEffectHold = 1u;
constant uint kGlitchEffectSplit = 2u;
constant uint kGlitchEffectBars = 3u;
constant uint kGlitchEffectCells = 4u;
constant uint kGlitchEffectDepth = 5u;

constant uint kGlitchLaneBandBase = 0u;
constant uint kGlitchLaneTearOffsetBase = 16u;
constant uint kGlitchLaneCellSize = 32u;
constant uint kGlitchLaneCellBase = 33u;
constant uint kGlitchLaneSplitSign = 48u;
constant uint kGlitchLaneBarCount = 64u;
constant uint kGlitchLaneBarBase = 65u;
constant uint kGlitchLaneDepthMode = 80u;
constant uint kGlitchLaneDepthProbe = 81u;
constant uint kGlitchLaneDepthWidth = 82u;
constant uint kGlitchLaneDepthOffset = 83u;

constant int kGlitchMaxBands = 6;
constant int kGlitchMaxBandHalfRows = 6;
constant int kGlitchMaxBars = 3;
constant int kGlitchBarPaletteSize = 4;
constant int kGlitchDepthProbeGrid = 4;
constant int kGlitchDepthProbeCount = 16;

constant uint kGlitchCellHashX = 0x8DA6B343u;
constant uint kGlitchCellHashY = 0xD8163841u;

// Canvas-space subject glitch (TRIXEL_GLITCH); CPU mirror in
// trixel_glitch_math.hpp.
constant uint kGlitchEffectSubjectTear = 6u;
constant uint kGlitchEffectSubjectHold = 7u;
constant uint kGlitchEffectSubjectDissolve = 8u;
constant uint kGlitchEffectSubjectPair = 9u;
constant uint kGlitchSubjectTableTag = 0x2C1B3C6Du;
constant uint kGlitchPairModeCount = 3u;
constant int kGlitchSubjectMaxSpill = 4;
constant int kGlitchSubjectHalo = 5;
constant int kGlitchSubjectTileSize = 26;
constant uint kGlitchSubjectRecordWords = 5u;
constant int kGlitchSubjectEmptyDistance = 65535;
constant int kGlitchSubjectTintPaletteSize = 4;

inline uint glitchHash(uint seed, uint2 tickWords, uint effect, uint lane) {
    return hash3(hash3(seed, tickWords.x, tickWords.y), effect, lane);
}

// Rows [x, y) of band `band` of `effect` among `rowCount` rows.
inline int2 glitchBandSpan(uint seed, uint2 tickWords, uint effect, int band, int rowCount) {
    uint lane = kGlitchLaneBandBase + 2u * uint(band);
    int center = int(glitchHash(seed, tickWords, effect, lane) % uint(rowCount));
    int halfRows = 1 + int(glitchHash(seed, tickWords, effect, lane + 1u) % uint(kGlitchMaxBandHalfRows));
    return int2(center - halfRows, center + halfRows);
}

inline uint glitchCellLane(int2 cell) {
    return kGlitchLaneCellBase ^ (uint(cell.x) * kGlitchCellHashX) ^ (uint(cell.y) * kGlitchCellHashY);
}

// One of the eight neighbours of a cell, row-major from (-1, -1), skipping
// the cell itself.
inline int2 glitchCellNeighbour(uint index) {
    int slot = int(index % 8u);
    int k = slot < 4 ? slot : slot + 1;
    return int2(k % 3 - 1, k / 3 - 1);
}

inline int2 glitchDepthProbePixel(int probe, int2 framebufferSize) {
    return int2(
        (2 * (probe % kGlitchDepthProbeGrid) + 1) * framebufferSize.x / (2 * kGlitchDepthProbeGrid),
        (2 * (probe / kGlitchDepthProbeGrid) + 1) * framebufferSize.y / (2 * kGlitchDepthProbeGrid)
    );
}

inline float3 glitchBarColour(int index) {
    int slot = index % kGlitchBarPaletteSize;
    if (slot == 0) {
        return float3(1.0, 0.1, 0.9);
    }
    if (slot == 1) {
        return float3(0.1, 1.0, 1.0);
    }
    if (slot == 2) {
        return float3(1.0, 0.9, 0.1);
    }
    return float3(0.3, 1.0, 0.2);
}

// A nonzero signed offset in [-maxOffset, maxOffset].
inline int glitchSignedOffset(uint hash, int maxOffset) {
    int offset = int(hash % uint(2 * maxOffset + 1)) - maxOffset;
    return offset == 0 ? maxOffset : offset;
}

// Additive tints the subject echo copies take, as 8-bit channels.
inline uint4 glitchSubjectTint(int index) {
    int slot = index % kGlitchSubjectTintPaletteSize;
    if (slot == 0) {
        return uint4(255u, 64u, 214u, 255u);
    }
    if (slot == 1) {
        return uint4(64u, 240u, 255u, 255u);
    }
    if (slot == 2) {
        return uint4(255u, 230u, 64u, 255u);
    }
    return uint4(120u, 255u, 90u, 255u);
}

#endif // IR_GLITCH_COMMON_METAL_INCLUDED
