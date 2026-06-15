# creations/

Where executables live. Each subdirectory is one binary (or one family
of binaries) that links against the engine static libraries. The engine
knows nothing about any specific creation.

## Inherits from engine baseline

Applies the rules in [`docs/agents/CLAUDE-BASELINE.md`](../docs/agents/CLAUDE-BASELINE.md).
No opt-outs.

## Two visibility tiers

`creations/demos/` and `creations/editors/voxel_editor/` are checked into
the engine repo. All other subdirectories under `creations/` (games,
personal tools, experimental editors) are gitignored and live only locally.
The top-level `.gitignore` tracks only these two subtrees; see it for the
exact allowlist. A private creation may have its own repo, its own
conventions, its own review criteria, even its own CLAUDE.md hierarchy.

## Engine vs. creation separation

The engine skills (`.claude/skills/commit-and-push`, `review-pr`,
`start-next-task`) are written generically and apply to all engine-level
work. A private creation that wants different conventions should:

1. Add a `CLAUDE.md` (and optionally a `REVIEW.md`) at its root.
2. The engine's `review-pr` skill reads the nearest `CLAUDE.md`/
   `REVIEW.md` under the PR's changed paths and applies creation-
   specific checks on top of engine-level ones.
3. A private creation may ship its own commit/review/push skills in a
   local repo; the engine-level skills will still work for engine
   changes.

## What lives under each creation

Minimum for a C++-only creation:

- `CMakeLists.txt`
- `main.cpp`

Minimum for a Lua-driven creation:

- `CMakeLists.txt`
- `main_lua.cpp`
- `lua_bindings.hpp/.cpp` (or similar)
- `lua_component_pack.hpp` — picks which component `*_lua.hpp` headers
  are actually compiled in and bound.
- `scripts/*.lua` — loaded at runtime from the exe's cwd.
- `main.lua` — optional top-level entry script.

See `creations/demos/CLAUDE.md` for how this actually looks in practice
and `creations/template/` for a minimal starter (if present; template
is gitignored).

## CMake boilerplate

Use `creations/demos/shape_debug/CMakeLists.txt` as the canonical reference.
It wires the exe-relative runtime layout (data/ shaders/ scripts/ + Windows
DLLs) via `irreden_bundle_assets(<target> SCRIPTS ...)` and a one-command
distributable bundle via `irreden_package_target(<target>)` — both in
`cmake/ir_functions.cmake`; prefer them over hand-copied `add_custom_command`
blocks. See [`docs/agents/BUILD.md`](../docs/agents/BUILD.md) §"Packaging a
distributable bundle".

Note: MinGW runtime DLLs (`libgcc_s_seh-1.dll`, etc.) are not copied by
the Windows `$<TARGET_RUNTIME_DLLS:...>` POST_BUILD step — they come from
`C:\msys64\mingw64\bin` and must be on `PATH` at runtime.

## Gotchas

- **Gitignore is aggressive.** `creations/*` ignores everything. If you
  create a new demo under `creations/demos/`, it is auto-included, but
  a new private creation directly under `creations/` is not. Don't be
  surprised when `git status` shows nothing after creating a new
  subdirectory.
- **Runtime CWD matters.** Creations load `data/`, `shaders/`, and
  `scripts/` relative to the exe. Always launch from the exe's own
  directory (the `IR<Name>Run` target does this correctly).
- **Don't include internal engine headers.** Only pull from
  `ir_*.hpp` umbrellas. If a creation needs something deeper, expose
  it through the umbrella first.
- **Private creations are on their own git.** A crash in a private
  creation's build does not fail engine CI. Engine-level changes must
  still be able to build and run the demos.
