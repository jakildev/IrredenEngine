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
   from there rather than inventing work. **Only the queue-manager agent
   edits `TASKS.md`** — author agents must never include TASKS.md changes
   in their feature PRs (this causes merge conflicts across all parallel
   PRs). Reference the task title in your PR description instead.

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

### Cross-platform parity (OpenGL ↔ Metal)

The fleet can run from either a **WSL2 Ubuntu** host (Linux,
OpenGL backend via `linux-debug`) or a **macOS** host (Metal backend
via `macos-debug`), or both simultaneously. Running on both sides in
parallel is how we mature the two graphics backends in lockstep.

New rendering work usually lands on whichever backend the author
happened to be running at the time. That creates drift — a GLSL
compute shader that has no MSL counterpart, or vice versa. The
`backend-parity` skill exists to catch and close those gaps.

Rules:

- After any render PR that touched only one backend, run the
  `backend-parity` skill **on the host matching the lagging side**
  (macOS to add Metal; WSL/Windows to add OpenGL).
- A parity port is not complete until it **builds clean on the lagging
  preset** and the target demo **renders at functional parity** with
  the leading backend. Build-only is not enough.
- One logical feature per parity PR. Don't bundle unrelated parity
  fixes — reviewer agents can't usefully sign off on them.
- Parity work that touches `engine/math/`, dispatch-grid helpers, GPU
  buffer lifetime, or anything where the two backends share a CPU-side
  feeder struct is **Opus work**. Sonnet-fleet agents should escalate.

See `.claude/skills/backend-parity/SKILL.md` for the full flow, the
GLSL↔MSL cheatsheet, and `engine/render/CLAUDE.md` for the pipeline
overview each port must respect.

### Verifying render changes

Any PR that touches `engine/render/src/shaders/`, `engine/prefabs/irreden/render/systems/`,
or pipeline ordering must run the **`render-debug-loop`** skill after
the change and attach a before/after screenshot pair to the PR body.
The skill drives any creation that supports `--auto-screenshot` (today:
`shape_debug`) and carries topic-indexed diagnosis tables for trixel /
SDF shapes, lighting, and backend parity. See `engine/render/CLAUDE.md`
"Verifying render changes" for the exceptions list.

---

## Build

The engine has **three supported environments**, each with its own CMake
preset:

| Preset          | Target                                              | Role                            |
|-----------------|-----------------------------------------------------|---------------------------------|
| `linux-debug`   | Linux / WSL2 Ubuntu, gcc-13+, OpenGL                | **Fleet environment**           |
| `windows-debug` | Windows native, MSYS2 mingw64, OpenGL               | Original / ship-it environment  |
| `macos-debug`   | macOS native, Metal backend                         | Matured on demand               |

**Fleet lives in WSL.** The parallel-agent workflow runs inside a WSL2
Ubuntu distro; the agents' Bash tool shells into Linux natively and
uses the `linux-debug` preset. The Windows-native clone at
`C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine` still exists and
is still authoritative for "does this actually ship", but day-to-day
agent work happens in the WSL clone (typically at
`~/src/IrredenEngine`). See `docs/AGENT_FLEET_SETUP.md` for the full
environment setup.

Most of the engine code was originally written against Windows +
MSYS2. **Maturing the Linux build is first-class ongoing work** — when
an agent hits a Linux-only build break, the correct response is to fix
it in a dedicated PR, not to work around it.

### Which build path applies to you

- **Are you running inside WSL (the fleet)?** → use the Linux section
  below. Your Bash tool is real Linux; no `cmd.exe` wrapping, no PATH
  fixes. Work against `~/src/IrredenEngine`.
- **Are you running on the Windows-native clone?** → use the Windows
  section below. The MSYS2 / Git mingw64 PATH saga applies to you.
- **macOS?** → `cmake --preset macos-debug` + `fleet-build --target <name>`
  once deps are installed via homebrew. The details haven't been
  stress-tested recently — flag anything you hit as you go.

In all three cases:

- **Each worktree has its own build tree.** `fleet-build` auto-detects
  the worktree root (`git rev-parse --show-toplevel`) and uses
  `<worktree>/build/`. If the build hasn't been configured yet,
  `fleet-build` runs `cmake --preset` automatically. Agents edit
  files in their own worktree and build there — no need to touch the
  main clone.
- `CMAKE_CXX_STANDARD` is **23**; your compiler must support it.
  (gcc ≥ 13 on Linux/WSL, gcc ≥ 13 via MSYS2 on Windows.)

### Linux / WSL build (fleet environment)

One-time configure from the fleet clone:

```bash
cd ~/src/IrredenEngine
cmake --preset linux-debug
```

