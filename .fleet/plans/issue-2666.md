## Plan: Command suites — declarative default-binding manifest (enumerate, opt out, rebind)

- **Issue:** #2666
- **Model:** opus
- **Date:** 2026-07-30

### Scope

Replace the two imperative engine command suites with declarative
default-binding manifests, add registration-time omit/remap overrides, and
expose enumeration + override-aware registration to Lua. Zero-arg suite
registration stays byte-identical; `voxel_editor` migrates onto the omit form
as the in-tree proof.

### Verified current state

- `command_suite_camera.hpp` registers 11 bindings (3 PRESSED singles + 4
  PRESSED/RELEASED WASD pairs); `command_suite_capture.hpp` registers 3.
- `bindPrefabCommand`'s hand-listed switch (`engine/command/src/ir_command.cpp`)
  covers every one of the 14 suite commands — the runtime loop needs no new
  dispatch machinery.
- `CommandManager::createCommand` stamps `m_commandRegistrations` only for
  **named PRESSED** bindings (`command_manager.hpp:55`) — so the registration
  map cannot serve as the manifest (it omits the RELEASED `*_END` rows and is
  populated only after registration). The manifest must be its own constexpr
  source.
- `creations/editors/voxel_editor/main.cpp:2532` hand-duplicates the camera
  suite minus CLOSE_WINDOW — the migration target.
- `IRPrefab::Camera::registerStandardKeyboardCommands()`
  (`engine/prefabs/irreden/render/camera_controls.hpp`) wraps
  `registerCameraCommands()` — needs an override pass-through.

### Approach

1. **Types + generic primitive**
   (`engine/command/include/irreden/command/ir_command_types.hpp` +
   `ir_command.hpp`): add `struct DefaultBinding { CommandNames command;
   IRInput::InputTypes inputType; IRInput::ButtonStatuses status; int button;
   IRInput::KeyModifierMask requiredModifiers; }`, `enum class Suite { CAMERA,
   CAPTURE }`, and `struct BindingOverrides { std::vector<CommandNames> omit;
   std::vector<std::pair<int, int>> remap; }` (or span-friendly equivalents).
   Then the one registration primitive in the command module:
   `IRCommand::registerBindings(std::span<const DefaultBinding>,
   const BindingOverrides & = {})` — skip rows whose command is in `omit`,
   substitute `button` per `remap` (a remap hits every row with that button,
   so PRESSED + RELEASED pairs move together), dispatch each surviving row
   through `bindPrefabCommand`. **Deliberately registration-time, not a
   mutable unbind/rebind registry**: `CommandId` is a vector index (removal
   invalidates ids / needs tombstones on the dispatch loop), `CommandNames`
   is not unique in the live registry (rebind-by-command is ambiguous), and
   never-bound keeps #2570's query + the overlay consistent for free. It
   needs only `DefaultBinding` + `bindPrefabCommand`, so it lives in
   `engine/command` with no prefab dependency, and any creation-authored
   binding table can use it.
2. **Manifests** (`engine/prefabs/irreden/common/command_suite_camera.hpp` /
   `command_suite_capture.hpp`): `constexpr DefaultBinding kCameraSuite[]` /
   `kCaptureSuite[]` (constexpr namespace-scope tables are program constants —
   allowed by `.claude/rules/cpp-globals.md`). `registerCameraCommands()` /
   `registerCaptureCommands()` become thin wrappers:
   `registerBindings(kCameraSuite, overrides)`, with the zero-arg overloads
   unchanged. Assert (debug) / log when a manifest row returns
   `kInvalidCommandId` — a future suite command missing from the
   `bindPrefabCommand` switch must fail loudly at registration, not silently
   thin the suite.
3. **Enumeration**: `IRCommand::suiteDefaults(Suite)` returning a span/const
   ref over the matching table, in a small
   `engine/prefabs/irreden/common/command_suite_registry.hpp` (it must see both
   tables; the prefab layer owns suite composition). Add the override
   pass-through to `IRPrefab::Camera::registerStandardKeyboardCommands`.
