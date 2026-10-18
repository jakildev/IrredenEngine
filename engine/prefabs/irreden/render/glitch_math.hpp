#ifndef GLITCH_MATH_H
#define GLITCH_MATH_H

#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/render/components/component_glitch_state.hpp>

#include <array>
#include <cstdint>
#include <limits>

// The deterministic contract every glitch pass shares: the tick clock, the
// integer hash ABI, and the per-pixel effect plan. The shader twins
// `ir_glitch_common.glsl` / `ir_glitch_common.metal` implement the same
// functions with the same constants; this header is their CPU mirror, so a
// headless test can pin hash vectors, band/cell selection and effect order.
//
// Integer ABI: seeds, effect ids, lane ids and hash outputs are uint32; a tick
// is a signed 64-bit integer carried as two uint32 words (two's-complement low
// and high halves). Every multiply wraps modulo 2^32 and every right shift is
// logical; signed arithmetic never reaches the hash. The CPU computes ticks
// once per frame and uploads the words, so no shader divides floating-point
// time and the backends cannot disagree at a tick boundary.
namespace IRPrefab::Glitch {

// Effect ids, the `effect` input of `glitchHash`.
inline constexpr std::uint32_t kEffectTear = 0u;
inline constexpr std::uint32_t kEffectHold = 1u;
inline constexpr std::uint32_t kEffectSplit = 2u;
inline constexpr std::uint32_t kEffectBars = 3u;
inline constexpr std::uint32_t kEffectCells = 4u;
inline constexpr std::uint32_t kEffectDepth = 5u;

// Lane ids within an effect. Bands take two lanes each (center, half-height)
// and bars two (row, colour) from their base.
inline constexpr std::uint32_t kLaneBandBase = 0u;
inline constexpr std::uint32_t kLaneTearOffsetBase = 16u;
inline constexpr std::uint32_t kLaneCellSize = 32u;
inline constexpr std::uint32_t kLaneCellBase = 33u;
inline constexpr std::uint32_t kLaneSplitSign = 48u;
inline constexpr std::uint32_t kLaneBarCount = 64u;
inline constexpr std::uint32_t kLaneBarBase = 65u;
inline constexpr std::uint32_t kLaneDepthMode = 80u;
inline constexpr std::uint32_t kLaneDepthProbe = 81u;
inline constexpr std::uint32_t kLaneDepthWidth = 82u;
inline constexpr std::uint32_t kLaneDepthOffset = 83u;

inline constexpr int kMaxBands = 6;
inline constexpr int kMaxBandHalfRows = 6;
inline constexpr int kMaxTearTrixels = 24;
inline constexpr int kMaxSplitTrixels = 3;
inline constexpr int kCellTrixelsMin = 4;
inline constexpr int kCellTrixelsSpan = 13;
inline constexpr int kMaxBars = 3;
inline constexpr int kBarPaletteSize = 4;
inline constexpr int kDepthProbeGrid = 4;
inline constexpr int kDepthProbeCount = kDepthProbeGrid * kDepthProbeGrid;
inline constexpr int kDepthHalfWidthMinSteps = 4;
inline constexpr int kDepthHalfWidthSpanSteps = 13;
// Composite-distance units per iso step at subdivision 1 (`iso * 8 + face`).
inline constexpr int kDepthUnitsPerStep = 8;
// Width of the composite-distance range the depth attachment normalizes.
inline constexpr float kDepthRangeUnits =
    static_cast<float>(IRConstants::kTrixelDistanceMaxDistance) -
    static_cast<float>(IRConstants::kTrixelDistanceMinDistance);

static_assert(
    kLaneBandBase + 2u * kMaxBands <= kLaneTearOffsetBase,
    "band lanes overlap the tear-offset lanes"
);
static_assert(
    kLaneTearOffsetBase + kMaxBands <= kLaneCellSize, "tear-offset lanes overlap the cell-size lane"
);
static_assert(kLaneBarBase + 2u * kMaxBars <= kLaneDepthMode, "bar lanes overlap the depth lanes");
static_assert(kCellTrixelsMin + kCellTrixelsSpan - 1 == 16, "cells span 4..16 trixels");

inline constexpr std::uint32_t kHashMultiplierA = 0x9E3779B1u;
inline constexpr std::uint32_t kHashMultiplierB = 0x85EBCA77u;
inline constexpr std::uint32_t kHashMultiplierC = 0xC2B2AE3Du;
inline constexpr std::uint32_t kCellHashX = 0x8DA6B343u;
inline constexpr std::uint32_t kCellHashY = 0xD8163841u;

// Bit-identical to `hash3` in `ir_iso_common.{glsl,metal}`.
constexpr std::uint32_t hash3(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    std::uint32_t h = a * kHashMultiplierA;
    h = (h ^ b) * kHashMultiplierB;
    h = (h ^ c) * kHashMultiplierC;
    h ^= h >> 16;
    h *= kHashMultiplierB;
    h ^= h >> 13;
    h *= kHashMultiplierC;
    h ^= h >> 16;
    return h;
}

struct TickWords {
    std::uint32_t low_ = 0u;
    std::uint32_t high_ = 0u;
};

constexpr TickWords toTickWords(std::int64_t tick) {
    const auto bits = static_cast<std::uint64_t>(tick);
    return {static_cast<std::uint32_t>(bits), static_cast<std::uint32_t>(bits >> 32)};
}

constexpr std::int64_t fromTickWords(TickWords words) {
    return static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(words.high_) << 32) | static_cast<std::uint64_t>(words.low_)
    );
}

