<!--
Plan file for #2341 (per #1932 — committed as the first commit of the
implementer's PR). This is the `## Plan` comment posted on issue #2341 on
2026-07-20. Reproduced verbatim below; the issue thread is the canonical
source.
-->

## Plan: light-volume global buffers go stale across canvases with 2+ populated C_CanvasLightVolume

- **Issue:** #2341
- **Model:** sonnet — the design judgment (option a vs b, guard location, the exact predicate, the positive-fire test shape) is resolved in this plan; implementation is a bounded, CPU-side diagnostic change (begin/endTick counters + one `IR_ASSERT` + a pure predicate + comments + a unit test). No shader, GPU-buffer, dispatch, or pipeline-state changes — render output stays byte-identical — so it does not carry opus-tier render judgment despite touching render system headers.
- **Date:** 2026-07-20

### Scope

Pick **option (a)** from the acceptance criteria: make the load-bearing
"single light-volume canvas" assumption of the spot-cone path **explicit and
guarded**, rather than building the full per-canvas light-list/params storage
(option b). The full fix is deferred — it should be filed as a follow-up only
when a real multi-canvas-SPOT consumer exists to validate it against (today
there are zero; see "Why (a), not (b)" below).

The deliverable: a debug `IR_ASSERT` that fires the moment a scene threads
per-canvas-**varying** spot data through the shared global buffers for 2+
rendered light-volume canvases, plus a documenting comment at the consumer's
global binds, plus a pure-CPU unit test that positively fires the guard.

### Verified current state (mechanism confirmed against code, not asserted)

Read both systems in full at current master (`5c53d2e5`). The staleness is real
and exactly as the Opus recheck on PR #2337 described:

- **Producer** `System<COMPUTE_LIGHT_VOLUME>::tick` (`system_compute_light_volume.hpp:375`)
  runs **per canvas** and re-uploads the single global named buffers every
  iteration: `lightSourceBuf_->subData(...)` (line 441) and
  `paramsBuf_->subData(...)` (line 443). Both `LightSourceBuffer` (SSBO) and
  `LightVolumeParamsBuffer` (UBO) are created once in `create()`
  (lines 551–566) as **global** resources. `params_.worldOriginVoxel_.w` is the
  per-canvas **has-SPOT** flag (line 422–423); `params_.lightCount_` and the
  light list are per-canvas. After the system finishes all canvases, the globals
  hold only the **last-processed** canvas's data.
- The two systems are **not** interleaved — each pipeline system iterates all
  its matched canvases before the next runs, so `COMPUTE_LIGHT_VOLUME` finishes
  every canvas before `LIGHTING_TO_TRIXEL` starts.
- **Consumer** `System<LIGHTING_TO_TRIXEL>::tick` (`system_lighting_to_trixel.hpp:145`)
  binds each canvas's **own** volume texture (slot 5, line 208) and **own**
  winning-light ID texture (image unit 7, line 213), but binds the **global**
  `lightVolumeParamsBuf_` (line 226) and **global** `lightSourceBuf_` (line 231).
  On the has-SPOT path it reads `worldOriginVoxel.w` and indexes `lights[winId-1]`
  from those globals — i.e. from the last canvas's data, not the canvas being lit.
