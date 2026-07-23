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

```
pattern: '^\s*(inline|extern)\s+(?!(constexpr|const|void)\b)[^(]*[;={]'
glob:    '**/*.{hpp,h}'
```

The `[^(]*` guard drops function declarations; classify surviving hits by
hand. Allowlisted paths (the sanctioned patterns above): module entry
points `engine/*/include/irreden/ir_*.hpp` and
`engine/include/irreden/ir_engine.hpp`. Everything else that matches is a
violation — route it to the pattern that fits the state kind per the table.

## Live deviations

- `system_update_joint_matrices.hpp::g_jointMatrixSystem` and
  `system_update_voxel_positions_gpu.hpp::g_allocatorSystem` — wire-once
  system handles; migrate to the `SystemManager` registry (#2526).
- `widget_theme.hpp::g_defaultTheme` — mutate-once widget theme; migrate
  to a `C_WidgetTheme` singleton component (#2527).

Don't add new violations; these migrate via the tracked issues. Don't
migrate them in an unrelated PR — the issues carry the plans.
