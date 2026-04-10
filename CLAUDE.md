# Irreden Engine

Isometric voxel game engine built on an archetype-based ECS. C++ handles the engine, systems, and pipelines. Lua 5.4 (via sol2) drives entity creation and game logic in creations.

See `AGENTS.md` for the full architecture reference.

---

## Build

The project is configured under `build/` using the `windows-debug` CMake preset (MinGW Makefiles, MSYS2 GCC at `C:\msys64\mingw64\bin\c++.exe`). The configured build tree is already present — do not reconfigure unless the user asks.

**Canonical build command** (matches the one driven from the user's VSCode task):

```bash
"C:/Program Files/CMake/bin/cmake.EXE" --build C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine/build --target IRCreationDefault IRGame --
```

Swap the `--target` list for whichever executable(s) you need, e.g. `IRShapeDebug`, `IRCreationDefault`, `IRGame`. Each executable's `CMakeLists.txt` has a Windows POST_BUILD step that copies runtime DLLs next to the `.exe`, so launching from `build/creations/.../<target>.exe` just works.

**Known shell limitation:** `cc1plus` diagnostics are invisible in the Bash tool's captured output on this machine — a clean compile shows normally, but errors vanish. If a build looks like it failed with no explanation, ask the user to check their VSCode build panel rather than thrashing on shell captures.

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
2. `CMakeLists.txt` with `add_executable` + `target_link_libraries(... PUBLIC IrredenEngine)`
3. Add subdirectory to parent `CMakeLists.txt`
4. C++-only: `main.cpp`; Lua-driven: `main_lua.cpp` + `lua_bindings.*` + `lua_component_pack.hpp`
5. See `creations/demos/midi_polyrhythm/` as the Lua reference; `creations/template/` for a minimal starter
