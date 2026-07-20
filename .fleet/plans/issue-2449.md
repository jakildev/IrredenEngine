## Plan: build(macOS): Apple clang 21 breaks fresh builds — fmt/spdlog pair bump (fmt→11.2.0, spdlog→v1.15.3)

- **Issue:** #2449
- **Model:** sonnet — the edit is two version strings; all design/verification judgment is spent in this plan via on-host measurement. (Supersedes the body's `[opus]` placeholder. Escalate per role step 8a only if the fresh build surfaces non-fmt/spdlog clang-21 breaks.)
- **Date:** 2026-07-20

### Scope

Restore fresh-build capability on macOS under Apple clang 21 by moving the fmt/spdlog dependency pair to versions that compile at the engine's C++ standard. **This plan supersedes the issue's recommended `10.1.1 → 10.2.1` bump — that fix was refuted by measurement on the affected host (table below), as was the issue's named alternative (`FMT_USE_CONSTEVAL=0`).**

### Verified current state (measured 2026-07-20 on the outage host)

Host: Apple clang 21.0.0 (clang-2100.1.1.101), arm64. Each probe compiles fmt's two compiled-lib TUs (`src/format.cc`, `src/os.cc`) standalone at **`-std=c++23`** — matching the engine's global `CMAKE_CXX_STANDARD 23` (root `CMakeLists.txt:16`). NB the issue body says C++20; the tree builds at 23, and the C++23-vs-20 delta is material (see the last two rows).

| Probe | Result |
|---|---|
| fmt 10.1.1 (pinned) | **FAIL** — consteval `FMT_STRING` errors (5 in format.cc, 4 in os.cc) |
| fmt 10.2.1 (issue's recommended bump) | **FAIL** — different consteval rejection: `core.h:704 remove_prefix(it - begin())` inside the compile-time checker |
| fmt 11.0.2 | **FAIL** — same class of errors |
| fmt 11.1.4 | PASS |
| fmt 11.2.0 | PASS |
| fmt 12.2.0 | PASS |
| spdlog 1.12.0 (pinned) + fmt 11.2.0 external | **FAIL 0/6 TUs** — `spdlog/common.h:373` expects fmt-10-era `fmt::basic_format_string` via `fmt/core.h` |
| spdlog 1.15.3 + fmt 11.2.0 external | **PASS 6/6 TUs** (`SPDLOG_COMPILED_LIB` + `SPDLOG_FMT_EXTERNAL`) |
| spdlog 1.17.0 + fmt 11.2.0 / 12.2.0 external | PASS 6/6 |
| Engine-API-surface TU (logger ctor + `stdout_color_sink_mt` + `set_pattern` + `register_logger` + `set_level`/`flush_on` + `fmt::format(fmt::runtime(...))`) vs spdlog 1.15.3 + fmt 11.2.0 | PASS |
| fmt 10.1.1 + `-DFMT_USE_CONSTEVAL=0` (issue's alternative) | **FAIL** — that macro does not exist in fmt 10.1.1; consteval errors persist unchanged |
| fmt 10.1.1 + `-DFMT_CONSTEVAL=` (empty — the real 10.1.1 hatch, `core.h:219`) | PASS (with `-Wdeprecated-literal-operator` warnings) |

**Cross-system audit (fmt/spdlog consumers):** no engine or creation source includes fmt directly; fmt enters only transitively via `spdlog/spdlog.h`. The single direct fmt call in the tree is `fmt::format(fmt::runtime(...))` at `engine/profile/include/irreden/profile/ir_profile.tpp:16` — API unchanged in fmt 11. All other use flows through spdlog's public logger API (`engine/profile/src/logger_spd.cpp`: logger ctor from sink iterators, `register_logger`, `set_level`, `flush_on`, sinks) — all stable across spdlog 1.12→1.15. The only version pins anywhere are `engine/profile/CMakeLists.txt:23` (fmt) and `:37` (spdlog); downstream creations carry no pins of their own and inherit the engine's.

### Approach

One file, two lines — `engine/profile/CMakeLists.txt`:

1. fmt `GIT_TAG 10.1.1` → `GIT_TAG 11.2.0` (line 23).
2. spdlog `GIT_TAG v1.12.0` → `GIT_TAG v1.15.3` (line 37). This is a **required pair**: spdlog 1.12 cannot compile against external fmt 11 (measured above), and no fmt ≤10.2.1 compiles under Apple clang 21 at C++23.

Everything else stays as-is: the `SPDLOG_FMT_EXTERNAL` wiring is identical in spdlog 1.15.3 (same option name and default, same `cmake_minimum_required(VERSION 3.10...3.21)`, so CMP0077 behavior is unchanged), and the `IR_copyDLL` artifact names (`libfmtd`, `libspdlogd`) are unchanged upstream.

Version choice: **11.2.0** over 11.1.4 (last patch of the 11.x line, same measured result) and over 12.x (fresh major that removes the `fmt/core.h` forwarder — larger compat surface for zero benefit here). **spdlog v1.15.3** over 1.17.0 (smallest jump that supports external fmt 11; same-era pairing).

**Fallback** (only if the bump hits an unforeseen wall on gcc-13/mingw): keep both pins and add `FMT_CONSTEVAL=` (empty) as a compile definition on the fmt target (`target_compile_definitions(fmt PUBLIC FMT_CONSTEVAL=)`) — measured green on clang 21 at C++23. Note the issue's named spelling `FMT_USE_CONSTEVAL=0` does **not** work on 10.1.1; only this corrected spelling does. It trades away fmt's compile-time format-string checking engine-wide, which is why it is the fallback and not the plan.

### Affected files

- `engine/profile/CMakeLists.txt` — the two `GIT_TAG` bumps (lines 23, 37)
- `.fleet/plans/issue-2449.md` — this plan, committed as the first commit of the impl PR (#1932 flow)

### Acceptance criteria (positive-fire)

1. **macOS (the outage host):** `cmake --preset macos-debug` into a **fresh** build dir (or wipe `_deps/fmt-*` and `_deps/spdlog-*` from an existing one so both deps compile from scratch), then `fleet-build --target IRShapeDebug` compiles fmt 11.2.0 + spdlog 1.15.3 from source and links; the built `IRShapeDebug` runs with `--auto-screenshot` and exits 0. The positive fire is the fresh fmt/spdlog object compiles appearing in the build log where the build previously died at ~12–20% in `fmt-src` — cite those log lines in the PR body.
2. **Downstream-creation build:** a dedicated `build-game` dir pointed at the branch engine worktree (BUILD.md § Dedicated game build dir) configures and builds a representative creation target clean — the downstream-creation build path where this outage first surfaced. (Avoid the aggregate all-target build — it pulls an unrelated known-broken creation dependency.)
3. **Linux non-regression:** `linux-debug` (gcc-13) build of `IRShapeDebug` stays green. Both chosen versions' upstream CI cover gcc-13, so this is expected-pass; verify via the standard `fleet:needs-linux-smoke` cross-host flow rather than blocking the PR on it (the fleet is currently all-macOS).
4. **Log output sanity:** one demo run shows normally formatted, colorized engine log lines (guards against a silent spdlog pattern/sink behavior change across 1.12→1.15).

### Gotchas

- Existing build dirs keep stale fmt-10 objects; FetchContent re-populates when `GIT_TAG` changes on reconfigure. If a dir misbehaves post-bump, wipe its `_deps/fmt-*` + `_deps/spdlog-*` (and stale `CMakeCache.txt` per BUILD.md).
- A fresh macOS build dir also fresh-compiles **every other** FetchContent dep (easy_profiler v2.1.0 etc.) — none of those have ever been exercised under clang 21 (the fmt wall sits in front of them). If one of them breaks, that is **new scope**: file it per TASK-FILING.md and do not grow this PR.
- The `-Wdeprecated-literal-operator` warnings from fmt 10.1.1 headers are gone in 11.x — don't chase them.
- Engine code includes no fmt header directly today; keep it that way in this task.
- Do not "fix" the issue title's `(bump fmt→10.2.1)` by editing the issue — this plan comment supersedes it; the impl PR title should name the real bump.

