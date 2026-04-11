# creations/

Where executables live. Each subdirectory is one binary (or one family
of binaries) that links against the engine static libraries. The engine
knows nothing about any specific creation.

## Two visibility tiers

```
creations/
├── demos/                  ← checked into git; shareable examples
├── bazel_test/             ← gitignored
├── editors/                ← gitignored
└── <private creations>/    ← gitignored (games, experiments, tools)
```

The top-level `.gitignore` says:

```
creations/*
!creations/demos/
!creations/demos/**
!creations/CLAUDE.md
```

Everything directly under `creations/` is ignored **except**
`creations/demos/` and this file. Private creations (games, personal
tools, experimental editors) live locally and are never pushed to the
engine repo. A private creation may have its own repo, its own
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

Every creation's `CMakeLists.txt` does three things:

1. **Declare the executable** and link `IrredenEngine`.
2. **Copy runtime data** — `engine/render/data`, `engine/data`,
   `engine/render/src/shaders` next to the exe as `data/` and
   `shaders/`, plus any creation-specific scripts.
3. **Copy runtime DLLs** — Windows-only POST_BUILD step using
   `$<TARGET_RUNTIME_DLLS:...>`. Note that MinGW runtime DLLs
   (`libgcc_s_seh-1.dll`, etc.) are *not* copied — they come from
   `C:\msys64\mingw64\bin` and must be on `PATH` at runtime.
4. **Define an `IR<Name>Run` custom target** with
   `WORKING_DIRECTORY` set to the runtime dir so VSCode launches it
   from the right cwd.

Use `creations/demos/shape_debug/CMakeLists.txt` as the canonical
reference.

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
