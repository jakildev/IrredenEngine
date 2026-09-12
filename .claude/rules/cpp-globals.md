# Global state: sanctioned patterns and the header-global ban

Rule:

> **Never** introduce a new mutable namespace-scope variable in a header
> (`inline` or `extern`). Every process- or world-scoped mutable object lives
> behind one of the sanctioned patterns below — each has an **owner**, a
> **lifecycle**, and an **accessor**.

Allowed at namespace scope in headers: `constexpr` / `const` compile-time
constants. On a pointer declaration `const` must appear on **both** ends —
`inline const T *const p` is a constant; `inline const T *p` (reseatable) and
`inline T *const p` (frozen handle to mutable data) are state and banned. A
`*` inside a template argument (`std::array<const char *, N>`) belongs to the
type argument and does not make the object a pointer.

## Sanctioned patterns

| State kind | Pattern | Owner / lifecycle |
|---|---|---|
| Module manager (process-singular subsystem) | `extern Manager *g_<x>` declared in the module's `ir_<module>.hpp` entry point, defined in the module `.cpp`; the manager's own ctor stamps `this`, its dtor clears-if-self; access via the asserting free function (`IREntity::getEntityManager()`) | `World` owns every manager as a member in dependency order — member order IS the set/clear order |
| Engine process context | `inline` variables in `engine/include/irreden/ir_engine.hpp` (`g_world`, `g_scriptsDir`, ...), set once in `IREngine::init()`, wrapped by accessors | `IREngine` entry points |
| Process infrastructure (logger, profiler, CLI args, GL dispatch table, Metal runtime) | Meyers singleton (`static X x; return x;`) or intentionally-leaked `instance()` where shutdown-order robustness demands it (leak documented at the site) | lazy first-use → process lifetime |
| World-scoped settings / game state (mutate-once config, per-world globals) | singleton component via `IREntity::singleton<T>()` | ECS-owned; preserved across `resetGameplay`, torn down with the world — see `engine/entity/CLAUDE.md` §"Singleton components" |
| System wiring (find a registered system by name) | the `SystemManager` `SystemName -> SystemId` registry (`IRSystem::findSystem`) | dies with `World` |
| Per-thread identity | `thread_local` in a `.cpp` behind an accessor (`IRJob::workerId()`) | thread lifetime |
| Module-internal state | anonymous-namespace variable in a `.cpp` | translation unit; never a header |

Naming: `g_` prefix at namespace/file scope, `t_` for `thread_local`. This
file is the canonical home for those two prefixes; the general naming table
is `docs/agents/CLAUDE-BASELINE.md` §"Naming".

The manager-global pattern is deliberate: the globals are private
implementation detail behind free-function module APIs, so the storage
mechanism can change inside `ir_<module>.cpp` without touching call sites. A
header global plus a "creation must call `setX(id)` once at init" contract is
the delegated bookkeeping `cpp-ecs.md` §"System-owned invariants" bans, and
spelling it as an `inline` variable relocates the unowned state without
giving it an owner.

## Detection

Executed, not hand-grepped: `cmake/run_header_convention_checks.cmake` runs
this ban together with the anonymous-namespace and `*Detail`-namespace bans,
tree-wide, over every first-party header — including the generated GL
wrapper (`engine/render/include/irreden/render/gl_wrap/`) and the Metal
backend, which the style tools skip. Every consumer that feeds the executor
passes `irreden_collect_quality_files(... INCLUDE_RENDER_BACKENDS)`; the two
style-tool lists — `format*` / clang-tidy, and the `format-check` CI shim
`cmake/run_clang_format_changed_standalone.cmake` — are the legitimate narrow
calls (clang-format is a style tool, not a correctness gate). Only vendored
code (`engine/render/third_party/metal-cpp/`, `build/`, `_deps/`,
`third_party/`) is excluded.

```
cmake --build <build-dir> --target header-checks                              # pure CMake
cmake --build <build-dir> --target lint                                       # + clang-tidy (no CI path)
cmake -DPROJECT_ROOT=<repo-root> -P cmake/run_header_checks_standalone.cmake  # the CI gate
```

Matcher contract: `constexpr` / `const` (including `inline static const`)
are judged on the **declaration head** — everything before `=` / `;` / `{`,
with a declaration whose terminator wraps onto a continuation line joined
first — never on the whole line; line and block comments (both `/* ... */`
shapes) are stripped before matching, so a commented-out declaration is dead
code, not a violation; `extern "C"` blocks and function declarations pass;
the module entry points `engine/*/include/irreden/ir_*.hpp` and
`engine/include/irreden/ir_engine.hpp` are allowlisted. Keep the executor
and this file in sync.

## Live deviations

**None.**

Add an entry — with its tracking issue — when a sweep finds a violation that
cannot be migrated on the spot; drop it when the issue closes. The list
mirrors `header_global_baseline` in
`cmake/run_header_convention_checks.cmake`; update both together. A
baselined path is skipped by the scan entirely, and the baseline is a
ratchet: a file may leave it, never join it. Don't migrate a deviation in an
unrelated PR — the issue carries the plan.

The sibling anonymous-namespace ban (`cpp-ecs.md` §"Naming") has its own
ratchet, `anonymous_namespace_baseline`, with one entry:

- `engine/render/include/irreden/render/gl_wrap/GLAPITrace.h` — generated by
  `GetGLAPI.py`; its file-scope `namespace { GL4API apiHook; }` is included
  by exactly one TU (`engine/render/src/gl_wrap/GLAPITrace.cpp`), so it is
  not a live ODR hazard.
