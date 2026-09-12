# BUILD — environments, presets, run paths

The full build/run reference; the top-level [`CLAUDE.md`](../../CLAUDE.md)
keeps the one-screen quick-ref. Host setup (WSL2, macOS, native Windows):
`docs/AGENT_FLEET_SETUP.md`.

## Environments

| Preset          | Target                                | Role                           |
|-----------------|---------------------------------------|--------------------------------|
| `linux-debug`   | Linux / WSL2 Ubuntu, gcc-13+, OpenGL  | **Fleet environment**          |
| `windows-debug` | Windows native, MSYS2 mingw64, OpenGL | Original / ship-it environment |
| `macos-debug`   | macOS native, Metal backend           | Matured on demand              |

Inside WSL (the fleet) use the Linux section — your Bash tool is real Linux,
no `cmd.exe` wrapping, no PATH fixes, working against `~/src/IrredenEngine`.
On the Windows-native clone use the Windows section; its PATH fix is
mandatory. On macOS: `cmake --preset macos-debug` then
`fleet-build --target <name>` once deps are installed via Homebrew. A
Linux-only build break is fixed in a dedicated PR, not worked around.

In all three:

- **Each worktree has its own build tree.** `ir-build` (and the `fleet-build`
  shim) auto-detects the worktree root (`git rev-parse --show-toplevel`),
  uses `<worktree>/build/`, and runs `cmake --preset` itself on first use.
- `CMAKE_CXX_STANDARD` is **23** (gcc ≥ 13 on Linux/WSL and MSYS2).
- **Doc snippets that cite a preset:** if the recipe's expected cwd is not
  the engine root, pass `-S <engine-root>` explicitly (`cmake --preset`
  alone fails without a `CMakePresets.json` in cwd), and a snippet naming
  one platform's preset carries a hint comment
  (`# or linux-debug / windows-debug for your platform`).

### Downstream-creation worktree builds

Downstream creations (gitignored repos under `creations/<name>/`) run their
own worktrees at `creations/<name>/.claude/worktrees/<agent>/` with no CMake
presets of their own. `ir-build` / `ir-run` route them automatically:

- **Detection** — the invoker's git toplevel lacks `CMakePresets.json` and
  sits under an engine root at `creations/<name>/…`.
