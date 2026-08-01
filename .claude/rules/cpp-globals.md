# Global state: sanctioned patterns and the header-global ban

Rule:

> **Never** introduce a new mutable namespace-scope variable in a header
> (`inline` or `extern`). Every process- or world-scoped mutable object lives
> behind one of the sanctioned patterns below — each has an **owner**, a
> **lifecycle**, and an **accessor**. A bare header global has none of the
> three.

Allowed at namespace scope in headers: `constexpr` / `const` compile-time
constants. Those are program constants, not state.

## Sanctioned patterns

| State kind | Pattern | Owner / lifecycle |
|---|---|---|
| Module manager (process-singular subsystem) | `extern Manager *g_<x>` declared in the module's `ir_<module>.hpp` entry point, defined in the module `.cpp`; the manager's own ctor stamps `this`, its dtor clears-if-self; access via the asserting free function (`IREntity::getEntityManager()`) | `World` owns every manager as a member in dependency order — member order IS the set/clear order |
| Engine process context | `inline` variables in `engine/include/irreden/ir_engine.hpp` (`g_world`, `g_scriptsDir`, ...), set once in `IREngine::init()`, wrapped by accessors | `IREngine` entry points |
| Process infrastructure (logger, profiler, CLI args, GL dispatch table, Metal runtime) | Meyers singleton (`static X x; return x;`) or intentionally-leaked `instance()` where shutdown-order robustness demands it (leak documented at the site) | lazy first-use → process lifetime |
| World-scoped settings / game state (mutate-once config, per-world globals) | singleton component via `IREntity::singleton<T>()` | ECS-owned; preserved across `resetGameplay`, torn down with the world — see `engine/entity/CLAUDE.md` §"Singleton components" |
| System wiring (find a registered system by name) | the `SystemManager` `SystemName -> SystemId` registry (`IRSystem::findSystem`, #2526) | dies with `World` |
| Per-thread identity | `thread_local` in a `.cpp` behind an accessor (`IRJob::workerId()`) | thread lifetime |
| Module-internal state | anonymous-namespace variable in a `.cpp` | translation unit; never a header |

Naming for the sanctioned forms: `g_` prefix at namespace/file scope, `t_`
for `thread_local`. (This file is the canonical home for these two
prefixes; the general naming table in `docs/agents/CLAUDE-BASELINE.md`
covers members, components, and shaders.)

## Why the ban

- **No owner.** A header global is never cleared at `World` teardown, is
  invisible to scene reset and save/load, and its mutation points are
  unguarded and unfindable. Each one becomes its own mini-convention the
  next reader has to reverse-engineer.
- **Wire-once handles are delegated bookkeeping.** A header global plus a
  "creation must call `setX(id)` once at init" contract makes every
  consumer responsible for the subsystem's invariant — the exact failure
  mode `.claude/rules/cpp-ecs.md` §"System-owned invariants: encapsulate,
  don't delegate to callers" exists to prevent. The subsystem that owns
  the state owns the wiring.
- **The inline-variable trick is not an exemption.** "It's an `inline`
  variable, not a function-local static" does not satisfy the system-state
  rule (`.claude/rules/cpp-systems.md`) — it relocates the unowned state,
  it doesn't give it an owner.

The manager-global pattern itself is deliberate and stays: the globals are
private implementation detail behind free-function module APIs, which is
what keeps the storage mechanism swappable (a future multi-world would
change `ir_<module>.cpp` internals, not call sites).

## Detection

Grep new diff hunks in headers for namespace-scope `inline` / `extern`
declarations that are not `constexpr` / `const`:

**This check is executed** — don't hand-grep it. It lives in
`cmake/run_header_convention_checks.cmake` alongside the anonymous-namespace
and `*Detail`-namespace checks, and runs via either target:

```
cmake --build <build-dir> --target header-checks   # pure CMake, no external tools
cmake --build <build-dir> --target lint            # + clang-tidy
```

In CI it runs as the **Header Checks** workflow
(`.github/workflows/header-checks.yml`) on every push and PR touching the
scanned tree, via `cmake/run_header_checks_standalone.cmake` — same collector,
same rules, no configure step. The workflow is named here on purpose: "this
check is executed" is only worth asserting if the reader can go confirm *what*
executes it. Note that `lint` is not that path — it reaches CI only through
`quality.yml`, which is disabled (#2718).

It reports the offending file and declaration and fails the target. The
matcher allows `constexpr` / `const` (including `inline static const`),
`extern "C"` linkage blocks, and function declarations; allowlisted paths
are the module entry points `engine/*/include/irreden/ir_*.hpp` and
`engine/include/irreden/ir_engine.hpp`.

Two precision notes, both measured against the tree:

- The `const` exemption is scoped to the **declaration head** (everything
  before `=` / `;` / `{`), not the whole line. A whole-line scan reads a
  trailing comment's "const" as a qualifier and passes real globals as clean.
- On a pointer declaration, `const` must appear on **both** ends to qualify
  as a constant. `inline const T *p` is a mutable, reseatable pointer and is
  banned; `inline T *const p` is a frozen handle to still-mutable data and is
  also banned. Only `inline const T *const p` is a program constant. A `*`
  inside a template argument (`std::array<const char *, N>`) belongs to the
  type argument, not the declarator, and does not make the object a pointer.

Keep the executor and this file in sync — a detection spec nothing runs
drifts silently (see #2727).

## Live deviations

- `creations/demos/lighting/common/lighting_demo_scene.hpp` — 13 demo
  CLI/config globals, two wire-once `SystemId` handles, and one scene
  `EntityId` (16 total, as reported by the executor); migrate to a
  singleton component + the `SystemManager` registry (#2728).

This list mirrors `header_global_baseline` in
`cmake/run_header_convention_checks.cmake` — update both together. The
baseline is a ratchet: a file may leave it, never join it. Don't migrate a
deviation in an unrelated PR — the issue carries the plan.