Canonical build command from the Bash tool:

```bash
cmake --build build --target IRShapeDebug -j$(nproc)
```

**Fleet agents: use `fleet-build` instead.** The `$(nproc)` command
substitution triggers Claude Code's `command_substitution` security
gate, which blocks unattended operation with an interactive prompt.
The `fleet-build` wrapper handles parallelism automatically:

```bash
fleet-build --target IRShapeDebug
```

Swap `--target` for whichever executable or library you're working on
(`IRCreationDefault`, `IrredenEngineTest`, a creation-specific target).
No wrapping, no PATH workarounds — `gcc-13`, `cmake`, `ninja`, `make`
are all on the normal Linux PATH inside WSL.

**Expect Linux-specific breakage.** The engine compiles cleanly on
Windows/MSYS2; the Linux path is still being matured. Common first-time
issues and how to attack them are documented in
`docs/AGENT_FLEET_SETUP.md` §10.

Utility targets (Linux):

```bash
cmake --build build --target format        # auto-format
cmake --build build --target format-check  # check only
cmake --build build --target lint          # clang-tidy
```

### Windows-native build (original environment)

The Windows clone is configured under `build/` using the `windows-debug`
CMake preset (MinGW Makefiles, MSYS2 GCC at `C:\msys64\mingw64\bin\c++.exe`).
The configured build tree is already present — do not reconfigure unless
the user asks.

**Canonical build command** from the Bash tool on Windows native (PATH
fix is mandatory — see below):

```bash
cmd.exe /c "set PATH=C:\\msys64\\mingw64\\bin;%PATH% && \"C:/Program Files/CMake/bin/cmake.EXE\" --build C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine/build --target IRShapeDebug -- -j4" 2>&1
```

#### ⚠ Critical: `cc1plus` silent-crash root cause (Windows only)

On the Windows-native clone, the Bash tool inherits a Windows `PATH`
where `C:\Program Files\Git\mingw64\bin` (shipped with Git-for-Windows)
appears **before** `C:\msys64\mingw64\bin`. When `gcc.exe` spawns
`cc1.exe`/`cc1plus.exe`, the Windows DLL loader picks up Git's older
mingw runtime DLLs while GCC's own come from MSYS2 — an ABI mismatch
that kills `cc1plus` **silently with zero output on any stream**.
Symptoms: `gcc --version` works, but actually compiling anything
exits 1 with no output.

**Mandatory fix:** every Bash-tool invocation of the compiler /
`cmake --build` / anything downstream **must** wrap the command so
MSYS2's mingw64 is prepended to `PATH` inside `cmd /c`:

```bash
cmd.exe /c "set PATH=C:\\msys64\\mingw64\\bin;%PATH% && <your build command>" 2>&1
```

Setting `PATH=/c/msys64/mingw64/bin:$PATH` in bash alone is **not
enough** — MSYS path translation re-injects Git's mingw64 back to the
front when the child Win32 process is spawned. You need the `set PATH=`
inside `cmd /c` to guarantee the DLL loader sees MSYS2's `mingw64\bin`
first.

This whole section **does not apply in WSL** — WSL has a single clean
Linux PATH, no Git-for-Windows mingw64 to fight. If you're running in
the fleet, skip it.

The user's VSCode terminal does not have Git's mingw64 on PATH, so
builds work there unconditionally. Never tell the user "something is
wrong with your GCC" based on a Bash-tool-only failure on
Windows-native — first verify with the PATH prefix.

### Build-hygiene canary (both environments)

A `[100%] Built target` line **does not prove anything compiled** — if
nothing was dirty, `make` prints the same success lines without invoking
the compiler. When investigating build-breaking edits, look for at
least one `[xx%] Building CXX object …` line. If there is none, `touch`
the translation unit you care about to force a real compile.

**Known shell limitation (Windows only):** `cc1plus` diagnostics
sometimes stream through the Bash tool with heavy buffering. If output
looks suspiciously empty after the PATH fix, redirect to a file inside
the `cmd /c` (`> C:\Users\evinj\AppData\Local\Temp\build.log 2>&1`) and
then `cat` the file. Linux/WSL builds don't have this buffering issue.

**Before declaring a task done: always build the target you touched and
run the relevant executable at least once.** Untested commits are the
single biggest waste of reviewer-agent time.

---

## Running an executable

**Fleet agents: use `fleet-run` instead of `cd <dir> && ./<exe>`.**
The `cd && command` pattern triggers Claude Code's compound-command
security gate and blocks unattended operation. `fleet-run` finds the
executable in the build tree, cd's into its directory (so sibling
`data/`, `shaders/`, `scripts/` are on its CWD), and runs it:

