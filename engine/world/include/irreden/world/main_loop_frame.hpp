#ifndef IR_WORLD_MAIN_LOOP_FRAME_H
#define IR_WORLD_MAIN_LOOP_FRAME_H

#include <cstdint>

namespace IREngine {

/// One main-loop frame: INPUT then UPDATE for every due tick, then RENDER.
/// `World::gameLoop` drives it with its own stages; tests drive it with stubs.
///
/// `Stages` provides `shouldUpdate()`, `pollEvents()`, `input()`,
/// `holdForFirstInput()` (true: skip this frame's remaining ticks),
/// `update()`, `render()`, and `halted()`. A stage that halts the loop (a
/// handoff that lost its GPU lease) stops the frame at the next check: no
/// later update and no render run, and the caller leaves the loop.
///
/// Returns the number of UPDATE ticks run.
template <typename Stages> std::uint32_t runMainLoopFrame(Stages &stages) {
    std::uint32_t updateTicks = 0;
    while (stages.shouldUpdate()) {
        stages.pollEvents();
        stages.input();
        if (stages.halted()) {
            return updateTicks;
        }
        if (stages.holdForFirstInput()) {
            break;
        }
        stages.update();
        ++updateTicks;
        if (stages.halted()) {
            return updateTicks;
        }
    }
    stages.render();
    return updateTicks;
}

} // namespace IREngine

#endif /* IR_WORLD_MAIN_LOOP_FRAME_H */