constexpr std::uint32_t
glitchHash(std::uint32_t seed, TickWords tick, std::uint32_t effect, std::uint32_t lane) {
    return hash3(hash3(seed, tick.low_, tick.high_), effect, lane);
}

// The engine's fixed-step clock in seconds: every tick, phase and period is
// measured on it, never on wall-clock time.
inline double engineTimeSeconds() {
    return static_cast<double>(IRTime::tick()) / static_cast<double>(IRConstants::kFPS);
}

// 2^63 is exact in double; a quotient q converts to int64 iff -2^63 <= q < 2^63.
inline constexpr double kTickLimit = 9223372036854775808.0;

inline double tickQuotient(double timeSeconds, double periodSeconds, double phaseSeconds) {
    return IRMath::floor((timeSeconds - phaseSeconds) / periodSeconds);
}

inline bool isRepresentableTick(double quotient) {
    return IRMath::isFinite(quotient) && quotient >= -kTickLimit && quotient < kTickLimit;
}

// floor((time - phase) / period). Saturates instead of converting an
// unrepresentable quotient: a stored state passed validation at its write time,
// but a tiny period can still outgrow int64 as the clock advances, and a
// saturated tick merely stops re-rolling.
inline std::int64_t tickIndex(double timeSeconds, double periodSeconds, double phaseSeconds) {
    const double quotient = tickQuotient(timeSeconds, periodSeconds, phaseSeconds);
    if (!(quotient >= -kTickLimit)) {
        return std::numeric_limits<std::int64_t>::min();
    }
    if (quotient >= kTickLimit) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(quotient);
}

inline double holdPeriodSeconds(const IRComponents::C_GlitchState &state) {
    return state.periodSeconds_ / static_cast<double>(state.holdDivisor_);
}

// Engine-clock snapshot of one state: the period tick every random choice
// keys on, the hold tick the history snapshot follows, and the bar envelope
// (1 at a period tick, falling toward 0 just before the next).
struct Clock {
    double timeSeconds_ = 0.0;
    std::int64_t tick_ = 0;
    std::int64_t holdTick_ = 0;
    float barEnvelope_ = 1.0f;
};

inline Clock clockAt(const IRComponents::C_GlitchState &state, double timeSeconds) {
    Clock clock;
    clock.timeSeconds_ = timeSeconds;
    clock.tick_ = tickIndex(timeSeconds, state.periodSeconds_, state.phaseSeconds_);
    clock.holdTick_ = tickIndex(timeSeconds, holdPeriodSeconds(state), state.phaseSeconds_);
    const double cycles = (timeSeconds - state.phaseSeconds_) / state.periodSeconds_;
    const double envelope = 1.0 - (cycles - IRMath::floor(cycles));
    clock.barEnvelope_ =
        IRMath::isFinite(envelope) ? static_cast<float>(IRMath::clamp(envelope, 0.0, 1.0)) : 1.0f;
    return clock;
}

