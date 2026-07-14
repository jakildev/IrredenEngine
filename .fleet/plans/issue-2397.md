## Plan: engine/math: IRMath::wrapToRange helper + migrate hand-rolled fmod+correction sites

- **Issue:** #2397
- **Model:** sonnet — every edit is prescribed below (signatures, sites, boundary semantics); no design choice remains
- **Date:** 2026-07-14

### Verified current state

Grep sweep over `engine/` + `creations/` for `fmod` **and** for the while-loop / `±kTwoPi` wrap idiom (checked at `origin/master` b8311c12). The issue's four sites are confirmed, and the sweep found **three more** the body missed — the full candidate set is seven:

1. `engine/prefabs/irreden/update/components/component_periodic_idle.hpp:146-149` (`valueAtAngle`) — `IRMath::fmod` + negative correction → `[0, 2π)`.
2. `engine/prefabs/irreden/render/camera.hpp:87-95` (`wrapYaw`) — fmod + correction → `[-π, π)`, then an ε-clamp to `-π + 1e-4` (pitchFromQuat validity guard).
3. `engine/prefabs/irreden/update/systems/system_action_animation.hpp:64-70` — `std::fmod` + correction on **double** (`C_RhythmicLaunch::elapsedSeconds_`/`periodSeconds_`). Live cpp-math violation.
4. `engine/prefabs/irreden/update/systems/system_spring_platform.hpp:138-142` — same double idiom. Live cpp-math violation.
5. **(new)** `engine/prefabs/irreden/render/camera.hpp:72-77` (`yawFromQuat`) — identical fmod+correction+`-π` shift as `wrapYaw`, inlined.
6. **(new)** `engine/prefabs/irreden/render/components/component_triangle_canvas_background.hpp:401` — bare `std::fmod` (no correction; the accumulator is non-negative). Live cpp-math violation.
7. **(new)** `engine/prefabs/irreden/render/systems/system_gizmo_drag.hpp:328-334` — private static `wrapToPi` while-loop wrap, one caller (line 235).

