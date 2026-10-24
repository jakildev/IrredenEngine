#ifndef COMPONENT_PERF_HUD_H
#define COMPONENT_PERF_HUD_H

namespace IRComponents {

// Which GUI-canvas corner the perf HUD anchors to. Crosses no binding
// boundary today; declared as a closed enum so a Lua table can mirror it.
enum class PerfHudCorner : int { TOP_LEFT = 0, TOP_RIGHT = 1, BOTTOM_LEFT = 2, BOTTOM_RIGHT = 3 };

// Singleton row holding the perf HUD's visibility and anchor, read via
// `IRPrefab::PerfHud::` (`perf_hud_state.hpp`). World-scoped mutable state
// lives on a singleton component rather than a header global or a
// RenderManager field — see `.claude/rules/cpp-globals.md` and
// `engine/prefabs/irreden/render/CLAUDE.md` §"Exposing system public API".
//
// `Command<TOGGLE_PERF_HUD>` flips `visible_` — a plain value mutation, safe
// from a command callback firing mid input-system tick. `System<PERF_HUD>`
// reads the row once per frame and early-returns when hidden.
//
// Bottom-right by default: the help overlay draws from the top-left and the
// perf-stats overlay from the top-right, so the HUD never shares a corner
// with either in a creation that registers all three.
struct C_PerfHudState {
    bool visible_ = false;
    PerfHudCorner corner_ = PerfHudCorner::BOTTOM_RIGHT;
};

} // namespace IRComponents

#endif /* COMPONENT_PERF_HUD_H */
