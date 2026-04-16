#ifndef IR_TIME_H
#define IR_TIME_H

#include <irreden/time/ir_time_types.hpp>

namespace IRTime {

/// Global pointer to the active `TimeManager`; managed by the engine runtime.
/// Prefer @ref getTimeManager() for safe access.
extern TimeManager *g_timeManager;
/// Returns a reference to the active `TimeManager`. Asserts if not initialised.
TimeManager &getTimeManager();

/// Returns the actual wall-clock dt (seconds) for the last tick of @p eventType.
/// Note: this is wall-clock time, **not** a fixed step — do not use it to advance
/// deterministic simulation state.  For that, use the fixed-step accumulator pattern
/// (see `shouldUpdate()`).
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
