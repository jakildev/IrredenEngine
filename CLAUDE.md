# Irreden Engine

Isometric voxel game engine built on an archetype-based ECS. C++ handles the
engine, systems, and pipelines. Lua 5.4 (via sol2) drives entity creation and
game logic in creations.

`AGENTS.md` has the full long-form architecture reference. Module-specific
details live in `CLAUDE.md` files nested under `engine/`, `engine/prefabs/`,
and `creations/` — read the most specific one for whatever directory you're
working in.

Private implementations (games, editors, experiments) layered on top of the
engine may live in their own gitignored subdirectories under `creations/` and
may define their own conventions, review criteria, workflows, or even their
own repo. The skills in `.claude/skills/` are engine-level and generic; when
working inside such a subdirectory, always read that subdirectory's own
`CLAUDE.md` first — its rules override the engine baseline for that scope.

---

## Workflow: parallel agents + PRs

This repo runs a parallel-agent workflow. The rules:

1. **Never commit to `master` directly.** Always work on a short-lived feature
   branch, typically named `claude/<area>-<topic>`.
2. **Commit + open a PR via the `commit-and-push` skill.** It branches if
   needed, runs `simplify`, writes the message, pushes, and calls `gh pr
   create` for you. Do **not** bypass and `git push origin master`.
3. **After opening a PR, run the `start-next-task` skill before continuing.**
   It resets the worktree to a fresh branch off `origin/master`. Do not keep
   adding unrelated commits to the same PR branch.
4. **A separate reviewer agent** (running the `review-pr` skill in its own
   worktree) looks at each PR. The user merges.
5. **Never `--force` push to `master`.** Never use `--no-verify` to skip hooks
   unless the user explicitly asks.
6. **Shared task queue lives in `TASKS.md`.** Pick the next unblocked item
   from there rather than inventing work.

See `TASKS.md` for the current queue and `.claude/skills/` for the exact
commit/PR/review flows.

### Model split: Opus for core, Sonnet for the fleet

The user has much more Sonnet budget than Opus budget. Spend each where it
pays off:

**Opus 4.6** — use for:

- Core engine architecture. ECS design, ownership and lifetime rules,
  render pipeline decisions.
- `engine/render/`, `engine/entity/`, `engine/system/`, `engine/world/`,
  `engine/audio/`, `engine/video/`, `engine/math/` optimization work.
- FFmpeg integration, GPU-buffer lifetime, anything concurrency-sensitive.
- "Why is this frame 4 ms slower" debugging and long-range reasoning about
  invariants.
- **Final review** on any PR that touches core-engine invariants, even after
  a Sonnet first pass.

**Sonnet 4.6** — use for:

- Writing unit tests against a clear spec (test generation is pattern-heavy
  and the compiler/tests are the oracle).
- Documentation passes: header doc comments, README sections, per-file
  summaries.
- Mechanical refactors: rename-across-codebase, extract-header, convert-
  to-smart-pointer, add-logging.
- **First-pass code review.** Style, obvious bugs, missing null checks,
  naming inconsistencies, untested branches.
- Clearly-scoped items from `TASKS.md` that have already been thought through
  by Opus or the user.
- Gameplay / creation-level work where mistakes are cheap to catch.

When tagging tasks in `TASKS.md`, mark them `[opus]` or `[sonnet]`. If a
Sonnet agent picks up a task and it turns out to be subtler than expected,
stop and escalate — the cost of running out your Opus budget on routine work
is much higher than the cost of one handoff.

Two-tier review is legitimate and encouraged: Sonnet catches the obvious
stuff cheaply, Opus looks at what's left. Don't skip the Opus second pass
for anything in the "Opus" list above.

---

## Build

The project is configured under `build/` using the `windows-debug` CMake
preset (MinGW Makefiles, MSYS2 GCC at `C:\msys64\mingw64\bin\c++.exe`). The
configured build tree is already present — do not reconfigure unless the
user asks.

**Important:** the build tree is configured against the **main worktree**
(`C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine`), not against any
`.claude/worktrees/...` worktree. When editing C++/GLSL/Lua sources from
inside a worktree, you must edit the file at the **main worktree path** for
the change to take effect in the build. (Or, ask the user to reconfigure the
build against your worktree.) Markdown and skill files can be edited in-
worktree freely.

**Canonical build command** from the Bash tool (PATH fix is mandatory — see
below):

```bash
cmd.exe /c "set PATH=C:\\msys64\\mingw64\\bin;%PATH% && \"C:/Program Files/CMake/bin/cmake.EXE\" --build C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine/build --target IRShapeDebug -- -j4" 2>&1
```

Swap the `--target` for whichever executable(s) you need, e.g. `IRShapeDebug`,
`IRCreationDefault`, or a creation-specific target.

### ⚠ Critical: `cc1plus` silent-crash root cause

The Bash tool inherits a Windows `PATH` where `C:\Program Files\Git\mingw64\bin`
(shipped with Git-for-Windows) appears **before** `C:\msys64\mingw64\bin`. When
`gcc.exe` spawns `cc1.exe`/`cc1plus.exe`, the Windows DLL loader picks up Git's
older mingw runtime DLLs while GCC's own come from MSYS2 — an ABI mismatch that
kills `cc1plus` **silently with zero output on any stream**. Symptoms: `gcc
--version` works, but actually compiling anything exits 1 with no output.