```bash
fleet-run IRShapeDebug
fleet-run IrredenEngineTest --gtest_brief=1
```

`fleet-run` auto-detects the build directory using the same logic as
`fleet-build` (worktree root → `<root>/build`).

### On Linux / WSL (fleet)

No runtime-DLL drama. Binaries are ELF files; deps resolve through the
normal dynamic linker using `rpath` (CMake sets this automatically for
targets in `build/`).

WSLg routes GLFW/OpenGL windows to the Windows host automatically on
Windows 11 and recent Windows 10 — no X server setup required. Audio
goes through PulseAudio passthrough. **MIDI** hardware from the Windows
side does not cleanly forward into WSL's ALSA yet; for demos that need
a real MIDI controller, run them from the Windows-native clone
instead. See `docs/AGENT_FLEET_SETUP.md` §11 for the full WSLg
caveats.

### On Windows native

Each `add_executable(...)` has a Windows POST_BUILD step that copies
CMake-tracked runtime DLLs next to the `.exe`. However, the MinGW C++
runtime (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
`libwinpthread-1.dll`) and FFmpeg DLLs (`avcodec-*`, `avformat-*`,
`avutil-*`, `swscale-*`) are toolchain-supplied and live at
`C:\msys64\mingw64\bin` — they are **not** copied next to the exe. The
Windows DLL loader needs that directory on `PATH`. `fleet-run` handles
the working-directory requirement, but you still need
`C:\msys64\mingw64\bin` on PATH for the MinGW runtime DLLs.

Each creation typically defines an `IR<Name>Run` custom target that
builds + launches with the correct working directory (on both
platforms).

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

## What belongs in CLAUDE.md files (applies everywhere)

CLAUDE.md files document **concepts, constraints, and gotchas** — things
that aren't obvious from reading the code. They are NOT inventories.

**Do NOT include:**
- File/directory tree listings or layout blocks. Agents can Glob/Grep.
- Catalogs of type, class, or struct names. Agents can Grep for them.
- Variable or field name references (e.g., `m_foo`, `kBar`). Grep.
- Lists of "files in this module" that just mirror `ls`.

