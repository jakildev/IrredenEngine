<!--
Plan file for #2375 (per #1932 — committed as the first commit of the
implementer's PR). This is the `## Plan` comment posted on issue #2375 on
2026-07-20, granted architect approach sign-off on 2026-07-28 (`human:review-plan`
cleared in the human-cued architect triage session). Reproduced verbatim below;
the issue thread is the canonical source.

One divergence worth flagging for the next reader: the plan's `**Model:**` field
reads `sonnet`, but the issue carries the `fleet:opus` label. The label is
authoritative (`.claude/commands/role-worker.md` step 3 / the fleet-claim
model-tag gate), so this shipped as an opus-class task. The field is stale, not a
re-scope.
-->

## Plan: script: expose IRDebug immediate-mode overlay draws to Lua

- **Issue:** #2375
- **Model:** sonnet — bounded, precedent-following; every judgment call is committed below
- **Date:** 2026-07-20

### Scope

Bind the IRDebug buffering draw calls under an `IRDebug` Lua table so EVAL Lua systems spliced into RENDER before `DEBUG_OVERLAY` can issue debug-overlay draws exactly like C++ callers. No GL/Metal work crosses the Lua boundary — the draws only append to CPU vectors; the `System<DEBUG_OVERLAY>` flush keeps sole ownership of all GPU work.

### Verified current state (2026-07-20, master)

- The entire IRDebug surface lives in `engine/prefabs/irreden/render/systems/system_debug_overlay.hpp` (the `namespace IRDebug` block, lines 20–256): the buffering draws (`drawLine3D`, `drawCircle3D`, `drawTriangle3D`, `drawDiamond3D`, `drawPath3D`, `drawLineScreen`, `drawTriangleScreen`, `drawRectScreen`, `drawDotScreen`), the inline-static buffer accessors, `clear()`, `worldToScreen`/`screenToWorld`, and the flush constants. The draw bodies are pure `push_back` — no render calls — so the issue's "no context-affinity reason" premise is read-verified, not assumed.
- **No Lua binding exists.** `grep -rn IRDebug` over `engine/script/`, `test/`, and all `creations/**/*.lua` returns zero hits. The only callers today are the flush system itself and `system_debug_culling_minimap.hpp` (C++, screen-space variants). Gap claim confirmed across the full candidate set.
- `IR_BIND_SYS(DEBUG_OVERLAY)` already exists (`lua_pipeline_bindings.hpp:161`), so Lua pipeline composition can already spell `SystemName.DEBUG_OVERLAY` — no pipeline-binding change needed.
- Binding-block precedent: `detail::bindRenderGlue` (`lua_render_bindings.hpp`) and `detail::bindWidgets` (`lua_widget_bindings.hpp`), both invoked from `bindLuaDrivenEcs()` (`lua_script.cpp:726-727`), both extend-never-replace their tables. `lua_widget_bindings.hpp` already includes `engine/prefabs` render headers, so the script→prefabs include direction is established.
- Canonical arg helpers in `ir_script_utils.hpp`: `vec3FromLua` (userdata or `{x,y,z}`/`{1,2,3}` table), `ivec3FromLua`, `colorFromLua`, `quatFromLua`. No `vec2FromLua`/`vec4FromLua` yet.
- Reference pipeline placement: the default demo (`creations/demos/default/main_lua.cpp`) runs `DEBUG_OVERLAY` after `TRIXEL_TO_FRAMEBUFFER` and before `FRAMEBUFFER_TO_SCREEN` in RENDER.
- Test precedent: `test/script/lua_render_bindings_test.cpp` (headless LuaScript + EntityManager + SystemManager, no RenderManager) is presence-only because invoking render-glue needs a GPU. The IRDebug draws buffer CPU-side, so this surface can be **invoked** and buffer-asserted headlessly — a strictly stronger test shape is available here.
- Sibling/in-flight reconciliation: no open PR touches `engine/script/` or the debug overlay (checked the 7 open engine PRs 2026-07-20). Needs-plan sibling #2446 (Lua attach of codegen'd components) is script-side but orthogonal — no shared files beyond `lua_script.cpp` call-site adjacency.