4. **Lua** (`engine/script/.../lua_command_bindings.hpp` + input enum
   bindings): `IRCommand.Suite` integer table (cpp-lua-enums rule — never
   string-matched suite names), `IRCommand.suiteDefaults(suite)` → array of
   `{command, inputType, status, button, modifiers}` tables,
   `IRCommand.registerSuite(suite, {omit = {...}, remap = {{from, to}, ...}})`
   (sugar over the same `registerBindings` primitive; suite identity is the
   integer enum, never a string). Extend the short `IRInput.Key` table with
   the keypad entries (KPAdd, KPSubtract, KP0–KP9, KPEnter, …) so remap
   targets are nameable from Lua.
5. **Migrate `voxel_editor`**: replace the hand-rolled subset at
   `main.cpp:2532` with the omit form (`omit = {CLOSE_WINDOW}`); delete the
   duplicated registrations and update the comment.
6. **Docs**: `engine/command/CLAUDE.md` (manifest + overrides section, Lua
   surface), `engine/prefabs/irreden/common/CLAUDE.md` (the Commands bullet).

Suggested slicing if two PRs: (1)–(3)+(5)+(6) C++ manifest/overrides/migration,
then (4) Lua exposure stacked on it.

### Affected files

- `engine/command/include/irreden/command/ir_command_types.hpp` — DefaultBinding, Suite, BindingOverrides
- `engine/command/include/irreden/ir_command.hpp` + `src/ir_command.cpp` — generic `registerBindings(span, overrides)` primitive
- `engine/prefabs/irreden/common/command_suite_camera.hpp` — constexpr manifest + loop + overrides overload
- `engine/prefabs/irreden/common/command_suite_capture.hpp` — same
- `engine/prefabs/irreden/common/command_suite_registry.hpp` (new) — `suiteDefaults(Suite)`
- `engine/prefabs/irreden/render/camera_controls.hpp` — override pass-through
- `engine/script/include/irreden/script/lua_command_bindings.hpp` — Suite table, suiteDefaults, registerSuite
- `engine/script/` input enum binding site (`bindInputEnums`) — keypad `IRInput.Key` entries
- `creations/editors/voxel_editor/main.cpp` — migrate onto omit form
- `engine/command/CLAUDE.md`, `engine/prefabs/irreden/common/CLAUDE.md` — docs

### Acceptance criteria

- Zero-arg suite registration byte-identical (same registrations, same debug
  help overlay output; build + run a demo that uses both suites).
- `suiteDefaults(CAMERA)` = 11 rows incl. RELEASED `*_END`; `CAPTURE` = 3.
- Omit test: camera suite `omit = {CLOSE_WINDOW}` → Escape unbound, rest bound.
- Remap test: WASD→arrows moves both statuses per key; no WASD rows remain.
- Lua: `suiteDefaults` rows match C++; `registerSuite` honors omit/remap;
  keypad keys resolvable in `IRInput.Key`.
- Generality: a caller-authored `DefaultBinding` table registers through
  `registerBindings` with the same omit/remap semantics (unit-style check or
  demo usage) — the mechanism is not suite-specific.
- `voxel_editor` builds and behaves unchanged on the omit form.

### Gotchas

- **The registration map is not the manifest.** `m_commandRegistrations` only
  captures named PRESSED binds — deriving enumeration from it silently drops
  the RELEASED camera rows. Keep the constexpr tables authoritative.
- **`bindPrefabCommand` is a hand-listed switch.** A suite row whose command
  case is missing logs and returns `kInvalidCommandId` — surface that as a
  loud registration failure (assert/log), and note in the CLAUDE.md that
  adding a suite command requires the switch case first.
- **Overlay parity.** Verify the loop path stamps the same command names into
  `m_commandRegistrations` as the old `createCommand<NAME>` calls so
  `buildCommandListText()` output is unchanged.
- **No dedup policy.** Per #2570's contract, `createCommand` keeps appending
  unconditionally; omit/remap shape what the suite *requests*, they impose no
  collision policy on other registrations.
- **cpp-lua-enums.** Suite identity crosses the Lua boundary as an integer
  table, never a string compared in C++.
- **Cross-platform.** Pure C++/Lua; no backend or host coupling.

