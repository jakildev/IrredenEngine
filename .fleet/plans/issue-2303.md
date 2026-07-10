# Plan: render: add IRPrefab::Fog::attachToCanvas helper

- **Issue:** #2303
- **Model:** sonnet — mechanical helper extraction + call-site migration. The
  two design decisions below are pinned here, so the implementer has no open
  judgment call.
- **Scope:** one PR (one task, no subtasks).

### Files
- `engine/prefabs/irreden/render/fog_of_war.hpp` — add the helper + one include.
- `creations/demos/lua_perf_grid/main_lua.cpp`
- `creations/demos/skeletal_demo/main.cpp`
- `creations/demos/perf_grid/main.cpp`
- `creations/demos/fog_demo/main.cpp`
- `creations/demos/lighting/common/lighting_demo_scene.hpp`

### Verified current state
- Exactly **five** in-repo sites attach `C_CanvasFogOfWar` (grep-confirmed; all
  under `creations/demos/`, all engine-side). Each first does
  `mainCanvas = IRRender::getActiveCanvasEntity()`, then sets
  `C_TrixelCanvasRenderBehavior{}` + `C_CanvasFogOfWar{}`.
- `kFogOfWarSize = 256`, `kFogOfWarHalfExtent = 128`
  (`component_canvas_fog_of_war.hpp:74-75`).
- Every existing `IRPrefab::Fog::*` free function operates on the **active**
  canvas via `detail::activeFogComponent()`; none takes an EntityId.
- No `hasComponent` free function exists; the established presence-check idiom
  is `getComponentOptional<C>(e).has_value()` (used by the prefab's own
  `detail::activeFogComponent`).
- `C_TrixelCanvasRenderBehavior` header:
  `<irreden/render/components/component_trixel_canvas_render_behavior.hpp>`.

### Two corrections vs. the issue's proposed sketch (the design content)
The issue's sketch — `IRPrefab::Fog::revealRadius(0,0,kFogOfWarSize)` inside
`attachToCanvas(canvas,...)` — has two defects this plan fixes:

1. **Reveal must target the passed `canvas`, not the active canvas.**
   `Fog::revealRadius` resolves `activeFogComponent()` (the *active* canvas).
   If `canvas` is not the active canvas at call time, that reveals the wrong
   canvas (or no-ops). The helper must reveal through the component it just
   attached: `getComponentOptional<C_CanvasFogOfWar>(canvas)` ->
   `->revealRadius(...)`. (In every current site `canvas` happens to be
   active, but the helper must not bake that in.)
2. **`C_TrixelCanvasRenderBehavior` is NOT fog-exclusive.** It gates the
   trixel render behaviors generally — AO / sun-shadow / light-volume passes
   require it too. In `lighting_demo_scene.hpp` it is set **unconditionally**
   while fog is set only `if (enableFog)`. So the helper must add
   `C_TrixelCanvasRenderBehavior` **only if absent**, never clobber a
   caller's already-set (possibly customized) one.

### Picked API
```cpp
// fog_of_war.hpp — add include:
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

/// Attach both components FOG_TO_TRIXEL's archetype requires to @p canvas:
/// C_TrixelCanvasRenderBehavior (added only if absent, preserving any prior
/// customized behavior component) and a fresh C_CanvasFogOfWar. FOG_TO_TRIXEL
/// silently no-ops on a canvas missing either, so co-attaching here removes
/// that footgun from call sites. @p revealRadius > 0 also reveals an
/// origin-centered disc of that radius on @p canvas (pass kFogOfWarSize for a
/// full reveal); 0 (default) attaches only, leaving the grid unexplored.
inline void attachToCanvas(IREntity::EntityId canvas, int revealRadius = 0) {
    if (!IREntity::getComponentOptional<IRComponents::C_TrixelCanvasRenderBehavior>(canvas)
             .has_value())
        IREntity::setComponent(canvas, IRComponents::C_TrixelCanvasRenderBehavior{});
    IREntity::setComponent(canvas, IRComponents::C_CanvasFogOfWar{});
    if (revealRadius > 0) {
        if (auto opt = IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(canvas))
            (*opt)->revealRadius(0, 0, revealRadius);
    }
}
```