inline bool isEnabled(const IRComponents::C_GlitchState &state) {
    return state.intensity_ > 0.0f &&
           (state.tearWeight_ > 0.0f || state.holdWeight_ > 0.0f || state.splitWeight_ > 0.0f ||
            state.barsWeight_ > 0.0f || state.cellsWeight_ > 0.0f || state.depthWeight_ > 0.0f);
}

// One frame's shader inputs, in framebuffer texel units. Texel rows have the
// same orientation on both backends, so no field carries a backend flip.
struct FrameParams {
    std::uint32_t seed_ = 0u;
    TickWords tick_{};
    float intensity_ = 0.0f;
    float tear_ = 0.0f;
    float hold_ = 0.0f;
    float split_ = 0.0f;
    float bars_ = 0.0f;
    float cells_ = 0.0f;
    float depth_ = 0.0f;
    float barEnvelope_ = 1.0f;
    IRMath::ivec2 trixelStep_ = IRMath::ivec2(1);
    IRMath::ivec2 framebufferSize_ = IRMath::ivec2(1);
};

inline FrameParams frameParams(
    const IRComponents::C_GlitchState &state,
    const Clock &clock,
    IRMath::ivec2 trixelStep,
    IRMath::ivec2 framebufferSize
) {
    FrameParams params;
    params.seed_ = state.seed_;
    params.tick_ = toTickWords(clock.tick_);
    params.intensity_ = state.intensity_;
    params.tear_ = state.tearWeight_;
    params.hold_ = state.holdWeight_;
    params.split_ = state.splitWeight_;
    params.bars_ = state.barsWeight_;
    params.cells_ = state.cellsWeight_;
    params.depth_ = state.depthWeight_;
    params.barEnvelope_ = clock.barEnvelope_;
    params.trixelStep_ = IRMath::max(trixelStep, IRMath::ivec2(1));
    params.framebufferSize_ = IRMath::max(framebufferSize, IRMath::ivec2(1));
    return params;
}

// Whole framebuffer pixels per trixel at a camera zoom: a trixel is two
// game pixels wide and one tall at zoom 1. Rounded so a fractional
// continuous zoom still displaces by whole pixels.
inline IRMath::ivec2 trixelStepForZoom(IRMath::vec2 zoom) {
    const IRMath::vec2 step = IRMath::calcTriangleStepSizeGameResolution(zoom);
    return IRMath::max(
        IRMath::ivec2(IRMath::roundHalfUp(step.x), IRMath::roundHalfUp(step.y)),
        IRMath::ivec2(1)
    );
}

// ---- Frame tables and the per-pixel plan the shader twins mirror ----------

inline int bandCount(float strength) {
    if (strength <= 0.0f) {
        return 0;
    }
    return IRMath::min(kMaxBands, static_cast<int>(IRMath::ceil(kMaxBands * strength)));
}

inline int maxOffsetTrixels(float strength, int limit) {
    return IRMath::max(1, static_cast<int>(IRMath::ceil(static_cast<float>(limit) * strength)));
}

// A nonzero signed offset in [-maxOffset, maxOffset].
constexpr int signedOffset(std::uint32_t hash, int maxOffset) {
    const int offset =
        static_cast<int>(hash % static_cast<std::uint32_t>(2 * maxOffset + 1)) - maxOffset;
    return offset == 0 ? maxOffset : offset;
}

constexpr int cellTrixels(std::uint32_t seed, TickWords tick) {
    return kCellTrixelsMin + static_cast<int>(
                                 glitchHash(seed, tick, kEffectCells, kLaneCellSize) %
                                 static_cast<std::uint32_t>(kCellTrixelsSpan)
                             );
}

constexpr std::uint32_t cellLane(IRMath::ivec2 cell) {
    return kLaneCellBase ^ (static_cast<std::uint32_t>(cell.x) * kCellHashX) ^
           (static_cast<std::uint32_t>(cell.y) * kCellHashY);
}

// One of the eight neighbours of a cell, row-major from (-1, -1), skipping
// the cell itself.
constexpr IRMath::ivec2 cellNeighbour(std::uint32_t index) {
    const int slot = static_cast<int>(index % 8u);
    const int k = slot < 4 ? slot : slot + 1;
    return IRMath::ivec2(k % 3 - 1, k / 3 - 1);
}