**DO include:**
- Design decisions and their rationale ("we use X because Y").
- Constraints and invariants not obvious from the code ("never call
  getComponent inside a tick function — here's why").
- Gotchas and footguns that have bitten before.
- Conceptual relationships that span multiple directories ("the trixel
  pipeline spans prefabs/render, prefabs/update, and shaders/glsl").
- Pipeline or ordering constraints that affect correctness.
- Code examples that demonstrate a **pattern** (e.g., tick-function
  signatures) — use illustrative names, but the pattern is the point.

Names are fine when they're part of a pattern example or a gotcha where
the specific name is the actionable fix. They're clutter when they're
just listing what exists. The test: "would this section survive a
rename refactor, or would it go stale?" If it would go stale, it
doesn't belong.

---

## Bash tool rules (applies everywhere, all agents)

**Every Bash invocation must be a single, simple command.** Never use
shell compound operators (`&&`, `||`, `;`, `|`) to chain commands.
Issue each command as its own separate Bash tool call, or use the
Read/Glob/Grep tools instead of Bash when possible.

- **No `cd <path> && git ...`** — use `git -C <path> ...` instead.
  `cd && git` triggers a hardcoded Claude Code security gate
  ("Compound commands with cd and git require approval to prevent
  bare repository attacks") that **cannot be suppressed** by any
  allowlist or setting. It always prompts interactively.
- **No `cat file || echo fallback`** — use the Read tool for files.
- **No `cmd1 | cmd2`** — run `cmd1`, read the output, then run `cmd2`
  if needed.
- **No `cmd > file`** (the `>` overwrite redirect operator, to any
  path) — Claude Code's Bash tool blocks shell `>` redirects
  regardless of destination. The gate is on the redirect operator
  itself, not the path. (The `>>` append form used for audit logs
  is distinct and may be fine in specific documented cases.) Run the command alone;
  the Bash tool returns stdout in its output (and auto-persists
  large outputs to a side file you can Read on the next iteration
  via the `<persisted-output>` link). If you need a file on disk,
  use the **Write** tool with the captured content — that's a
  different mechanism and does honor `additionalDirectories`.
- **No `sed -n 'N,Mp' file`** — use the Read tool with `offset` and
  `limit` parameters instead. `sed` triggers its own security gate.
- **Use `git -C`** for any git operation on a repo other than the
  current working directory.
- **Use `--repo owner/name`** for any `gh` operation on a repo other
  than the current working directory.
- **Use the Grep tool** instead of `grep` via Bash. The built-in Grep
  tool is already allowlisted and doesn't require approval.
- **Use the Glob tool** instead of `find`. Glob supports patterns like
  `**/*.hpp` and is already allowlisted.
- **Use `--jq`** on `gh` commands instead of piping to `python3` or
  `jq`. Example: `gh pr list --json number,title --jq '.[] | "#\(.number) \(.title)"'`
- **No `git show ref:file | sed/head/tail`** — run `git show ref:file`
  alone, or use `git -C <path> log`/`git -C <path> diff` with
  appropriate flags instead of piping.

This rule exists because the user-level allowlist (`~/.claude/settings.json`)
matches on the first token of each Bash command. A compound command like
`cd path && git log` starts with `cd`, not `git`, so it won't match
`Bash(git:*)` and will always prompt. The bare-repo check for `cd && git`
is an additional hardcoded gate on top of that.

---

## Cross-repo information isolation (applies everywhere, all agents)

**The engine repo (`jakildev/IrredenEngine`) is public. The game repo
(`jakildev/irreden`) is private.** Information flows one way only:

- **Game-side artifacts MAY reference engine.** The engine is public,
  so a game PR description, commit message, issue, review comment,
  or `TASKS.md` blocker can freely cite engine PRs/issues/files. Game
  agents already do this when filing engine issues for cross-repo
  dependencies.
- **Engine-side artifacts MUST NOT reference game.** Anything the
  engine repo publishes — PR titles, PR descriptions, commit
  messages, review comments, `TASKS.md` entries, GitHub issue bodies
  filed on the engine — is world-readable. Leaking the game's task
  IDs, feature names, design language, file paths, or repo slug
  exposes private game work.

**Concretely, when filing or writing anything that lands in the
engine repo, never include:**

- Game task IDs (`game T-005`, or unqualified `T-NNN` IDs that came
  from the game queue).
- Game PR or issue URLs (`jakildev/irreden#41`, etc.).
- The game repo slug `jakildev/irreden`.
- File paths under `creations/game/` (or any other gitignored
  creation that lives in its own repo — `creations/<name>/`).
- Game-specific design language, feature names, mechanics, or
  systems by their game-side names. Talk in engine-level terms only:
  "the prefab system needs a relation cache" rather than "the ant
  pheromone trails need a relation cache for diffusion".

**Cross-repo dependencies — the right pattern:**

1. The work that lands in the engine PR is described in **pure
   engine terms** — generic capabilities, no game-specific
   motivation. The engine task in `TASKS.md` is self-contained.
2. The game-side PR's description / TASKS.md entry references the
   engine task by ID or PR URL as a `Blocked by:` dependency.

This is the existing convention for `Blocked by:` (see queue-manager
role, "Cross-repo work" section). The information-isolation rule
generalizes that direction: engine talks engine, game talks both.

**`commit-and-push` checks for this** before opening an engine PR
— it greps the staged diff and the PR body for game-leakage tokens
and warns. Treat the warning as blocking unless the leakage is
intentional and the user explicitly approves.

---

## Issue/PR labeling discipline (applies everywhere, all agents)

When filing a GitHub issue (`gh issue create`) or PR (`gh pr create`)
on either repo, **do not pre-apply state labels**. Every fleet label
has an owner that's allowed to set it; agents filing new artifacts
are not in that owner set.

Specifically, **never pass these via `--label` when filing**:

- `human:approved` — owned by the **human**. The human's "yes, work on
  this" gate. Queue-manager keys ingestion off it.
- `fleet:queued` / `fleet:task` — owned by the **queue-manager**, set
  AFTER it ingests an issue into `TASKS.md`. Adding it at filing time
  excludes the issue from queue-manager's triage search and strands
  it (observed on issues #270-#273, #287).
- `fleet:approved` / `fleet:needs-fix` / `fleet:has-nits` /
  `fleet:blocker` — owned by the **reviewer agents** as PR verdicts.
- `fleet:wip` — owned by the **author agent** (set at PR creation
  during a normal `commit-and-push` flow). Don't add to issues.
- `fleet:in-progress` / `fleet:merger-cooldown` /
  `fleet:changes-made` — owned by the worker / merger pipeline.

**The right pattern when filing an issue:** create it with NO labels.
The human will add `human:approved` if and when they want it picked
up. The queue-manager will add `fleet:queued` (or `fleet:needs-plan`
/ `fleet:needs-info`) on the next triage pass after that.

**Exception:** if you're operating in a role's own lane (e.g. you
ARE the queue-manager and you've just ingested an issue, or you ARE
a reviewer and you've just verdict'd a PR), then setting your role's
labels is correct. The rule above is about ad-hoc issue/PR filing
from human conversations.

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