**Mandatory fix:** every Bash-tool invocation of the compiler / `cmake --build`
/ anything downstream **must** wrap the command so MSYS2's mingw64 is prepended
to `PATH` inside `cmd /c`:

```bash
cmd.exe /c "set PATH=C:\\msys64\\mingw64\\bin;%PATH% && <your build command>" 2>&1
```

Setting `PATH=/c/msys64/mingw64/bin:$PATH` in bash alone is **not enough** —
MSYS path translation re-injects Git's mingw64 back to the front when the
child Win32 process is spawned. You need the `set PATH=` inside `cmd /c` to
guarantee the DLL loader sees MSYS2's `mingw64\bin` first.

The user's VSCode terminal does not have Git's mingw64 on PATH, so builds work
there unconditionally. Never tell the user "something is wrong with your GCC"
based on a Bash-tool-only failure — first verify with the PATH prefix.

### Build-hygiene canary

A `[100%] Built target` line **does not prove anything compiled** — if nothing
was dirty, `make` prints the same success lines without invoking the compiler.
When investigating build-breaking edits, look for at least one `[xx%] Building
CXX object …` line. If there is none, `touch` the translation unit you care
about to force a real compile.

**Known shell limitation:** `cc1plus` diagnostics sometimes stream through the
Bash tool with heavy buffering. If output looks suspiciously empty after the
PATH fix, redirect to a file inside the `cmd /c`
(`> C:\Users\evinj\AppData\Local\Temp\build.log 2>&1`) and then `cat` the file.

Utility targets:

```bash
cmake --build build --target format        # auto-format
cmake --build build --target format-check  # check only
cmake --build build --target lint          # clang-tidy
```

**Before declaring a task done: always build the target you touched and run
the relevant executable at least once.** Untested commits are the single
biggest waste of reviewer-agent time.

---

## Running an executable

Each `add_executable(...)` has a Windows POST_BUILD step that copies
CMake-tracked runtime DLLs next to the `.exe`. However, the MinGW C++ runtime
(`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`) and FFmpeg
DLLs (`avcodec-*`, `avformat-*`, `avutil-*`, `swscale-*`) are toolchain-
supplied and live at `C:\msys64\mingw64\bin` — they are **not** copied next to
the exe. The Windows DLL loader needs that directory on `PATH`.

From the Bash tool:

```bash
cd "C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine/build/creations/demos/shape_debug" && \
PATH="/c/msys64/mingw64/bin:$PATH" cmd.exe /c "set PATH=C:\\msys64\\mingw64\\bin;%PATH% && .\IRShapeDebug.exe"
```

The double `PATH` set is intentional. Run from the exe's own directory —
that's where its sibling DLLs and `data/`, `shaders/`, `scripts/` live.

Each creation typically defines an `IR<Name>Run` custom target that builds +
launches with the correct working directory.

---

## ECS — the single biggest footgun

**Never** call `getComponent` or `getComponentOptional` on individual entities
inside a system's per-entity tick function. Each call is a hash-map lookup, a
linear scan of the archetype, and another hash-map lookup — at scale this
dominates the frame.

Fix: add the component to the system's template parameters so it iterates the
dense column directly. Alternatives (in order of preference):

1. Include the component in `createSystem<...>` template params.
2. Cache the data in an existing component at creation time.
3. Use `beginTick` / `endTick` for once-per-frame lookups.
4. Use `relationTick` for per-parent-group lookups.

See `engine/system/CLAUDE.md` for the full tick-function-signature story.

---

## Naming (applies everywhere)

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

Prefer descriptive names over abbreviations (`viewCenterIso` not `vcIso`).
Use a lowercase `detail` namespace for header-only helpers under the owning
namespace (`IRSystem::detail`, `IRRender::detail`). Don't use anonymous
namespaces in headers; keep them in `.cpp`.

---

## Style (applies everywhere)

- Early return over nested logic.
- `unique_ptr` over `shared_ptr`; raw pointer = non-owning.
- `std::string` over C buffers unless a low-level API requires otherwise.
- No per-entity `getComponent` inside system tick functions.
- Don't add abstractions for one-time operations; don't design for hypothetical
  future requirements.
- Don't add error handling, fallbacks, or validation for scenarios that can't
  happen. Trust internal code. Only validate at system boundaries.

---

## Project layout (pointers to deeper CLAUDE.md files)

```
engine/                        # Core static libs — see engine/CLAUDE.md
  entity/   system/   world/   # ECS runtime
  math/     common/            # Shared primitives
  render/   audio/   video/    # IO and GPU
  script/   input/   command/  # Scripting + input
  window/   time/    profile/  asset/
  prefabs/irreden/             # Header-only components/systems/commands/entities
    common/ update/ voxel/ input/ render/ audio/ video/
creations/                     # Applications — see creations/CLAUDE.md
  demos/   editors/   template/
```

Each of the listed directories has its own `CLAUDE.md` with the module-
specific patterns, gotchas, and file maps. Read the most specific one for
whatever you're touching. Creations layered on top of the engine (including
gitignored private implementations) define their own conventions in their
own `CLAUDE.md` — when working there, those override the engine baseline.