**Why a single `int revealRadius = 0` instead of the issue's
`bool revealAll=false, int revealRadius=kFogOfWarSize`:** the bool+radius pair
is redundant (revealAll==false makes the radius dead; ==true always uses it).
A single radius with `0 == no reveal` sentinel maps directly onto every
existing call site and drops the two-arg combination. It also dodges a name
collision — a param literally named `revealRadius` would shadow the free
function `Fog::revealRadius` in the same namespace. *Rejected:* the two-arg
form; and a `revealRadius`-only overload targeting the active canvas
(inconsistent with taking an explicit entity). Only origin-centered reveals
fold into the param; off-center / dynamic sites keep their own reveal call
(see migration).

### Migration (per site — NOT a uniform sed; the reveal patterns differ)
Replace the
`setComponent(C_TrixelCanvasRenderBehavior{}) + setComponent(C_CanvasFogOfWar{})`
pair with `attachToCanvas(...)`:

1. **lua_perf_grid/main_lua.cpp** (attach ~313; `revealRadius(0,0,128)` ~330):
   origin-centered -> **fold**: `IRPrefab::Fog::attachToCanvas(mainCanvas, 128);`
   and delete the separate `revealRadius(0,0,128)`. (The reveal is
   order-independent of the intervening light `createEntity`, so moving it
   earlier is behavior-identical.)
2. **skeletal_demo/main.cpp** (491; `revealRadius(0,0,80)` at 494): **fold**:
   `attachToCanvas(mainCanvas, 80);` delete the reveal line.
3. **perf_grid/main.cpp** (837; `revealRadius(0,0,128)` at 855, ~18 lines
   apart): reveal is not adjacent — do NOT reorder. Replace only the attach
   pair with `attachToCanvas(mainCanvas);` and leave
   `revealRadius(0,0,128)` at 855 as-is.
4. **fog_demo/main.cpp** (740-741): dynamic per-frame `setVisionCircle` demo,
   no init reveal -> `attachToCanvas(mainCanvas);` (no reveal arg); leave
   every `setVisionCircle` call untouched.
5. **lighting/common/lighting_demo_scene.hpp** (191 unconditional behavior;
   192 `if(enableFog) setComponent(C_CanvasFogOfWar{})`; off-center
   `revealRadius(24,6,42)` at 320): **KEEP the unconditional
   `C_TrixelCanvasRenderBehavior{}` at 191** (non-fog paths need it). Replace
   only the fog line: `if (enableFog) { IRPrefab::Fog::attachToCanvas(mainCanvas); }`.
   The only-if-absent guard makes attachToCanvas see the behavior already
   present and skip it. Leave the off-center `revealRadius(24,6,42)` at 320
   as-is.

Each migrated file already includes fog_of_war.hpp (they call
`Fog::revealRadius`/`setVisionCircle` today), so no new include is needed at
the call sites.

### Acceptance
- `IRPrefab::Fog::attachToCanvas(canvas, revealRadius=0)` exists, attaches
  both required components (behavior only-if-absent), and optionally reveals
  an origin disc on the **passed** canvas.
- All five in-repo call sites use it; no behavior/visual change — same
  components attached, same reveals preserved. The affected demos'
  render-verify references stay green.
- Headless smoke build green.

### Gotchas / pitfalls
- **Reveal targets `canvas`, not the active canvas** (correction 1) — don't
  route through `Fog::revealRadius`.
- **Don't clobber `C_TrixelCanvasRenderBehavior`** (correction 2) —
  only-if-absent, or you break `lighting_demo_scene`'s no-fog path / any
  customized behavior.
- **Not uniform** — sites 3/4/5 keep their own reveal; only 1/2 fold into the
  param.
- **fog_of_war.hpp stays a leaf `inline` header** (no .cpp); it only gains the
  one behavior-component include.
- **Downstream (gitignored game) call sites are out of scope** — the issue's
  Context note says consumer migration in downstream repos follows later.
  Migrate only the five in-repo sites.

### Verification
`fleet-build --target IRShapeDebug` (headless) green. Because the change is a
pure attach-order refactor with reveals preserved, the real gate is a
render-verify pass on the affected demos (e.g. `fog_demo`, `perf_grid`,
lighting family). A Linux/GL pane can run the GL render-verify; the macOS/Metal
build also compiles. No new visual output is introduced, so no new baselines.
