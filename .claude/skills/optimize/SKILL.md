---
name: optimize
description: >-
  Profile and improve performance for newly written or modified
  performance-critical code in the Irreden Engine. Use whenever the
  current change touches a system tick function, a render pipeline
  stage, a shader, audio/video processing, math hot paths, or
  anywhere on the per-frame critical path. Also use when the user
  says "optimize", "profile", "this is slow", "find the hotspot",
  "check perf", "benchmark this", or "why is this frame slow".
  Should run BEFORE simplify when the change is performance-relevant
  — optimize may add profile blocks or comments that simplify would
  otherwise consider extraneous. Reports CPU profiler findings and,
  where GPU profiling infrastructure exists, GPU timer query results.
  Files an issue if the necessary profiling infrastructure is
  missing.
---

# optimize

A performance pass on freshly written or modified hot-path code.
Decides what kind of profiling matters (CPU, GPU, or both), runs it,
identifies hotspots, and applies engine-specific optimizations.

## Why this exists

The engine has hard real-time goals: 60 FPS on the target platform,
deterministic frame pacing for the iso voxel pipeline. New code that
adds even 0.5 ms to the per-frame budget compounds — by the time five
features have each shipped a "small" regression, the frame budget is
gone.

Catching regressions in the author's worktree, before review, is much
cheaper than discovering them in a perf bisect three weeks later.
This skill is that catch.

It runs **before** simplify when relevant, because simplify might
strip the `IR_PROFILE_BLOCK` calls or the explanatory comments that
optimize added during instrumentation.

## When this is the right skill (vs. simplify alone)

Run optimize when the diff touches:

- **System tick functions** — anything in `engine/system/`,
  `engine/prefabs/irreden/.../systems/`, or per-entity loops in
  creations.
- **Render pipeline stages** — `engine/render/`, shader files
  (`engine/render/src/shaders/`), GPU buffer lifetimes, dispatch
  sizes.
- **Audio / video processing** — `engine/audio/`, `engine/video/`,
  ffmpeg paths, MIDI handling.
- **Math hot paths** — `engine/math/` functions called from per-entity
  loops or per-pixel shader code.
- **New game systems** — anything in `creations/<name>/src/` that
  runs per-frame or per-entity.
- **Anything the author suspects is slow** — even if it's not in the
  list above, if the user says "this feels slow", profile it.

Skip optimize when the diff is:

- Tests, docs, or config changes.
- Build / CI / tooling changes.
- One-shot setup or teardown code that doesn't run per frame.
- Pure refactors that preserve hot-path structure.

If you're unsure, ask. Profiling time is cheap; pushing a hot-path
regression is expensive.

## Flow

### 1. Identify the hot path

```bash
git diff --stat
git diff
```

For each touched file, ask:
- Does this run per-frame?
- Does this run per-entity inside a system tick?
- Does this run per-pixel inside a shader?
- Does this run per-sample in audio?

If yes to any, that file is a candidate for profiling. Group
candidates by **CPU-bound** (system ticks, math) and **GPU-bound**
(shaders, dispatch sizes, frame buffer ops). Some changes are both.

### 2. CPU profiling

The engine has CPU profiling via `engine/profile/` — easy_profiler
under the hood, exposed through the `IR_PROFILE_*` macros in
`engine/profile/include/irreden/ir_profile.hpp`. Read
`engine/profile/CLAUDE.md` for the full surface.

**Instrument the hot path:**

```cpp
void IRSGlowPulse::tickEntity(C_GlowPulse& glow, C_Color& color) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
    // ... existing logic
}
```

Wrap the **entry point** of the new code (system tick function,
pipeline stage, audio callback). For inner sub-blocks worth
isolating, use `IR_PROFILE_BLOCK("name", color)` / `IR_PROFILE_END_BLOCK`.

Use the named `IR_PROFILER_COLOR_*` constants from
`engine/profile/include/irreden/ir_profile.hpp` (`_RENDER`,
`_ENTITY_OPS`, `_COMMANDS`, etc.). Every existing call site uses
the named constants — raw ARGB hex literals are an anti-pattern.
Pick the constant that matches the module of the system you're
instrumenting; the colors group related blocks visually in the
easy_profiler timeline.

**Run with profiling enabled:**

```bash
fleet-build --target <executable>
fleet-run --timeout 15 <executable>
```

`CPUProfiler::setEnabled(true)` is the runtime gate — check the
creation's startup to confirm it's on, or temporarily enable in the
demo's main.