No phase 0: no measurable-mechanism premise — the buffering-only claim is verified by reading the draw bodies.

### Approach

1. **Mechanical header split.** Move the whole `namespace IRDebug { ... }` block from `system_debug_overlay.hpp` into a new `engine/prefabs/irreden/render/debug_overlay_draws.hpp`; the system header includes it and keeps only the `System<DEBUG_OVERLAY>` specialization. In the moved code, qualify math types explicitly (`IRMath::vec3`) — the file-scope `using namespace IRMath;` stays behind in the system header — so the binding header doesn't inject the namespace into script TUs, and the script module doesn't drag in `buffer.hpp`/`shader.hpp`/`vao.hpp`. Zero behavior change.
2. **Add `vec2FromLua` and `vec4FromLua`** to `ir_script_utils.hpp`, mirroring `vec3FromLua` (userdata or keyed/indexed table; vec4 accepts `{x,y,z,w}` or `{r,g,b,a}` keys, or indices 1–4; zero-default per the existing helper contract). Extend the helper table in `engine/script/CLAUDE.md` §"C++ ↔ Lua math type helpers".
3. **New binding block** `engine/script/include/irreden/script/lua_debug_overlay_bindings.hpp` with `detail::bindDebugOverlay(LuaScript&)`. Creates the `IRDebug` table under the `if (!valid())` extend-never-replace guard, then binds thin forwards whose signatures mirror the C++ surface exactly — same names, same argument order, colors as separate 0..1 floats (NOT the 0-255 `colorFromLua` tables `IRGui` uses; parity with the C++ callers wins so C++ overlay code ports line-for-line):
   - `IRDebug.drawLine3D(from, to, r, g, b [, a])` — vec3s via `vec3FromLua`
   - `IRDebug.drawCircle3D(center, radius, r, g, b [, a [, segments]])`
   - `IRDebug.drawTriangle3D(a, b, c, r, g, b [, alpha])`
   - `IRDebug.drawDiamond3D(center, radius, r, g, b [, a])`
   - `IRDebug.drawPath3D(points, r, g, b [, a])` — `points` an array table; each element via `vec3FromLua`
   - `IRDebug.drawLineScreen(from, to, r, g, b [, a])` / `IRDebug.drawTriangleScreen(a, b, c, r, g, b [, alpha])` — vec2s via `vec2FromLua`
   - `IRDebug.drawRectScreen(min, max, fillColor, borderColor)` / `IRDebug.drawDotScreen(center, radius, color)` — vec4 colors (0..1 floats) via `vec4FromLua`
   Trailing optionals via `sol::optional`, defaulting to the C++ defaults (`a = 1.0`, `segments = 32`). Out of scope, deliberately: `worldToScreen`/`screenToWorld` (pure-math helpers, additive follow-up if a creation needs them) and `clear()` (the flush owns clearing).