- `.xyz` (`cameraAnchorVoxel()`) is canvas-invariant within a frame, so it was
  always safe; `.w`/light-list are the **first** per-canvas-varying data threaded
  through the globals (#2318), which is why the "last canvas wins" property only
  became load-bearing then.
- **No shipping scene hits it**: every creation attaches exactly one
  `C_CanvasLightVolume` (on `mainCanvas`); the per-axis path shares the main
  canvas's volume/params inside the main tick (`dispatchPerAxisLighting`, not a
  separate `C_CanvasLightVolume` entity); and `main_per_canvas_scope.cpp` (#363)
  is emissive-only with a **non-rendered** sentinel B canvas
  (`useCameraPositionIso_ == false`, so `COMPUTE_LIGHT_VOLUME::tick` early-returns
  at line 380 and never counts it). Worst case is a visual miss (spot → omni),
  **never a crash** (`LightSourceBuffer` is always sized to 256).

### Why (a), not (b)

The Opus recheck (PR #2337, 2026-07-09) recommended **(a) a comment + `IR_ASSERT`**
as the primary path and framed (b) per-canvas buffers as a heavier "tracked
follow-up." Option (b) would add a ~20 KiB per-canvas `LightSourceBuffer` + a
per-canvas params UBO on `C_CanvasLightVolume` and re-bind the canvas's own
buffers in the consumer — real storage + code for a capability
(multi-light-volume-canvas scenes with SPOT lights) that has **zero** current
consumers, and whose only positive-fire acceptance test would be a synthetic
new demo scene. Per engine convention (assert the load-bearing invariant; defer
generalization until a real consumer exists), (a) is the proportionate fix: it
converts a silent wrong-cone into a loud debug abort, and the abort message is
the trigger to do (b) — validated against the actual scene that needs it — when
one appears.

### Approach

Guard **in the producer** (`COMPUTE_LIGHT_VOLUME`), with a cross-referencing
comment in the consumer. Deviation-with-rationale from the recheck's "assert in
`LIGHTING_TO_TRIXEL`" phrasing: the per-canvas has-SPOT flag and the
processed-canvas count are available **CPU-side only in the producer** (the
consumer would have to read the flag back off the GPU UBO). Putting the assert
in the producer lets the guard be **precise** (`!(2+ canvases && any spot)`)
instead of the conservative "≤1 light-volume canvas" a consumer-side count would
force — and the producer is the system that actually clobbers the shared buffers.

1. **`system_compute_light_volume.hpp` — add a pure predicate** in the existing
   `IRSystem::detail` namespace (alongside `gatherLightSources`, so the existing
   isolated gtest can call it the same way):
   ```cpp
   // The consumer (LIGHTING_TO_TRIXEL) reads the has-SPOT gate + light list from
   // the GLOBAL LightVolumeParamsBuffer / LightSourceBuffer, which this system
   // overwrites per canvas — after the tick they hold only the LAST processed
   // canvas's data. That is simultaneously correct for every processed canvas
   // only while at most one light-volume canvas is processed, OR no processed
   // canvas seeds a SPOT (the only per-canvas-VARYING field the consumer reads;
   // .xyz is the camera anchor, identical for all canvases). Returns false when
   // the globals cannot be correct for all processed canvases at once — 2+
   // processed canvases AND at least one SPOT. Multi-canvas SPOT scenes need
   // per-canvas storage (issue #2341 deferred option b).
   inline bool lightVolumeGlobalBufferSafe(int processedLightVolumeCanvases,
                                           bool anyCanvasSeededSpot) {
       return !(processedLightVolumeCanvases >= 2 && anyCanvasSeededSpot);
   }
   ```
2. **`system_compute_light_volume.hpp` — wire per-frame counters** on
   `System<COMPUTE_LIGHT_VOLUME>` (registerSystem auto-wires begin/endTick when
   defined; the system currently has neither):
   - Add members `int processedLightVolumeCanvases_ = 0;` and
     `bool anyCanvasSeededSpot_ = false;`.
   - Add `void beginTick() { processedLightVolumeCanvases_ = 0; anyCanvasSeededSpot_ = false; }`.
   - In `tick`, **after** the `if (!behavior.useCameraPositionIso_) return;`
     early-return (line 380–381) so non-rendered canvases like #363's sentinel B
     are not counted: `++processedLightVolumeCanvases_;` and, inside/after the
     upload block where the local `hasSpot` is computed (line 409–418),
     `anyCanvasSeededSpot_ |= hasSpot;`.
   - Add `void endTick()` with the guard:
     ```cpp
     IR_ASSERT(
         detail::lightVolumeGlobalBufferSafe(processedLightVolumeCanvases_, anyCanvasSeededSpot_),
         "COMPUTE_LIGHT_VOLUME: global LightSourceBuffer/LightVolumeParams hold only the "
         "last processed canvas's spot data; a scene with 2+ rendered C_CanvasLightVolume "
         "canvases where any seeds a SPOT renders wrong/omni cones (issue #2341). Make the "
         "light list / has-SPOT per-canvas (option b) to lift this assumption."
     );
     ```
3. **`system_lighting_to_trixel.hpp` — documenting comment only** (no code
   change) at the two global binds (lines 226 and 231): note the params/light-list
   buffers are per-frame-correct only for the last-processed light-volume canvas,
   so the spot-cone read here relies on the single-light-volume-canvas assumption
   guarded in `COMPUTE_LIGHT_VOLUME::endTick`; multi-canvas + SPOT needs per-canvas
   storage (#2341 option b).
4. **`test/render/per_canvas_light_scope_test.cpp` — positive-fire unit test**
   (existing gtest, runs without a RenderManager): assert the predicate.

### Affected files

- `engine/prefabs/irreden/render/systems/system_compute_light_volume.hpp` — new
  `detail::lightVolumeGlobalBufferSafe` predicate; `beginTick`/`endTick` +
  two counter members on `System<COMPUTE_LIGHT_VOLUME>`; count/`|= hasSpot` in `tick`.
- `engine/prefabs/irreden/render/systems/system_lighting_to_trixel.hpp` —
  documenting comment at the global `lightVolumeParamsBuf_`/`lightSourceBuf_`
  binds (no logic change).
- `test/render/per_canvas_light_scope_test.cpp` — new gtest cases for the predicate.

### Cross-system audit (consumers of the shared global buffers)

The change documents/guards the shared global `LightSourceBuffer` +
`LightVolumeParamsBuffer`; it does not alter their layout or bindings, so no
consumer needs migration. For completeness, the CPU-side readers/binders:
- `COMPUTE_LIGHT_VOLUME` — owner; binds both for its own clear/seed/propagate
  dispatch (one canvas at a time — internally consistent).
- `LIGHTING_TO_TRIXEL` — the affected consumer; binds both globally (lines 226,
  231) and reads them on the has-SPOT path. This is the sole read-site that is
  per-canvas-incorrect, and the target of the documenting comment.
No shader `binding=`/slot changes — `kBufferIndex_LightSourceBuffer` and
`kBufferIndex_LightVolumeParams` are untouched.

### Acceptance criteria

Positive-fire (the guard observably trips with the unsafe condition), with
positive controls — mirrors the isolated-gather test style already in the file,
no GPU/RenderManager, no death test:
- `EXPECT_FALSE(IRSystem::detail::lightVolumeGlobalBufferSafe(2, /*anySpot=*/true))`
  — **the positive fire**: 2 canvases + a spot ⇒ guard trips.
- `EXPECT_FALSE(... lightVolumeGlobalBufferSafe(3, true))`.
- Positive controls (guard must NOT trip on currently-correct configs):
  `EXPECT_TRUE(... lightVolumeGlobalBufferSafe(1, true))` (single canvas, spot ok),
  `EXPECT_TRUE(... lightVolumeGlobalBufferSafe(2, false))` (multi canvas, no spot — correct today),
  `EXPECT_TRUE(... lightVolumeGlobalBufferSafe(0, false))`.
- Build is clean and the existing `per_canvas_light_scope_test` suite stays green
  (`IRShapeDebug` builds; no render-verify needed — output is byte-identical, the
  change is a debug assert + comments).

### Gotchas

- **Count after the early-return.** The `++processedLightVolumeCanvases_` must
  land *after* `if (!behavior.useCameraPositionIso_) return;` — otherwise #363's
  non-rendered sentinel B canvas counts and the assert false-fires on an existing
  demo.
- **Predicate is intentionally conservative on one exotic case.** Two canvases
  that both carry the *same* world-scope spot upload identical lists and are
  technically correct, yet the predicate returns false. That is acceptable: the
  config is exotic, the abort is debug-only (`IR_ASSERT` is stripped under
  `IR_RELEASE`), and the message tells the author to verify or move to option (b).
- **`IR_ASSERT` is debug-only** (`engine/profile/include/irreden/ir_profile.hpp`;
  stripped in `IR_RELEASE`). That is fine — this is a developer-misuse guard, not
  a runtime fallback; release builds keep the existing benign omni-fallback.
- **Do NOT build option (b) here.** If a real multi-canvas-SPOT scene is needed
  later, file a follow-up per TASK-FILING.md to make the light list/has-SPOT
  per-canvas (per-canvas SSBO/UBO on `C_CanvasLightVolume`, re-bound by the
  consumer) — validated against that scene. This task is the guard only.
- **No `simplify` "dead assert" flag:** the `endTick` assert references
  `detail::lightVolumeGlobalBufferSafe`, keeping the predicate live even though
  the guard is never expected to fire in shipping scenes.