`IRMath` today: `fmod` is float-only (`ir_math.hpp:357`); `kPi`/`kTwoPi` are float constexprs; no wrap helper exists. `.claude/rules/cpp-math.md` has no wrap bullet (the #2399 batch deliberately left it for this ticket; originating #2354 is closed as split into this issue). Math unit tests live in `test/math/*_test.cpp` (gtest), each file explicitly listed in `test/CMakeLists.txt` under the `IrredenEngineTest` target.

### Scope

Add `IRMath::wrapToRange` + `wrapAngleTwoPi` / `wrapAnglePi`, migrate all seven hand-rolled sites, add the cpp-math rule bullet, and cover the helper with headless unit tests. One task, one PR.

### Approach

**1. Helper** in `engine/math/include/irreden/ir_math.hpp`, adjacent to `fmod` (~line 356):

```cpp
/// Wrap x into [0, range) (right half-open). Floor-mod semantics: negative x
/// wraps correctly, matching GLSL/Metal `mod(x, y)` for range > 0, so CPU and
/// GPU phase math agree (same rationale as roundHalfUp). Caller guarantees
/// range > 0.
template <typename T>
inline T wrapToRange(T x, T range) {
    static_assert(std::is_floating_point_v<T>);
    T wrapped = std::fmod(x, range);
    if (wrapped < T(0))
        wrapped += range;
    // A tiny-negative fmod result can round to exactly `range` after the
    // correction; fold it back so the contract stays right-half-open.
    if (wrapped >= range)
        wrapped = T(0);
    return wrapped;
}

/// Wrap an angle into [0, 2π).
inline float wrapAngleTwoPi(float a) { return wrapToRange(a, kTwoPi); }

/// Wrap an angle into [-π, π).
inline float wrapAnglePi(float a) { return wrapToRange(a + kPi, kTwoPi) - kPi; }
```

The template (not a float overload) is load-bearing: sites 3–4 wrap **double** periods, and routing them through float `IRMath::fmod` would silently degrade long-running `elapsedSeconds_` precision. `std::fmod` inside `engine/math/` is the sanctioned wrapper location. Do **not** remove `IRMath::fmod` (it stays the raw-remainder primitive; engine API removal rule).

**2. Migrations** (behavior-preserving unless noted):

- Site 1 → `float wrapped = IRMath::wrapAngleTwoPi(angle);`
- Site 2 → `return IRMath::max(IRMath::wrapAnglePi(yaw), -IRMath::kPi + 1e-4f);` — **keep the ε-clamp and its comment**; only the fmod+correction+shift collapses.
- Site 5 → `return IRMath::wrapAnglePi(2.0f * IRMath::atan2(q.z, q.w));`
- Sites 3, 4 → `IRMath::wrapToRange(launch.elapsedSeconds_, launch.periodSeconds_)` (deduces `T = double`; existing `periodSeconds_ > 0` guards already precede both calls).
- Site 6 → `IRMath::wrapToRange(motion.timeSeconds_, motion.periodSeconds_)` — identical for the non-negative accumulator, and fixes the `std::fmod` violation.
- Site 7 → delete local `wrapToPi`, call `IRMath::wrapAnglePi` at line 235; update the comment at line 321. Boundary note: the while-loop kept `+π` (closed), `wrapAnglePi` maps it to `-π` (half-open) — the same physical angle mod 2π; the value feeds rotation deltas, so this is safe. Say so in the commit message.

**3. Rule text** — one row in the `.claude/rules/cpp-math.md` "What to use instead" table:

| Don't write | Write |
|---|---|
| `std::fmod(x, p)` + `if (v < 0) v += p`, or `while` ±2π loops | `IRMath::wrapToRange(x, p)` / `IRMath::wrapAngleTwoPi(a)` / `IRMath::wrapAnglePi(a)` |

(`.claude/rules/` is not gated self-config; #2399 merged edits to this file through the normal PR flow.)

**4. Tests** — new `test/math/ir_math_wrap_test.cpp`, registered in `test/CMakeLists.txt`:

- `wrapToRange`: in-range identity; negative input lands in `[0, range)`; multi-cycle input; `x == range` → 0; tiny-negative float input (e.g. `-1e-10f`, range `kTwoPi`) stays **strictly** `< range` (the fold-back edge); double instantiation with a large elapsed (e.g. `1e6 + 0.25`, period `1.0`) → `0.25` within double tolerance.
- `wrapAngleTwoPi`: negative angle, `2π` → 0, many-cycle input.
- `wrapAnglePi`: `0 → 0`; `π → -π`; `-π → -π`; `3π → -π`; values just below `π` stay positive.

### Affected files

- `engine/math/include/irreden/ir_math.hpp` — add `wrapToRange` + two angle wrappers (needs `<type_traits>` if not already included)
- `engine/prefabs/irreden/update/components/component_periodic_idle.hpp` — site 1
- `engine/prefabs/irreden/render/camera.hpp` — sites 2, 5
- `engine/prefabs/irreden/update/systems/system_action_animation.hpp` — site 3
- `engine/prefabs/irreden/update/systems/system_spring_platform.hpp` — site 4
- `engine/prefabs/irreden/render/components/component_triangle_canvas_background.hpp` — site 6
- `engine/prefabs/irreden/render/systems/system_gizmo_drag.hpp` — site 7 (delete local helper)
- `.claude/rules/cpp-math.md` — one table row
- `test/math/ir_math_wrap_test.cpp` — new
- `test/CMakeLists.txt` — register the new test file

### Acceptance criteria

- Zero `std::fmod` remains outside `engine/math/` and `tools/**` (grep-clean).
- Zero hand-rolled fmod-or-while wrap-corrections remain at the seven sites; `system_gizmo_drag.hpp` no longer defines `wrapToPi`.
- `IrredenEngineTest` builds and passes headless (all new wrap cases green), on any host.
- `wrapYaw` still returns strictly `> -π` (ε-clamp preserved — pitchFromQuat contract).
- Engine builds for a render target (e.g. `fleet-build --target IRShapeDebug`) — the touched headers are include-heavy.

### Gotchas

- **Do not route sites 3/4 through float** — that is the one way this mechanical task can silently regress runtime behavior (rhythm-sync timing drift as `elapsedSeconds_` grows).
- The `wrapped >= range` fold-back is not paranoia: `fmod(-1e-10f, kTwoPi) + kTwoPi` rounds to exactly `kTwoPi` in float, which would violate the `[0, range)` contract and, downstream, `valueAtAngle`'s stage lookup.
- `wrapAnglePi` composes as `wrapToRange(a + kPi, kTwoPi) - kPi`; since `wrapToRange` is strictly `< kTwoPi`, the result is strictly `< kPi` — no extra guard needed there.
- Behavior deltas to disclose in the PR body (both intentional, both safe): site 7's `+π ↦ -π` boundary change; site 6 gains negative-input correctness it never exercises today.
