# CODE-CONVENTIONS — source-code rules for engine + creations

Part of the agent baseline ([`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md)), read
on demand: read this file before editing source code under `engine/`,
`creations/`, `test/`, `tools/` or `scripts/`. Each section keeps the heading
of its stub in the baseline; a creation that opts out of that heading opts
out of the section here.

---

## Naming

| Context           | Convention                                         |
|-------------------|----------------------------------------------------|
| Private members   | `m_` prefix                                        |
| Public members    | trailing `_`                                       |
| Components        | `C_` prefix                                        |
| Enum values       | `SCREAMING_SNAKE_CASE`                             |
| Compute shaders   | `c_` prefix                                        |
| Vertex shaders    | `v_` prefix                                        |
| Fragment shaders  | `f_` prefix                                        |
| Geometry shaders  | `g_` prefix                                        |
| Header helpers    | nested `detail` namespace (not anonymous, not feature-named) |
| Include guards    | `<NAME>_H` from the file basename (not `_HPP`, not `#pragma once`) |
| Globals / thread-locals | `g_` / `t_` — [`.claude/rules/cpp-globals.md`](../../.claude/rules/cpp-globals.md) |

Descriptive names over abbreviations (`viewCenterIso`, not `vcIso`).
Header-only helpers go in a lowercase `detail` namespace under the owning
namespace (`IRSystem::detail`); anonymous namespaces stay in `.cpp` files.
`_H` is the guard form for `.hpp` files too — do not "fix" it to `_HPP`. No
prototype-phase infixes (`Test`, `New`, `Tmp`) in production function names;
`Debug` is fine when it names a real debug feature.

---

## Style

- Early return over nested logic.
- `unique_ptr` over `shared_ptr`; raw pointer = non-owning.
- `std::string` over C buffers unless a low-level API requires otherwise.
- No abstractions for one-time operations; no design for hypothetical
  future requirements.
- No error handling, fallbacks, or validation for scenarios that can't
  happen. Trust internal code; validate at system boundaries.
- **`enum class` over strings for closed categorical fields** (light type,
  text alignment, system name): `enum class T : int { SCREAMING_SNAKE_CASE = 0, ... }`.
  Strings are for human-readable text, paths, and external interop.
- **Components hold data; systems do work.** A component method reads or
  writes only its own fields; anything that looks up another entity by a
  stored `EntityId` belongs in a system, a `Prefab<>::create` builder, or a
  prefab-scoped `IRPrefab::Foo::` namespace. Tiers and exceptions:
  `engine/prefabs/CLAUDE.md` §"Component method rules".
- **Prefer culling, alloc-free reuse, and GPU-side computation over CPU-side
  dirty-flag bookkeeping.** For per-frame render work, in order: cull with
  the fixed iso camera angle and known lighting axes; reuse containers
  across frames without releasing capacity; derive render data on the GPU
  from buffers already there; content-addressable memoization only as a
  documented stopgap with a follow-up issue; dirty flags last, with a test
  proving every input mutation invalidates the cached output
  ([`.claude/rules/cpp-ecs.md`](../../.claude/rules/cpp-ecs.md) §"No dirty flags on components").
- **All math primitives flow through `IRMath`.** `glm::*` and `std::min` /
  `std::max` / `std::clamp` / `std::abs` / `std::cos` / `std::lround` / etc.
  appear only inside `engine/math/`; add a missing wrapper there first.
  Substitution table and detector:
  [`.claude/rules/cpp-math.md`](../../.claude/rules/cpp-math.md).
- **No new mutable namespace-scope variables in headers** (`inline` or
  `extern`); every process- or world-scoped mutable object has an owner, a
  lifecycle, and an accessor —
  [`.claude/rules/cpp-globals.md`](../../.claude/rules/cpp-globals.md).
- **Comments explain the code, never its history.** A comment states a
  contract, invariant, or gotcha the code cannot say for itself; never what
  the code does, how it changed, where related code sits, or an issue or PR
  number. Test: if the code had always existed in this form, would you
  still write the sentence? If not, it belongs in the commit message, the
  PR body, or `docs/design/`. The full rule, the keeper test, and the
  executed ratchet: [`.claude/rules/comments.md`](../../.claude/rules/comments.md).
  `simplify` greps new diffs for location narration.
- **The same rules apply to the Python under `scripts/`.** ruff enforces
  PEP8 spacing, import order, unused imports, and bare-`assert` guards
  (`ruff check scripts/`, CI-gated — BUILD.md §"Python (scripts)"); the
  comment rule's issue-reference ratchet covers Python and shell too, and
  intent-not-narration stays reviewer-enforced in every language.

---

## Encode contracts in code, not in comments

A documented precondition — `// must be exactly 4 bytes`, `// caller
guarantees non-null`, `// only call from the render thread` — is enforced
with `IR_ASSERT` / `static_assert` / `if consteval`, not by the comment
alone: the comment explains why, the assert makes a violation loud. This is
the narrow complement of "don't validate what can't happen" — if you wrote
"must be X", the next line makes "not X" a crash. The assertion message
restates the constraint in the docstring's words.

- Compile-time constraint on a literal or template argument →
  `static_assert`.
- Runtime constraint on a parameter → `IR_ASSERT(predicate, "diagnostic")`
  at entry, including unwritten invariants such as parallel containers that
  must agree in length.
- Shared helper whose violated precondition is UB (div-by-zero, OOB) →
  the helper asserts its own precondition; a low-level helper called from
  many modules *is* a system boundary, and UB is platform-divergent.
- Helper reachable from both `constexpr` and runtime → `if consteval`
  branch that `static_assert`s, runtime `IR_ASSERT` otherwise.
- Precondition coupling module-level constants → `static_assert` beside the
  constants, in exact integer arithmetic, asserting the structural identity
  (`bias == kDepthEncodeShift`, not `bias == 8`); a shader-side constant is
  mirrored into the C++ constants header and asserted there
  (`ir_render_types.hpp` is the precedent). A constant that gains a second
  requirement later asserts it at the definition site.
- Value crossing an untrusted boundary (Lua, deserialized file, network) →
  clamp or reject at the boundary; `IR_RELEASE` strips asserts. Adding an
  `IR_ASSERT` to a type a Lua binding constructs, or a binding to an
  asserting setter, means checking the binding surface either way. Log a
  warning on the clamped path when the call is not per-frame.

Which assert: runtime → `IR_ASSERT(predicate, "diagnostic")` (engine log
sink, stripped under `IR_RELEASE`); compile-time → raw `static_assert` with
a message (there is no `IR_STATIC_ASSERT`); raw `<cassert>` `assert()`
never; thread affinity → `IR_ASSERT_MAIN_THREAD()`.

An invariant no automated run exercises (run-mode flag combinations,
hand-paired table indices, cross-file constraints on a field) gets a
load-bearing `static_assert` or startup assert, proven by restoring the bad
value and watching it fail; a test asserting an exclusion, and the matcher
(regex, glob set, allowlist) carrying it, owe the same proof — the excluded
token sits where the matcher scans and the test goes red when the exclusion
is deleted (VALIDATION.md "Positive control"). A comment naming a runtime
guard ("trips the `x < n` range assert") is read as fact: the guard exists
on every path the comment's scope implies, and since `IR_ASSERT` is
release-stripped, the comment says which build config. A guard, clamp, or
asymmetry whose rationale lives in another file says so at its own site, and
a declaration-order invariant lists its consumers at the declaration
(`.claude/skills/simplify/SKILL.md` §8 keeps `simplify` from reading such a
guard as deletable hygiene).

---

## Deprecation markers

When an API, file format, helper, or module surface is being replaced and
the replacement has shipped or has a concrete landing plan, mark it in three
places: `// DEPRECATED — use <X> instead.` above the declaration; a
`## Deprecated` section in the module's `CLAUDE.md` naming the surface, the
replacement, and the marking PR or issue; and a flag in any new design doc
that proposes to extend the surface. A task that proposes to extend a
deprecated surface escalates to the human (the usual answer is to redirect
to the replacement). Removal of ECS surface still needs human sign-off per
the Engine API removal rule; deprecation is the slower "going away" signal.

Transition shims — code intentionally dead once a known milestone lands
(compatibility aliases, legacy-artifact cleaners, deprecated-knob
fallthrough) — carry `// Remove once <phase> lands (epic #N)` at the
declaration so the cleanup pass is self-guided.
