#ifndef GPU_ENCODER_INTERVALS_H
#define GPU_ENCODER_INTERVALS_H

#include <irreden/ir_math.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace IRRender {

// One stage invocation's GPU time, in counter ticks (nanoseconds on Metal).
// `exclusiveTicks_` is the length of the union of the stage's own encoder
// intervals: time during which at least one of its encoders was executing.
// `windowTicks_` spans the earliest encoder start to the latest encoder end and
// includes other stages' work the GPU ran inside that window.
struct GpuEncoderIntervalResult {
    bool valid_ = false;
    // False when the stage created more encoders than its sample capacity:
    // the overflow encoders share the last pair, so only the window is known.
    bool exclusiveValid_ = false;
    std::uint64_t exclusiveTicks_ = 0;
    std::uint64_t windowTicks_ = 0;
};

// Sentinel a counter writes for a sample it could not take.
inline constexpr std::uint64_t kGpuCounterErrorValue = ~std::uint64_t{0};

struct GpuEncoderInterval {
    std::uint64_t start_ = 0;
    std::uint64_t end_ = 0;
};

// Resolves one stage invocation from its sample pairs. `samples` holds
// `min(encoderCount, capacity)` pairs as `[start0, end0, start1, end1, ...]`;
// encoders `0 .. capacity - 2` own a pair each and every later encoder shares
// pair `capacity - 1` (the first of them writes its start, each re-writes its
// end). `encoderCount` is the number of encoders the stage created. A sample is
// fresh when it is later than `previousBoundary`, the latest sample the pair set
// held before this invocation. A pair with neither sample fresh belongs to an
// encoder the GPU never executed (an empty encoder writes nothing) and is
// skipped. A pair with one stale, zero, error or reversed sample invalidates the
// whole invocation, as does an invocation with no executed encoder: a partial
// union would read as a cost lower than the stage's. `scratch` must hold at
// least `min(encoderCount, capacity)` entries.
inline GpuEncoderIntervalResult resolveGpuEncoderIntervals(
    std::span<const std::uint64_t> samples,
    std::size_t encoderCount,
    std::size_t capacity,
    std::uint64_t previousBoundary,
    std::span<GpuEncoderInterval> scratch
) {
    GpuEncoderIntervalResult result;
    const std::size_t pairCount = IRMath::min(encoderCount, capacity);
    if (pairCount == 0 || samples.size() < pairCount * 2 || scratch.size() < pairCount) {
        return result;
    }
    std::uint64_t earliest = kGpuCounterErrorValue;
    std::uint64_t latest = 0;
    std::size_t executed = 0;
    for (std::size_t i = 0; i < pairCount; ++i) {
        const std::uint64_t start = samples[i * 2];
        const std::uint64_t end = samples[i * 2 + 1];
        const bool startFresh = start != kGpuCounterErrorValue && start > previousBoundary;
        const bool endFresh = end != kGpuCounterErrorValue && end > previousBoundary;
        if (!startFresh && !endFresh && start != kGpuCounterErrorValue &&
            end != kGpuCounterErrorValue) {
            continue;
        }
        if (!startFresh || !endFresh || end < start) {
            return result;
        }
        scratch[executed++] = {start, end};
        earliest = IRMath::min(earliest, start);
        latest = IRMath::max(latest, end);
    }
    if (executed == 0) {
        return result;
    }
    result.valid_ = true;
    result.windowTicks_ = latest - earliest;
    result.exclusiveValid_ = encoderCount <= capacity;
    if (!result.exclusiveValid_) {
        return result;
    }

    const auto intervals = scratch.first(executed);
    std::sort(intervals.begin(), intervals.end(), [](const auto &a, const auto &b) {
        return a.start_ < b.start_;
    });
    std::uint64_t runStart = intervals[0].start_;
    std::uint64_t runEnd = intervals[0].end_;
    for (std::size_t i = 1; i < intervals.size(); ++i) {
        if (intervals[i].start_ > runEnd) {
            result.exclusiveTicks_ += runEnd - runStart;
            runStart = intervals[i].start_;
        }
        runEnd = IRMath::max(runEnd, intervals[i].end_);
    }
    result.exclusiveTicks_ += runEnd - runStart;
    return result;
}

} // namespace IRRender

#endif /* GPU_ENCODER_INTERVALS_H */