- **Build dir** — `<engine>/build-<creation>-<agent>/` (covered by the
  engine `.gitignore`'s `build-*/`); nothing lands in the creation repo.
- **Auto-configure** (first build) — the host preset against the enclosing
  engine source with `-DIRREDEN_BUILD_CREATIONS=OFF` (so the creation's main
  checkout can't collide with the worktree's identically named targets) and
  `-DIRREDEN_USER_PROJECTS=<worktree>`.
- **Routing** — every later `fleet-build --target X` / `fleet-run X` from
  that cwd resolves the same dir; `ir-run --targets` lists from it.

The engine source for that tree is the enclosing clone at whatever state it
is checked out — keep it on `master` on fleet hosts. The creation's main
checkout (`creations/<name>/` itself) builds through `<engine>/build/` via
the engine root's nested-checkout include. `IRREDEN_BUILD_DIR` overrides
the build dir everywhere.

### Dedicated game build dir against a specific engine worktree (`build-game`)

A fleet worker building a game PR (a game-side semantic conflict,
role-worker step 1c) builds against **its own engine worktree**, not the
enclosing main clone: a one-time manual configure of a dedicated dir, reused
across iterations via `IRREDEN_BUILD_DIR`:

```bash
ENG=<your-engine-worktree-abs-path>
GAME_WT=~/src/IrredenEngine/creations/game/.claude/worktrees/<your-worktree-name>
# one-time configure (skip if $ENG/build-game/CMakeCache.txt exists):
cmake -S "$ENG" --preset <host>-debug -B "$ENG/build-game" -DIRREDEN_USER_PROJECTS="$GAME_WT"
# build the affected project's target via ir-build, pointed at that dir:
IRREDEN_BUILD_DIR="$ENG/build-game" fleet-build --target IRIrredenAll
```

`<host>-debug` is your host preset. `-S "$ENG"` is required because the
game-PR flow runs from the game worktree, where a bare `--preset` fails with
`Could not read presets from <game-worktree>: File not found`. `ir-build`
never auto-configures a dir bound to a specific engine worktree, and there
is no `--build-dir` flag — `IRREDEN_BUILD_DIR` is the knob. Pick the
`IR<Project>All` target matching what the PR touches; never build
`IRGameAll` (its midi dependency is broken).

**Stale cache.** `CMakeCache.txt` records `CMAKE_CACHEFILE_DIR` and
`CMAKE_HOME_DIRECTORY` as absolute paths:

- Same worktree, only `IRREDEN_USER_PROJECTS` or the preset changed →
  reconfigure in place with the full command above, **including
  `--preset`** (without it only the one variable is patched and a preset
  switch is silently not applied). A generator switch (`windows-debug` is
  `MinGW Makefiles`; the others `Unix Makefiles`) cannot happen in place —
  it errors `generator : X Does not match the generator used previously`
  and takes the next bullet.
- Worktree renamed or migrated → CMake refuses (`The current CMakeCache.txt
  directory … is different than the directory … where CMakeCache.txt was
  created`). Remove and reconfigure:
  `rm -rf "$ENG/build-game" && cmake -S "$ENG" --preset <host>-debug -B "$ENG/build-game" -DIRREDEN_USER_PROJECTS="$GAME_WT"`.
  If removal is blocked by a permission layer, use a fresh sibling dir
  (`$ENG/build-game-<agent>`) and point `IRREDEN_BUILD_DIR` at it.

### `ir-build` / `ir-run` (canonical) vs `fleet-build` / `fleet-run` (aliases)

`engine/tools/bin/ir-build` and `ir-run` are canonical (shared by the fleet,
a solo dev, and CI); `scripts/fleet/fleet-build` and `fleet-run` are
one-line shims that `exec` them. `ir-build` wraps `cmake --build` in
`ir-acquire cpu N`; `ir-run` wraps `--auto-screenshot` runs in
`ir-acquire gpu` and `--auto-profile` in `ir-acquire benchmark`, so parallel
workers serialize on the CPU budget or split it (`IR_FLEET_WORKERS=2` → each
build caps at `budget/2`). Either name works.

## Linux / WSL build (fleet environment)

```bash
cd ~/src/IrredenEngine
cmake --preset linux-debug          # one-time configure
fleet-build --target IRShapeDebug
```

`fleet-build` wraps `cmake --build build --target <T> -j$(nproc)`; the
`$(nproc)` substitution trips Claude Code's `command_substitution` gate, so
fleet agents use the wrapper. Swap `--target` for any executable or library
(`IRCreationDefault`, `IrredenEngineTest`, a creation target). `gcc-13`,
`cmake`, `ninja`, `make` are on the normal PATH. First-time Linux issues:
`docs/AGENT_FLEET_SETUP.md` §10.

Utility targets:

```bash
cmake --build build --target format          # whole tree
cmake --build build --target format-changed  # current-branch diff lines only
cmake --build build --target format-check    # check only
cmake --build build --target lint            # clang-tidy
cmake --build build --target header-checks   # header-global / namespace bans
```

`format-changed` rewrites only the lines your branch modified (committed vs
`@{upstream}` plus working tree), so a touched file's pre-existing drift
stays out of your diff; brand-new files are formatted in full. Bare `format`
is for intentional cleanup PRs only.

### Python (scripts)

Everything under `scripts/` is linted by **ruff**
(CI: `.github/workflows/python-lint.yml`):

```bash
ruff check scripts/          # PEP8 + import-order + unused + bare-assert
ruff check --fix scripts/    # autofix import-order / unused imports
```

`ruff.toml` pins the rules and enumerates the extension-less Python
executables explicitly (the largest `fleet-*` scripts are bash). Install:
`brew install ruff` (macOS), `pipx install ruff` (Linux),
`pacman -S mingw-w64-x86_64-ruff` (MSYS2).

## Windows-native build (original environment)

`build/` is configured with `windows-debug` (MinGW Makefiles, MSYS2 GCC at
`C:\msys64\mingw64\bin\c++.exe`); do not reconfigure unless asked.

`fleet-build` / `fleet-run` internalize the PATH fix below and resolve the
`.exe` plus runtime DLLs:

```bash
fleet-build --target IRShapeDebug
fleet-run IRShapeDebug --auto-screenshot 10
```

The raw form they run, for debugging the wrappers themselves:

```bash
cmd.exe /c "set PATH=C:\\msys64\\mingw64\\bin;%PATH% && \"C:/Program Files/CMake/bin/cmake.EXE\" --build C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine/build --target IRShapeDebug -- -j4" 2>&1
```

### `cc1plus` silent crash (Windows only)

The Bash tool inherits a `PATH` with Git-for-Windows'
`C:\Program Files\Git\mingw64\bin` **before** `C:\msys64\mingw64\bin`, so
`cc1plus` loads Git's older mingw runtime DLLs and dies **silently with no
output**: `gcc --version` works, compiling exits 1 with nothing on any
stream. Every compiler / `cmake --build` invocation from the Bash tool
prepends MSYS2 inside `cmd /c`:

```bash
cmd.exe /c "set PATH=C:\\msys64\\mingw64\\bin;%PATH% && <your build command>" 2>&1
```

`PATH=/c/msys64/mingw64/bin:$PATH` in bash alone is not enough — MSYS path
translation re-injects Git's mingw64 when the Win32 child spawns.

> **Interactive terminal:** in a real MSYS2 / Git-Bash terminal POSIX path
> conversion rewrites a standalone `/c` to `C:/`, opening `cmd`
> interactively. Type `//c` by hand (it collapses back to `/c`); `ir-build`
> uses `//c` internally. Don't wrap a no-space path in `"…"` inside the
> `//c` string — the quotes leak into the argument.

Not applicable in WSL. The user's VSCode terminal lacks Git's mingw64 on
PATH, so builds work there unconditionally — a Bash-tool-only failure on
Windows-native is checked with the PATH prefix before anything else is
blamed.

### Debugging under gdb (Windows only)

A process under `gdb` runs with the Windows debug heap. A mingw
emutls-shaped crash (a `thread_local` destructor faulting during
`DLL_THREAD_DETACH`) that appears only under the debugger: pass
`_NO_DEBUG_HEAP=1` in the debuggee's environment to reach the bug you were
chasing. It is a diagnostic workaround, not a fix — the use-after-free is
real and surfaces in a non-debugger run once the freed block is reused.

### Running the fleet on native Windows

The bash+tmux fleet runs natively (host key `windows`): orchestration from
an **MSYS2 bash** shell (`pacman -S tmux jq`), agent panes run `claude`
whose Bash tool is Git Bash; both share `$HOME`, so `~/.fleet` is common.

```bash
bash scripts/fleet/setup-windows.sh        # FLEET_CLONE / FLEET_CPU_BUDGET overridable
```

Idempotent: clones a dedicated fleet checkout, writes
`~/.fleet/fleet-up.conf` + `~/.config/irreden/host.toml`, puts the tool dirs
on PATH, creates the worktrees (no Developer Mode needed, unlike
`install.sh`'s symlinks). Then `fleet-up dry-run` from the clone,
`tmux attach -t fleet`, `fleet-up live`. See FLEET.md "Cross-platform
parity".

## Build-hygiene canary (both environments)

`[100%] Built target` does not prove anything compiled — `make` prints it
with nothing dirty. When investigating a build-breaking edit, look for a
`[xx%] Building CXX object …` line; if none, `touch` the translation unit to
force a real compile.

**Windows only:** `cc1plus` diagnostics sometimes stream through the Bash
tool heavily buffered. If output looks empty after the PATH fix, redirect
inside the `cmd /c` (`> C:\Users\evinj\AppData\Local\Temp\build.log 2>&1`)
and `cat` the file.

## Running an executable

`fleet-run` finds the executable in the build tree (same worktree detection
as `fleet-build`), cd's into its directory so sibling `data/`, `shaders/`,
`scripts/` are on cwd, and runs it — `cd <dir> && ./<exe>` trips the
compound-command gate:

```bash
fleet-run IRShapeDebug
fleet-run IrredenEngineTest --gtest_brief=1
```

### Timeout choices

Two modes; never mix them:

- **`--auto-screenshot` demos** — omit `--timeout`:
  `fleet-run <demo> --auto-screenshot 10`. Auto-screenshot calls
  `closeWindow()` when the shot sequence is done; a `--timeout` would report
  "alive at deadline" as success and mask a hang.
- **Everything else** (interactive demos, test binaries, profiling runs) —
  `fleet-run --timeout 15 <exe>`; omitting it blocks indefinitely.

`fleet-run --targets` lists runnable names (add `--plan` for CMake demo/test
targets); `fleet-help` indexes every `fleet-*` tool and
`fleet-help <command>` opens per-tool help.

### On Linux / WSL (fleet)

ELF binaries resolve deps through `rpath` (set by CMake for targets in
`build/`). WSLg routes GLFW/OpenGL windows to the Windows host on Windows 11
and recent Windows 10; audio via PulseAudio passthrough. **MIDI** hardware
does not forward into WSL's ALSA — run MIDI-controller demos from the
Windows-native clone. WSLg caveats: `docs/AGENT_FLEET_SETUP.md` §11.

### On Windows native

Each `add_executable` has a POST_BUILD step that copies CMake-tracked
runtime DLLs next to the `.exe`. The MinGW runtime (`libgcc_s_seh-1.dll`,
`libstdc++-6.dll`, `libwinpthread-1.dll`) and FFmpeg DLLs (`avcodec-*`,
`avformat-*`, `avutil-*`, `swscale-*`) are toolchain-supplied from
`C:\msys64\mingw64\bin` and are **not** copied — that directory must be on
`PATH`. Each creation typically defines an `IR<Name>Run` target that builds
and launches with the correct working directory.

## Packaging a distributable bundle

A creation wired with `irreden_package_target(<target> ...)`
(`cmake/ir_functions.cmake`) gains a `<target>Package` target:

```bash
cmake --build <build-dir> --target <target>Package
# -> <build-dir>/<target>-<platform>-<arch>.zip
```

The archive holds one `<target>/` folder — the executable, its `data/`,
`shaders/`, `scripts/` (the exe-relative layout `IREngine::init` resolves
via `current_path(exeDir)`), and the platform runtime libraries — so it
runs by double-click on a clean box. `IRShapeDebug` is the reference.

- **Linux** — FetchContent deps default to static (top-level
  `CMakeLists.txt`); `$ORIGIN` rpath covers any remaining `.so`.
- **Windows** — bundles `$<TARGET_RUNTIME_DLLS>` plus the MinGW runtime trio
  (which `TARGET_RUNTIME_DLLS` omits).
- **macOS** — `cmake/macos_bundle_dylibs.cmake` copies non-system dylibs
  next to the exe, rewrites load commands to `@executable_path`, and ad-hoc
  re-signs. Shallow: transitive Homebrew deps (ffmpeg's codec libraries)
  are not walked, so a macOS bundle is verified via cross-host smoke or the
  human.

Packaging never runs as part of a normal `--target <exe>` build.