enum class DepthMode : std::uint32_t {
    TEAR = 0,
    HOLD = 1,
};

constexpr DepthMode depthMode(std::uint32_t seed, TickWords tick) {
    return static_cast<DepthMode>(glitchHash(seed, tick, kEffectDepth, kLaneDepthMode) & 1u);
}

constexpr IRMath::ivec2 depthProbePixel(int probe, IRMath::ivec2 framebufferSize) {
    return IRMath::ivec2(
        (2 * (probe % kDepthProbeGrid) + 1) * framebufferSize.x / (2 * kDepthProbeGrid),
        (2 * (probe / kDepthProbeGrid) + 1) * framebufferSize.y / (2 * kDepthProbeGrid)
    );
}

inline float depthHalfWidthNormalized(std::uint32_t seed, TickWords tick) {
    const int steps =
        kDepthHalfWidthMinSteps + static_cast<int>(
                                      glitchHash(seed, tick, kEffectDepth, kLaneDepthWidth) %
                                      static_cast<std::uint32_t>(kDepthHalfWidthSpanSteps)
                                  );
    return static_cast<float>(steps * kDepthUnitsPerStep) / kDepthRangeUnits;
}

// Saturated additive colours a bar row picks from.
inline IRMath::vec3 barColour(int index) {
    constexpr IRMath::vec3 kPalette[kBarPaletteSize] = {
        {1.0f, 0.1f, 0.9f},
        {0.1f, 1.0f, 1.0f},
        {1.0f, 0.9f, 0.1f},
        {0.3f, 1.0f, 0.2f},
    };
    return kPalette[index % kBarPaletteSize];
}

// Rows [start_, end_) of one band, in trixel rows (cell rows for cells), and
// the band's x shift in framebuffer pixels where the effect has one.
struct BandSpan {
    int start_ = 0;
    int end_ = 0;
    int offsetPx_ = 0;
};

// Every choice that is uniform across a frame, evaluated once on the CPU
// through the hash ABI above and uploaded, so the fragment does compares
// instead of re-hashing per pixel. Only the per-cell hash and the depth
// probe (which reads the GPU composite) stay per fragment.
struct FrameTables {
    std::uint32_t seed_ = 0u;
    TickWords tick_{};
    IRMath::ivec2 trixelStep_ = IRMath::ivec2(1);
    IRMath::ivec2 framebufferSize_ = IRMath::ivec2(1);
    std::array<BandSpan, kMaxBands> tear_{};
    std::array<BandSpan, kMaxBands> hold_{};
    std::array<BandSpan, kMaxBands> cells_{};
    int tearCount_ = 0;
    int holdCount_ = 0;
    int cellCount_ = 0;
    IRMath::ivec2 cellPx_ = IRMath::ivec2(1);
    // A cell in a cells band moves when the low 16 bits of its hash fall
    // below this.
    std::uint32_t cellThreshold_ = 0u;
    bool depthEnabled_ = false;
    DepthMode depthMode_ = DepthMode::TEAR;
    int depthProbeStart_ = 0;
    float depthHalfWidth_ = 0.0f;
    int depthTearPx_ = 0;
    float depthHoldBlend_ = 0.0f;
    int splitOffsetPx_ = 0;
    int barCount_ = 0;
    std::array<int, kMaxBars> barRows_{};
    std::array<int, kMaxBars> barColours_{};
    // Bar brightness, including intensity and the within-period envelope.
    float barStrength_ = 0.0f;
};

inline BandSpan
bandSpan(std::uint32_t seed, TickWords tick, std::uint32_t effect, int band, int rowCount) {
    const auto lane = kLaneBandBase + 2u * static_cast<std::uint32_t>(band);
    const int center = static_cast<int>(
        glitchHash(seed, tick, effect, lane) % static_cast<std::uint32_t>(rowCount)
    );
    const int halfRows = 1 + static_cast<int>(
                                 glitchHash(seed, tick, effect, lane + 1u) %
                                 static_cast<std::uint32_t>(kMaxBandHalfRows)
                             );
    return {center - halfRows, center + halfRows, 0};
}

