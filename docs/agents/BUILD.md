# BUILD — environments, presets, run paths

The full build/run reference. The top-level [`CLAUDE.md`](../../CLAUDE.md) keeps a one-screen quick-ref and points here for everything else (Windows-native PATH gotchas, the cc1plus silent-crash root cause, runtime-DLL handling, WSLg caveats).

---

## Environments

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

- **Each worktree has its own build tree.** `ir-build` (and the legacy
  `fleet-build` shim) auto-detects the worktree root
  (`git rev-parse --show-toplevel`) and uses `<worktree>/build/`. If
  the build hasn't been configured yet, `ir-build` runs `cmake
  --preset` automatically. Agents edit files in their own worktree and
  build there — no need to touch the main clone. (Downstream-creation
  worktrees route differently — see the next section.)
- `CMAKE_CXX_STANDARD` is **23**; your compiler must support it.
  (gcc ≥ 13 on Linux/WSL, gcc ≥ 13 via MSYS2 on Windows.)

**Convention for doc snippets that cite a preset** (here and in any doc,
including downstream creation/game repos): if the recipe's expected CWD is
not the engine root, the snippet must pass `-S <engine-root>` explicitly —
`cmake --preset macos-debug` alone fails anywhere without a
`CMakePresets.json`. And a snippet that names one platform's preset should
carry a hint comment (`# or linux-debug / windows-debug for your platform`)
so a reader on another host doesn't hit "preset not found" following it
literally.

### Downstream-creation worktree builds

Downstream creations (gitignored repos nested under `creations/<name>/`)
run their own agent worktrees at `creations/<name>/.claude/worktrees/
<agent>/`. Those worktrees have **no CMake presets of their own** — their
targets compile through the enclosing engine with the worktree added as a
user project. `ir-build` / `ir-run` handle this automatically:

- **Detection** — the invoker's git toplevel lacks `CMakePresets.json`
  and sits under an engine root at `creations/<name>/…`.
- **Build dir** — `<engine>/build-<creation>-<agent>/` (e.g.
  `~/src/IrredenEngine/build-game-pool-2/`), already covered by
  the engine `.gitignore`'s `build-*/` pattern. Build output never lands
  in the creation repo, and the engine's own build trees are untouched.
- **Auto-configure** (first build only) — host preset against the
  enclosing engine source with `-DIRREDEN_BUILD_CREATIONS=OFF` (so the
  creation's *main* checkout can't collide with the worktree's
  identically-named targets) and `-DIRREDEN_USER_PROJECTS=<worktree>`.
- **Routing** — every later `fleet-build --target X` / `fleet-run X`
  from that cwd resolves the same dedicated dir; `ir-run --targets`
  lists from it too.

So from a creation worktree, `fleet-build --target <T>` followed by
`fleet-run <T>` Just Works with zero manual `cmake`. `IRREDEN_BUILD_DIR`
still overrides the build dir everywhere (the escape hatch when you want
the build against a specific engine worktree instead of the enclosing
clone — configure that dir yourself once, then point the env var at it).

Two things to know:

- The engine **source** for the auto-configured tree is the enclosing
  clone (the one physically containing `creations/<name>/`) at whatever
  state it's checked out — keep it on `master` on fleet hosts.
- The creation's **main** checkout (`creations/<name>/` itself, not a
  worktree) builds through `<engine>/build/` via the engine root's
  nested-checkout include.

### Dedicated game build dir against a specific engine worktree (`build-game`)

Fleet workers building a game PR (e.g. resolving a game-side semantic
conflict, role-worker step 1c g) build against **their own engine
worktree**, not the enclosing main clone the auto-configure above would
pick — a stale or concurrently-edited main clone must not decide whether
the resolution compiles. That is the `IRREDEN_BUILD_DIR` escape hatch
with a one-time manual configure of a **dedicated** dir (dedicated so the
game configure can't clobber the engine worktree's own preset build;
idempotent — reuse it across iterations):

```bash
ENG=<your-engine-worktree-abs-path>
GAME_WT=~/src/IrredenEngine/creations/game/.claude/worktrees/<your-worktree-name>
# one-time configure (skip if $ENG/build-game/CMakeCache.txt exists):
cmake --preset <host>-debug -B "$ENG/build-game" -DIRREDEN_USER_PROJECTS="$GAME_WT"
# build the affected project's target via ir-build, pointed at that dir:
IRREDEN_BUILD_DIR="$ENG/build-game" fleet-build --target IRIrredenAll
```

