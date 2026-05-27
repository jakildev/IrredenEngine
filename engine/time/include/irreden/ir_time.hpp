#ifndef IR_TIME_H
#define IR_TIME_H

#include <irreden/time/ir_time_types.hpp>

namespace IRTime {

/// Global pointer to the active `TimeManager`; managed by the engine runtime.
/// Prefer @ref getTimeManager() for safe access.
extern TimeManager *g_timeManager;
/// Returns a reference to the active `TimeManager`. Asserts if not initialised.
TimeManager &getTimeManager();

/// Returns dt (seconds) for the last tick of @p eventType.
/// UPDATE: fixed step `1.0 / IRConstants::kFPS` (`const`-after-ctor; safe to read
/// from PARALLEL_FOR worker bodies). RENDER: actual wall-clock dt of the last tick —
/// use only for presentation-frame interpolation, not deterministic sim state.
/// RENDER dt is set before the RENDER pipeline executes and is immutable during the
/// pipeline run — also safe to read from PARALLEL_FOR worker bodies within the
/// RENDER pipeline.
double deltaTime(Events eventType);
/// Returns `true` when the UPDATE accumulator has buffered at least one frame period
/// (1 / @ref IRConstants::kFPS).  Call in a `while` loop to drain catch-up ticks.
bool shouldUpdate();

/// 1-second rolling average of RENDER ticks per second.
double renderFps();
/// Average RENDER frame time in milliseconds over the last second.
double renderFrameTimeMs();
/// 1-second rolling average of UPDATE ticks per second.
double updateFps();
/// Number of RENDER frames dropped since last @ref resetDroppedFrames call.
/// A frame is "dropped" when RENDER is ≥ 2 frame periods behind schedule.
unsigned int droppedFrames();
/// Resets the dropped-frame counter to zero.
void resetDroppedFrames();
} // namespace IRTime

#endif /* IR_TIME_H */
