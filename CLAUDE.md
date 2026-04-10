# Irreden Engine

Isometric voxel game engine built on an archetype-based ECS. C++ handles the engine, systems, and pipelines. Lua 5.4 (via sol2) drives entity creation and game logic in creations.

See `AGENTS.md` for the full architecture reference.

---

## Build

The project is configured under `build/` using the `windows-debug` CMake preset (MinGW Makefiles, MSYS2 GCC at `C:\msys64\mingw64\bin\c++.exe`). The configured build tree is already present — do not reconfigure unless the user asks.

**Important:** the build tree is configured against the **main worktree** (`C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine`), not against any `.claude/worktrees/...` worktree. When editing C++/GLSL/Lua sources from inside a worktree, you must edit the file at the **main worktree path** for the change to take effect in the build. (Or, ask the user to reconfigure the build against your worktree.)

**Canonical build command** from the Bash tool (PATH fix is mandatory — see below):

```bash
cmd.exe /c "set PATH=C:\\msys64\\mingw64\\bin;%PATH% && \"C:/Program Files/CMake/bin/cmake.EXE\" --build C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine/build --target IRGame -- -j4" 2>&1
```

Swap the `--target` list for whichever executable(s) you need, e.g. `IRShapeDebug`, `IRCreationDefault`, `IRGame`.

### ⚠ Critical: `cc1plus` silent-crash root cause (diagnosed 2026-04-10)

The Bash tool inherits a Windows `PATH` where **`C:\Program Files\Git\mingw64\bin`** (shipped with Git-for-Windows) appears **before** `C:\msys64\mingw64\bin`. When `gcc.exe` spawns `cc1.exe` / `cc1plus.exe`, the Windows DLL loader picks up Git's older `libgcc_s_seh-1.dll`, `libgmp-10.dll`, `libwinpthread-1.dll`, `zlib1.dll`, `libzstd.dll`, while GCC's own `libmpc-3.dll`, `libisl-23.dll`, `libmpfr-6.dll` still come from MSYS2 — an ABI mismatch that kills `cc1plus` **silently with zero output on any stream** (stdout, stderr, file redirects, winpty, all empty; exit code 1 or 127).

**Symptoms to recognise:**
- `gcc --version` / `-print-search-dirs` work fine (no cc1 spawn).
- The moment you actually compile a `.c` / `.cpp`, you get exit 1 and **zero** output.
- `ldd` on `C:\msys64\mingw64\lib\gcc\x86_64-w64-mingw32\15.2.0\cc1plus.exe` shows `libgcc_s_seh-1.dll => /c/Program Files/Git/mingw64/bin/...` — that is the smoking gun.

**Mandatory fix:** every Bash-tool invocation of the compiler / `cmake --build` / anything downstream **must** prepend `C:\msys64\mingw64\bin` to the Windows PATH inside the child process. Wrap every build command as:

```bash
cmd.exe /c "set PATH=C:\\msys64\\mingw64\\bin;%PATH% && <your build command>" 2>&1
```

Setting `PATH=/c/msys64/mingw64/bin:$PATH` in bash alone is **not enough** — MSYS path translation re-injects Git's mingw64 back to the front when the child Win32 process is spawned. You need the `set PATH=` inside `cmd /c` to guarantee the DLL loader sees MSYS2's `mingw64\bin` first.

**The user's VSCode terminal does not have Git's mingw64 on PATH**, so builds work there unconditionally. Never tell the user "something is wrong with your GCC" based on a Bash-tool-only failure — first verify with the PATH prefix.

### Build-hygiene canary

A `[100%] Built target` line **does not prove anything compiled** — if nothing was dirty, `make` prints the same success lines without invoking the compiler at all. When investigating build-breaking edits, look for at least one `[xx%] Building CXX object …` line. If there is none, touch the translation unit you care about (e.g. `touch engine/render/src/ir_render.cpp`) to force a real compile, otherwise latent errors stay hidden.

**Known shell limitation:** even with the PATH fix applied, `cc1plus` diagnostics sometimes stream through the Bash tool with heavy buffering. If a build output looks suspiciously empty after the PATH fix, redirect to a file inside the `cmd /c` (`> C:\Users\evinj\AppData\Local\Temp\build.log 2>&1`) and then `cat` the file — that reliably captures everything.

Utility targets:

```bash
cmake --build build --target format        # auto-format
cmake --build build --target format-check  # check only
cmake --build build --target lint          # clang-tidy
```

To reconfigure from scratch (rarely needed):

```bash
cmake --preset windows-debug
```

---

## Running an executable