4. **Wire into `bindLuaDrivenEcs()`** — `detail::bindDebugOverlay(*this);` beside `bindRenderGlue`/`bindWidgets` (`lua_script.cpp:726-727`).
5. **Headless invocation test** `test/script/lua_debug_overlay_bindings_test.cpp` (+ registration in `test/CMakeLists.txt` beside `lua_render_bindings_test.cpp`). Mirror the `LuaRenderBindingsTest` harness, but invoke and assert buffer contents, not just presence — see Acceptance criteria. Reset via `IRDebug::clear()` in teardown so tests stay independent.
6. **Demo exercise** (visual e2e, mirroring the render-glue precedent): in `creations/demos/lua_pipeline_demo/`, add `IRSystem::DEBUG_OVERLAY` to the `registerPrefabSystems<...>` list (`main_lua.cpp`) and have `main.lua` splice `IRSystem.systemId(SystemName.DEBUG_OVERLAY)` into its RENDER pipeline after `TRIXEL_TO_FRAMEBUFFER` (matching the default demo's placement), with the demo's existing RENDER-phase Lua system (positioned before DEBUG_OVERLAY) issuing a world-anchored marker (`drawDiamond3D`) + a HUD frame (`drawRectScreen`) each frame.
7. **Docs:** new `engine/script/CLAUDE.md` section "Debug-overlay draws (`IRDebug.*`)" — the surface, the immediate-mode re-issue-each-frame contract, RENDER-phase placement before `DEBUG_OVERLAY`, and the UPDATE-phase warning (below).

### Affected files

- `engine/prefabs/irreden/render/debug_overlay_draws.hpp` — NEW: the moved `IRDebug` namespace, IRMath-qualified
- `engine/prefabs/irreden/render/systems/system_debug_overlay.hpp` — keeps only `System<DEBUG_OVERLAY>`; includes the new header
- `engine/script/include/irreden/script/ir_script_utils.hpp` — add `vec2FromLua`, `vec4FromLua`
- `engine/script/include/irreden/script/lua_debug_overlay_bindings.hpp` — NEW: `detail::bindDebugOverlay`
- `engine/script/src/lua_script.cpp` — call it from `bindLuaDrivenEcs()`
- `test/script/lua_debug_overlay_bindings_test.cpp` — NEW headless invocation test
- `test/CMakeLists.txt` — register the test
- `creations/demos/lua_pipeline_demo/main_lua.cpp`, `main.lua` — DEBUG_OVERLAY registration + Lua-issued overlay draws
- `engine/script/CLAUDE.md` — new section + helper-table rows

### Acceptance criteria

Positive-fire (all observably fire with the feature ON, headless, any host):

- Lua `IRDebug.drawLine3D({1,2,3}, {4,5,6}, 1, 0, 0)` → `IRDebug::getLines()` holds exactly one record with those endpoints, `r=1, g=0, b=0`, and the **defaulted** `a=1.0`; the same call with a `vec3` userdata arg also lands.
- `drawPath3D` with a 3-point table → exactly 2 line records with chained endpoints.
- `drawRectScreen` → 2 screen-triangle + 4 screen-line records (the C++ decomposition).
- `drawCircle3D` records `segments=32` by default and a passed override verbatim.
- Presence checks for every bound function (the render-bindings pattern), plus a non-table/non-vec3 first arg raises a Lua error rather than zero-defaulting.
- `lua_pipeline_demo --auto-screenshot` shows the Lua-issued marker + HUD frame (visible delta vs master).
- Regression: full engine + default demo build green after the header split (C++ callers incl. `DEBUG_CULLING_MINIMAP` compile unchanged); existing `test/script/` suite green.

### Gotchas

- **Immediate-mode contract.** The flush consumes AND clears the buffers every `DEBUG_OVERLAY` RENDER tick, so a draw persists only if re-issued each frame by a system running before `DEBUG_OVERLAY` in RENDER. A script-load one-shot draw shows for at most one frame. A draw issued from an **UPDATE**-phase Lua system runs 0..N times per render frame (fixed timestep) → flickering or N-times-overdrawn overlay; document RENDER-phase placement as the supported shape (same doc language as the `IRGui.draw*` contract).
- **Buffer identity across TUs.** The moved draw/accessor functions must stay `inline` (same namespace, non-static) — the script TU and the flush TU share one buffer set via inline-function static-local deduplication. Converting to `static` free functions would silently give the script module its own dead buffers.
- **Callsite type validation.** `vec3FromLua` zero-defaults on unrecognized input by contract — the binding must type-check the `sol::object` and raise a Lua error first (existing binding style), or a typo'd call draws silently at the origin.
- **`drawPath3D` boundary allocation.** Building the temp `std::vector<vec3>` per call at the binding boundary is acceptable for a debug surface; do not cache it in the binding lambda (no cross-frame state in bindings).
- **Segments clamp.** The flush clamps circle segments to 32 (`kCircleLutMaxSegments`); a larger Lua value silently clamps — mirror-C++ behavior, don't add validation.
- **Scope fence.** Keep the binding immediate-mode; no handles, retained draws, or per-draw lifetimes.