inline FrameTables frameTables(const FrameParams &params) {
    FrameTables tables;
    tables.seed_ = params.seed_;
    tables.tick_ = params.tick_;
    tables.trixelStep_ = params.trixelStep_;
    tables.framebufferSize_ = params.framebufferSize_;
    const std::uint32_t seed = params.seed_;
    const TickWords tick = params.tick_;
    const int rowCount = IRMath::max(1, params.framebufferSize_.y / params.trixelStep_.y);

    const float tearStrength = params.intensity_ * params.tear_;
    tables.tearCount_ = bandCount(tearStrength);
    for (int band = 0; band < tables.tearCount_; ++band) {
        tables.tear_[band] = bandSpan(seed, tick, kEffectTear, band, rowCount);
        const std::uint32_t hash = glitchHash(
            seed,
            tick,
            kEffectTear,
            kLaneTearOffsetBase + static_cast<std::uint32_t>(band)
        );
        tables.tear_[band].offsetPx_ =
            signedOffset(hash, maxOffsetTrixels(tearStrength, kMaxTearTrixels)) *
            params.trixelStep_.x;
    }

    tables.holdCount_ = bandCount(params.intensity_ * params.hold_);
    for (int band = 0; band < tables.holdCount_; ++band) {
        tables.hold_[band] = bandSpan(seed, tick, kEffectHold, band, rowCount);
    }

    const float cellStrength = params.intensity_ * params.cells_;
    tables.cellPx_ = params.trixelStep_ * cellTrixels(seed, tick);
    tables.cellCount_ = bandCount(cellStrength);
    const int cellRowCount = IRMath::max(1, params.framebufferSize_.y / tables.cellPx_.y);
    for (int band = 0; band < tables.cellCount_; ++band) {
        tables.cells_[band] = bandSpan(seed, tick, kEffectCells, band, cellRowCount);
    }
    tables.cellThreshold_ = static_cast<std::uint32_t>(65536.0f * cellStrength);

    const float depthStrength = params.intensity_ * params.depth_;
    tables.depthEnabled_ = depthStrength > 0.0f;
    tables.depthMode_ = depthMode(seed, tick);
    tables.depthProbeStart_ = static_cast<int>(
        glitchHash(seed, tick, kEffectDepth, kLaneDepthProbe) %
        static_cast<std::uint32_t>(kDepthProbeCount)
    );
    tables.depthHalfWidth_ = depthHalfWidthNormalized(seed, tick);
    if (tables.depthEnabled_) {
        if (tables.depthMode_ == DepthMode::TEAR) {
            const std::uint32_t hash = glitchHash(seed, tick, kEffectDepth, kLaneDepthOffset);
            tables.depthTearPx_ =
                signedOffset(hash, maxOffsetTrixels(depthStrength, kMaxTearTrixels)) *
                params.trixelStep_.x;
        } else {
            tables.depthHoldBlend_ = IRMath::min(depthStrength, 1.0f);
        }
    }

    const float splitStrength = params.intensity_ * params.split_;
    if (splitStrength > 0.0f) {
        const int sign = (glitchHash(seed, tick, kEffectSplit, kLaneSplitSign) & 1u) != 0u ? 1 : -1;
        tables.splitOffsetPx_ =
            sign * maxOffsetTrixels(splitStrength, kMaxSplitTrixels) * params.trixelStep_.x;
    }

    const float barStrength = params.intensity_ * params.bars_;
    if (barStrength > 0.0f) {
        tables.barCount_ = 1 + static_cast<int>(
                                   glitchHash(seed, tick, kEffectBars, kLaneBarCount) %
                                   static_cast<std::uint32_t>(kMaxBars)
                               );
        for (int bar = 0; bar < tables.barCount_; ++bar) {
            const auto lane = kLaneBarBase + 2u * static_cast<std::uint32_t>(bar);
            tables.barRows_[bar] = static_cast<int>(
                glitchHash(seed, tick, kEffectBars, lane) % static_cast<std::uint32_t>(rowCount)
            );
            tables.barColours_[bar] = static_cast<int>(
                glitchHash(seed, tick, kEffectBars, lane + 1u) %
                static_cast<std::uint32_t>(kBarPaletteSize)
            );
        }
        tables.barStrength_ = barStrength * params.barEnvelope_;
    }
    return tables;
}

// Index of the first band in @p bands[0, count) containing @p row, or -1.
inline int firstSpanOfRow(const std::array<BandSpan, kMaxBands> &bands, int count, int row) {
    for (int band = 0; band < count; ++band) {
        if (row >= bands[band].start_ && row < bands[band].end_) {
            return band;
        }
    }
    return -1;
}