Each `add_executable(...)` in a creation has a Windows POST_BUILD step that copies all CMake-tracked runtime DLLs next to the `.exe` via `$<TARGET_RUNTIME_DLLS:...>`. **However**, the MinGW C++ runtime (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`) and any FFmpeg DLLs (`avcodec-*.dll`, `avformat-*.dll`, `avutil-*.dll`, `swscale-*.dll`) are toolchain-supplied and live at `C:\msys64\mingw64\bin` — they are **not** copied next to the exe. The Windows DLL loader needs that directory on `PATH`.

The user's VSCode terminal has `C:\msys64\mingw64\bin` on the system `PATH`, so `<exe>Run` targets and direct launches "just work" for them. From the Bash tool here, you must add it explicitly:

```bash
cd "C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine/build/creations/demos/shape_debug" && \
PATH="/c/msys64/mingw64/bin:$PATH" cmd.exe /c "set PATH=C:\\msys64\\mingw64\\bin;%PATH% && .\IRShapeDebug.exe"
```

The double `PATH` set is intentional: the bash form propagates env into `cmd.exe`, and the `set PATH=` form ensures the loader sees a Windows-format path. Run from the exe's own directory — that's where its sibling DLLs and `data/`, `shaders/`, `scripts/` live.

Equivalently, each creation typically defines an `IR<Name>Run` custom target that builds + launches with the correct working directory; this works the same from a VSCode terminal but still needs the PATH fix when invoked from this Bash tool.

---

## Project Layout

```
engine/                        # Core static libs
  prefabs/irreden/             # Header-only components, systems, commands, entities
    common/ update/ voxel/ input/ render/ audio/ video/
creations/                     # Applications and demos
  demos/   editors/   hana_class_projects/   game/   template/
cmake/                         # CMake utility scripts
```

Module entry points: `engine/include/irreden/ir_*.hpp` — always use these, never internal headers.

---

## ECS Conventions

- **Components** — `C_` prefix, public members with trailing `_`, data-only structs, live in `*/components/component_*.hpp`
- **Systems** — lambda-based, registered to a pipeline event (UPDATE / INPUT / RENDER)
- **`SystemId` is an `EntityId`** — systems are ECS entities with `C_SystemEvent`
- **`createEntity` always adds** `C_PositionGlobal3D` and `C_PositionOffset3D`

### Critical anti-pattern — never do this inside a per-entity tick:
```cpp
// BAD: hash-map lookup + linear scan per entity, every frame
auto& foo = getComponent<C_Foo>(id);
```
Fix: add `C_Foo` to the system's template parameters so it iterates the dense column directly.

---

## Naming

| Context | Convention |
|---|---|
| Private members | `m_` prefix |
| Public members | trailing `_` |
| Components | `C_` prefix |
| Enum values | `SCREAMING_SNAKE_CASE` |
| Shader prefixes | `c_` compute, `v_` vert, `f_` frag, `g_` geom |
| Header helpers | `detail` nested namespace (not anonymous, not feature-named) |

Prefer descriptive names over abbreviations (`viewCenterIso` not `vcIso`).

---

## Style

- Early return over nested logic
- `unique_ptr` over `shared_ptr`; raw pointer = non-owning
- `std::string` over C buffers unless a low-level API requires otherwise
- No per-entity `getComponent` inside system tick functions
- Don't add abstractions for one-time operations; don't design for hypothetical future requirements

---

## Adding a New Creation

1. Add a folder under `creations/demos/your_name/` (or `editors/`, etc.)
2. `CMakeLists.txt` with at least:
   - `set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})` so the exe lands in its own runtime dir.
   - `add_executable(IRYourName main.cpp)` + `target_link_libraries(IRYourName PUBLIC IrredenEngine)`.
   - The Windows DLL POST_BUILD step (`copy -t $<TARGET_FILE_DIR:...> $<TARGET_RUNTIME_DLLS:...>`).
   - Custom commands to copy `engine/render/data`, `engine/data`, and `engine/render/src/shaders` into the runtime dir as `data/` and `shaders/`, plus any creation-specific scripts.
   - An `IRYourNameRun` custom target with `WORKING_DIRECTORY ${runtime_dir}` and `USES_TERMINAL` that runs `$<TARGET_FILE:IRYourName>` so VSCode launches it from the right cwd.
3. Add `add_subdirectory(your_name)` to the parent `creations/demos/CMakeLists.txt`.
4. C++-only: `main.cpp`; Lua-driven: `main_lua.cpp` + `lua_bindings.*` + `lua_component_pack.hpp`.
5. See `creations/demos/shape_debug/CMakeLists.txt` for the canonical layout, `creations/demos/midi_polyrhythm/` for the Lua reference, and `creations/template/` for a minimal starter.
