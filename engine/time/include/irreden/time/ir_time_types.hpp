#ifndef IR_TIME_TYPES_H
#define IR_TIME_TYPES_H

#include <chrono>
#include <cstddef>
#include <irreden/ir_constants.hpp>

namespace IRTime {
class TimeManager;

/// Monotonic clock used for all engine timekeeping.
using Clock = std::chrono::steady_clock;
/// A point-in-time on the @ref Clock.
using TimePoint = std::chrono::time_point<Clock>;
/// Integer nanosecond duration (for accumulator arithmetic).
using NanoDuration = std::chrono::duration<int64_t, std::nano>;
/// Floating-point millisecond duration (for display / logging).
using MilliDuration = std::chrono::duration<double, std::milli>;
/// Floating-point seconds duration (for dt values).
using SecondsDuration = std::chrono::duration<double>;
using NanoSeconds = std::chrono::nanoseconds;
using MilliSeconds = std::chrono::milliseconds;
using Seconds = std::chrono::seconds;
/// Compile-time duration type representing one frame at @p fps frames per second.
template <intmax_t fps> using FramePeriod = std::chrono::duration<double, std::ratio<1, fps>>;
/// Duration of one frame at the engine's target frame rate (@ref IRConstants::kFPS).
constexpr FramePeriod<IRConstants::kFPS> kFPSFramePeriod{1};
/// Same as @ref kFPSFramePeriod in integer nanoseconds (for accumulator comparisons).
constexpr NanoDuration kFPSNanoDuration = std::chrono::duration_cast<NanoDuration>(kFPSFramePeriod);

/// Rolling history depth used by each `EventProfiler` (frames).
const unsigned int kProfileHistoryBufferSize = 100;
/// Capacity of the FPS rolling window inside `EventProfiler` (frames).
constexpr size_t kFpsWindowCapacity = 1024;

/// Pipeline event tags — used as template parameters to
/// `TimeManager::beginEvent<E>()` / `endEvent<E>()` and as arguments to
/// `deltaTime(E)`.  @ref UPDATE drives the fixed-step accumulator.
enum Events { UPDATE, RENDER, INPUT, START, END };

/// Per-event timing profiler; tracks accumulator, rolling history, FPS window,
/// and dropped-frame counter.  Instantiated inside `TimeManager`.
template <Events eventType> class EventProfiler;
} // namespace IRTime

#endif /* IR_TIME_TYPES_H */