// The composite-depth interval the depth effect selects this tick: centred on
// the first non-background probe of a 4x4 grid, scanned from a hashed start.
// Background is the cleared far depth, 1.0.
struct DepthInterval {
    bool valid_ = false;
    int probe_ = -1;
    float center_ = 1.0f;
    float halfWidth_ = 0.0f;
};

template <typename DepthAt>
DepthInterval selectDepthInterval(const FrameTables &tables, DepthAt &&depthAt) {
    DepthInterval interval;
    for (int i = 0; i < kDepthProbeCount; ++i) {
        const int probe = (tables.depthProbeStart_ + i) % kDepthProbeCount;
        const float depth = depthAt(depthProbePixel(probe, tables.framebufferSize_));
        if (depth < 1.0f) {
            interval.valid_ = true;
            interval.probe_ = probe;
            interval.center_ = depth;
            interval.halfWidth_ = tables.depthHalfWidth_;
            return interval;
        }
    }
    return interval;
}

template <typename DepthAt>
DepthInterval selectDepthInterval(const FrameParams &params, DepthAt &&depthAt) {
    return selectDepthInterval(frameTables(params), depthAt);
}

inline bool inDepthInterval(const DepthInterval &interval, float depth) {
    return interval.valid_ && depth < 1.0f &&
           IRMath::abs(depth - interval.center_) <= interval.halfWidth_;
}

// What the fragment for one destination texel samples and adds. Composition
// order is fixed: tear, then cells, then depth tear move the source
// coordinate; hold then depth hold choose the image it reads; split offsets
// the red and blue reads; bars add last.
struct SamplePlan {
    IRMath::ivec2 source_ = IRMath::ivec2(0);
    bool holdBand_ = false;
    float depthHoldBlend_ = 0.0f;
    int splitOffsetPx_ = 0;
    IRMath::vec3 barAdd_ = IRMath::vec3(0.0f);
};

template <typename DepthAt>
SamplePlan samplePlan(const FrameTables &tables, IRMath::ivec2 pixel, DepthAt &&depthAt) {
    const int row = pixel.y / tables.trixelStep_.y;
    SamplePlan plan;
    IRMath::ivec2 source = pixel;

    const int tearBand = firstSpanOfRow(tables.tear_, tables.tearCount_, row);
    if (tearBand >= 0) {
        source.x += tables.tear_[tearBand].offsetPx_;
    }

    if (tables.cellCount_ > 0 &&
        firstSpanOfRow(tables.cells_, tables.cellCount_, pixel.y / tables.cellPx_.y) >= 0) {
        const IRMath::ivec2 cell = pixel / tables.cellPx_;
        const std::uint32_t hash =
            glitchHash(tables.seed_, tables.tick_, kEffectCells, cellLane(cell));
        if ((hash & 0xFFFFu) < tables.cellThreshold_) {
            source += cellNeighbour(hash >> 16) * tables.cellPx_;
        }
    }

    if (tables.depthEnabled_) {
        const DepthInterval interval = selectDepthInterval(tables, depthAt);
        if (inDepthInterval(interval, depthAt(pixel))) {
            source.x += tables.depthTearPx_;
            plan.depthHoldBlend_ = tables.depthHoldBlend_;
        }
    }
    plan.source_ =
        IRMath::clamp(source, IRMath::ivec2(0), tables.framebufferSize_ - IRMath::ivec2(1));

    plan.holdBand_ = firstSpanOfRow(tables.hold_, tables.holdCount_, row) >= 0;
    plan.splitOffsetPx_ = tables.splitOffsetPx_;
    for (int bar = 0; bar < tables.barCount_; ++bar) {
        if (tables.barRows_[bar] == row) {
            plan.barAdd_ += barColour(tables.barColours_[bar]) * tables.barStrength_;
        }
    }
    return plan;
}

template <typename DepthAt>
SamplePlan samplePlan(const FrameParams &params, IRMath::ivec2 pixel, DepthAt &&depthAt) {
    return samplePlan(frameTables(params), pixel, depthAt);
}

} // namespace IRPrefab::Glitch

#endif /* GLITCH_MATH_H */