The profiler dumps a `.prof` file when the executable exits. Open it
with the `profiler_gui` tool from easy_profiler (the human runs this
— you can't render the GUI from here). Ask the human for the
hotspot read.

If `IR_PROFILE_*` macros are no-ops (release build) or disabled at
runtime, the run produces no data — switch the build to debug or
re-enable the profiler before re-running.

### 3. GPU profiling

The engine **does not currently have GPU timer query infrastructure**
in production. The OpenGL bindings (`engine/render/include/glad/glad.h`)
expose `glQueryCounter` / `GL_TIMESTAMP`, but no engine code uses
them. There is no per-pass timing API and no Lua-accessible breakdown.

This is the work tracked in **jakildev/IrredenEngine#173** ("Skill:
render performance optimization loop"). Until that lands, GPU
profiling here is qualitative:

- **Use RenderDoc** for a single-frame capture if the human can run
  it (RenderDoc is GUI; you can't drive it from here). Tell the
  human what to look at — which pass, which draw call, which compute
  dispatch.
- **Use shader-side counters** as a proxy: a debug uniform
  incremented per work-group, read back via SSBO, gives a coarse
  cost estimate. Useful for "is the new compute pass even running"
  but not for ms-level timing.
- **Compare frame time before/after** with the engine's existing
  frame counter (`IRTime` / wherever `getDeltaTime()` lives) — log
  the average delta over 300 frames before the change and after.
  Coarse but real.

**If the change touches a render pipeline stage and the impact isn't
obvious from inspection alone**, comment on the PR that
`jakildev/IrredenEngine#173` is required to validate the perf cost,
and surface the missing infrastructure to the human. Don't merge
hot-path GPU work blind.

### 4. Engine-specific optimizations to check

For **CPU hotspots** in system ticks:

- Per-entity `getComponent<C_Foo>()` → add `C_Foo` to the system's
  template parameters so it's iterated as a dense column. (See
  `engine/system/CLAUDE.md` for tick-function signature patterns.)
  This is the single biggest engine-wide perf win and `simplify`
  also flags it — but optimize is the place to actually verify the
  measured ms improvement after fixing.
- Allocation in per-entity paths → hoist to component construction
  or pre-size + reuse a member buffer.
- `std::map` / `std::unordered_map` lookups in the inner loop →
  replace with a flat array indexed by entity ID, or move the
  lookup outside the loop.
- Virtual dispatch in the inner loop → if the type is known at
  compile time, devirtualize via templates.

For **GPU hotspots**:

- Hand-rolled compute dispatch sizes → use
  `voxelDispatchGridForCount()`. Wrong dispatch sizes either waste
  workgroups (overestimate) or miss work (underestimate).
- Compute shader workgroup-size mismatch with the dispatched size —
  read the shader's `layout(local_size_x = ...)` and confirm the CPU
  side's dispatch math accounts for it.
- Excessive uniform block updates per frame → batch into one update.
- Texture-format mismatch causing implicit conversions in the
  fragment shader.
- Read-back via `glReadPixels` or `glGetBufferSubData` synchronously
  — kills the frame, force a fence + multi-frame ringbuffer instead.

For **shader hot paths**:

- Repeated math sequences across multiple shaders that should be in
  a shared `.glsl` include (e.g. `ir_iso_common.glsl` for iso
  projection math).
- Per-pixel work that could be per-vertex — move it.
- Branchy conditionals on a uniform value → consider a permutation.

### 5. Measure, don't guess

For each change you make based on profiling, **re-run the same
benchmark** and confirm the improvement. A "fix" that doesn't move
the needle isn't an optimization — revert it and look elsewhere.
Pre/post numbers belong in the PR body so the reviewer doesn't have
to re-profile.

If pre/post measurement isn't possible (e.g. GPU pass without timer
queries), say so explicitly in the PR — never claim "this is faster"
without numbers.

### 6. Report

Print a compact summary:

```
optimize: <N> hot-path file(s) profiled
  CPU hotspots:
    - <path:line> — <function> — <ms before> → <ms after> (<change>)
    - ...
  GPU hotspots:
    - <pass name> — <observation> — <action taken or blocker>
  applied <X> optimization(s)
  reported <Y> finding(s) needing more measurement
  baselines: <link to docs/perf-reports/ entry, if added>
```

If the touched code didn't actually need optimization (after
profiling, no hotspot), say so — the value here is the confidence,
not the changes.

## Coordinating with simplify

Run optimize **first** when the change is performance-relevant.
Optimize may:
- Add `IR_PROFILE_*` blocks at hot-path entry points
- Add explanatory comments documenting non-obvious perf trade-offs
  (e.g. "// pre-sized to avoid realloc in tick — see PR #N")
- Restructure a tick function for better cache behavior

Simplify, run after, will respect those additions because:
- `IR_PROFILE_*` macros are part of the engine's profiling story,
  not "debug logs"
- Comments that explain a perf rationale are kept (they explain a
  non-obvious why)

If you're working on a non-perf-critical change (docs, tests,
mechanical refactor), skip optimize and run simplify directly.

## What this skill does NOT do

- **Doesn't replace human profiling judgment.** A 0.1 ms regression
  in a cold path may be fine; a 0.1 ms regression on the per-entity
  hot path at 50K entities is 5 ms/frame. Surface the data, let the
  human decide.
- **Doesn't optimize without measuring.** Speculative optimization
  is worth less than the readability it costs. Always measure.
- **Doesn't push.** Edits the working tree only.
- **Doesn't run RenderDoc or other GUI profilers.** Hand off the
  capture instructions and ask the human to run it.

## Example

User: "I added a new lighting pass — optimize it before I push"

```
optimize: 3 files profiled (engine/render/, shaders/glsl/)

CPU hotspots: none. The new pass is GPU-bound.

GPU profiling: limited — engine has no timer-query infrastructure
(see #173). Frame-time A/B with 300-frame averages:
  - before: 14.2 ms ± 0.3
  - after:  16.8 ms ± 0.4
  - delta:  +2.6 ms (16% over budget at 60fps)

Hotspot suspect (RenderDoc capture needed, asked human to run):
  c_lighting_apply.glsl — likely the per-pixel 3x3x3 sample loop.
  Reading the occupancy grid 27 times per pixel.

Optimization applied:
  - Switched to a 2D shadow texture lookup once per pixel instead
    of per-sample, then modulated by the precomputed AO texture.
  - Re-measured: 14.5 ms ± 0.3 (+0.3 ms over original — acceptable).

Reported for human:
  - GPU timer queries needed before next render PR (issue #173).

ready for simplify pass.
```