`<host>-debug` is your host preset (`macos-debug` / `linux-debug` /
`windows-debug`). You own this configure: `ir-build` only auto-configures
when it can derive the layout itself (bare preset, or the
creation-worktree detection above, which targets the *enclosing* clone),
so a dir bound to a specific engine worktree must be configured by hand
once as shown. There is no `--build-dir` flag — the `IRREDEN_BUILD_DIR`
env var is the override knob. If the cache in `$ENG/build-game` goes
stale (worktree renamed/migrated, preset change), reconfigure in place:
`cmake -S "$ENG" -B "$ENG/build-game" -DIRREDEN_USER_PROJECTS="$GAME_WT"`.

Target selection stays with the caller (pick the `IR<Project>All` target
matching what the PR touches), as does the **never build `IRGameAll`**
rule (its midi dependency is broken — game #88).

### `ir-build` / `ir-run` (canonical) vs `fleet-build` / `fleet-run` (aliases)

The canonical build/run wrappers live at `engine/tools/bin/ir-build`
and `engine/tools/bin/ir-run` — they coordinate the same machine
across the fleet, a solo dev, and CI, so the canonical home is
engine-tools, not fleet. `scripts/fleet/fleet-build` and
`scripts/fleet/fleet-run` are one-line shims that `exec` into the
canonical names; every existing invocation continues to work.

`ir-build` wraps `cmake --build` in `ir-acquire cpu N`, and `ir-run`
wraps `--auto-screenshot` runs in `ir-acquire gpu` (and
`--auto-profile` in `ir-acquire benchmark`). Two parallel fleet
workers therefore serialize on the configured CPU budget or split it
in half (`IR_FLEET_WORKERS=2` → each build caps at `budget/2` cores)
without any per-call accounting.

Use whichever name you prefer; the rest of this doc mixes them and
both keep working.

---

## Linux / WSL build (fleet environment)

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
cmake --build build --target format          # auto-format (whole tree)
cmake --build build --target format-changed  # auto-format current-branch diff only
cmake --build build --target format-check    # check only
cmake --build build --target lint            # clang-tidy
```

`format-changed` is what you want mid-iteration: it only touches files
your branch has modified (committed vs `@{upstream}` plus working
tree). The bare `format` target rewrites every formattable file in the
repo and should only run on intentional cleanup PRs.

### Python (scripts)

Everything under `scripts/` — the fleet automation plus the render / perf /
gui harnesses — is linted by **ruff**, the Python analogue of
`clang-format`/`clang-tidy`. After touching any Python script, run the
canonical check before committing (the CI `Python lint` step in
`.github/workflows/quality.yml` gates it on every PR):

```bash
ruff check scripts/          # PEP8 + import-order + unused + bare-assert
ruff check --fix scripts/    # autofix import-order / unused imports
```

`ruff.toml` at the repo root pins the rules and the file set (it enumerates
the extension-less Python executables explicitly, since the largest fleet
scripts — `fleet-claim`, `fleet-dispatcher`, `fleet-up`, … — are bash, not
Python; the non-fleet harnesses all carry a `.py` extension and are
auto-discovered). ruff is installed by the bootstrap scripts (`brew install
ruff` on macOS, `pipx install ruff` on Linux, `pacman -S
mingw-w64-x86_64-ruff` on Windows/MSYS2).

---

## Windows-native build (original environment)

The Windows clone is configured under `build/` using the `windows-debug`
CMake preset (MinGW Makefiles, MSYS2 GCC at `C:\msys64\mingw64\bin\c++.exe`).
The configured build tree is already present — do not reconfigure unless
the user asks.

**Fleet agents: use `fleet-build` / `fleet-run`.** On Windows these wrappers
internalize the `cc1plus` PATH fix below (they run cmake inside `cmd /c "set
PATH=…mingw64…"`) and resolve the `.exe` artifact + runtime DLLs automatically —
so a Windows agent calls them exactly like on Linux/macOS:

```bash
fleet-build --target IRShapeDebug
fleet-run IRShapeDebug --auto-screenshot 10
```

The raw `cmake --build` form below is what they run under the hood; reach for it
only when debugging the wrappers themselves.

**Canonical build command** from the Bash tool on Windows native (PATH
fix is mandatory — see below):

```bash
cmd.exe /c "set PATH=C:\\msys64\\mingw64\\bin;%PATH% && \"C:/Program Files/CMake/bin/cmake.EXE\" --build C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine/build --target IRShapeDebug -- -j4" 2>&1
```

### ⚠ Critical: `cc1plus` silent-crash root cause (Windows only)

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

> **Interactive-terminal caveat (`/c` vs `//c`):** the bare `cmd.exe /c`
> above works under Claude's Bash tool, but in a real MSYS2 / Git-Bash
> terminal POSIX path conversion rewrites the standalone `/c` argument into
> `C:/`, so `cmd.exe /c "…"` becomes `cmd.exe C:/ "…"` — which opens `cmd`
> **interactively** instead of running the command. When typing one of these
> by hand in a terminal, use the conversion-proof `//c` idiom (it collapses
> back to `/c` for cmd.exe and works in both contexts). `ir-build` already
> uses `//c` internally for this reason; also avoid embedding `"…"` quotes
> around a no-space path inside the `//c` string — the literal quotes leak
> into the wrapped command's arguments.

This whole section **does not apply in WSL** — WSL has a single clean
Linux PATH, no Git-for-Windows mingw64 to fight. If you're running in
the fleet, skip it.

The user's VSCode terminal does not have Git's mingw64 on PATH, so
builds work there unconditionally. Never tell the user "something is
wrong with your GCC" based on a Bash-tool-only failure on
Windows-native — first verify with the PATH prefix.

### Running the fleet on native Windows

The full bash+tmux fleet runs natively on Windows as a co-equal host (key
`windows`). Orchestration (`fleet-up`/tmux/dispatcher) runs from an **MSYS2
bash** shell (`pacman -S tmux jq`); the agent panes run `claude`, whose Bash
tool is Git Bash — both share `$HOME`, so `~/.fleet` is common.

One-shot, idempotent host setup — clones a dedicated fleet checkout (kept
separate from any interactive dev clone), writes `~/.fleet/fleet-up.conf` +
`~/.config/irreden/host.toml`, puts the tool dirs on PATH, and creates the
worktrees:

```bash
bash scripts/fleet/setup-windows.sh        # FLEET_CLONE / FLEET_CPU_BUDGET overridable
```

Native symlinks (`install.sh`) need Windows Developer Mode; the PATH approach
`setup-windows.sh` uses does not — the scripts resolve their siblings from
their real location either way. The fleet's OpenGL build must be on `master`
before worktrees can build (the cc1plus/LuaJIT fixes above). Then `fleet-up
dry-run` from the clone, `tmux attach -t fleet`, `fleet-up live`. See
[`FLEET.md`](FLEET.md) "Cross-platform parity".

---

## Build-hygiene canary (both environments)

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

### Timeout choices

Two modes; never mix them:

- **`--auto-screenshot` demos** — omit `--timeout`:
  `fleet-run <demo> --auto-screenshot 10`
  Auto-screenshot fires `closeWindow()` when the shot sequence is done.
  Adding `--timeout` would mask hangs: `fleet-run` reports "alive at deadline"
  as success even when `--auto-screenshot` never completes.

- **All other executables** (interactive demos, test binaries, profiling runs) —
  use `--timeout 15`:
  `fleet-run --timeout 15 <exe>`
  15 s gives demos time to init; omitting `--timeout` would block indefinitely.

---

`fleet-run --targets` lists names you can pass to `fleet-run` (built
executables under `creations/` and `test/` by default; add `--plan` for
CMake demo/test targets from `cmake --build --target help`). Same as
`fleet-run-targets` in `scripts/fleet/`.

`fleet-help` prints an index of all `fleet-*` tools (after
`scripts/fleet/install.sh`); `fleet-help <command>` opens per-tool help
when available.

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

## Packaging a distributable bundle

A creation wired with `irreden_package_target(<target> ...)` (see
`cmake/ir_functions.cmake`) gains a `<target>Package` custom target that
produces a self-contained, per-platform, double-clickable bundle in one
command:

```bash
cmake --build <build-dir> --target <target>Package
# -> <build-dir>/<target>-<platform>-<arch>.zip
#    e.g. IRShapeDebug-macos-arm64.zip / IRShapeDebug-linux-x86_64.zip
```

The archive holds a single `<target>/` folder containing the executable,
its `data/`, `shaders/`, and `scripts/` (the exe-relative layout
`IREngine::init` resolves via `current_path(exeDir)`), plus the platform
runtime libraries — so the unzipped folder runs by double-click on a
clean box with no repo, build tree, or `PATH` setup. `IRShapeDebug` is
wired as the reference; the per-platform runtime-dep handling:

- **Linux** — FetchContent deps default to **static** (top-level
  `CMakeLists.txt`), so the exe is largely self-contained; `$ORIGIN`
  rpath covers any remaining `.so`.
- **Windows** — bundles `$<TARGET_RUNTIME_DLLS>` plus the MinGW runtime
  trio (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`),
  which `TARGET_RUNTIME_DLLS` omits (pulled from the compiler's `bin`).
- **macOS** — `cmake/macos_bundle_dylibs.cmake` copies the exe's
  non-system dylibs next to it and rewrites the load commands to
  `@executable_path`, then ad-hoc-resigns the modified binary. This is a
  **shallow** bundle: transitive Homebrew deps (e.g. ffmpeg's codec
  libraries) are not yet walked, so a truly clean-box macOS bundle is
  follow-up work — verify a macOS bundle via cross-host smoke or the
  human, not from the build host alone.

Packaging is a build-time custom target only; it does not run as part of
a normal `--target <exe>` build.
