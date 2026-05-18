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

- **Each worktree has its own build tree.** `fleet-build` auto-detects
  the worktree root (`git rev-parse --show-toplevel`) and uses
  `<worktree>/build/`. If the build hasn't been configured yet,
  `fleet-build` runs `cmake --preset` automatically. Agents edit
  files in their own worktree and build there — no need to touch the
  main clone.
- `CMAKE_CXX_STANDARD` is **23**; your compiler must support it.
  (gcc ≥ 13 on Linux/WSL, gcc ≥ 13 via MSYS2 on Windows.)

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

---

## Windows-native build (original environment)

The Windows clone is configured under `build/` using the `windows-debug`
CMake preset (MinGW Makefiles, MSYS2 GCC at `C:\msys64\mingw64\bin\c++.exe`).
The configured build tree is already present — do not reconfigure unless
the user asks.

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

This whole section **does not apply in WSL** — WSL has a single clean
Linux PATH, no Git-for-Windows mingw64 to fight. If you're running in
the fleet, skip it.

The user's VSCode terminal does not have Git's mingw64 on PATH, so
builds work there unconditionally. Never tell the user "something is
wrong with your GCC" based on a Bash-tool-only failure on
Windows-native — first verify with the PATH prefix.

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
  The fleet-wide default; 5 s is too short for a demo mid-init.

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
