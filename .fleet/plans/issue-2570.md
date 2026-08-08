## Plan: Expose command-binding query to Lua (IRCommand.isButtonBound / getRegisteredBindings)

- **Issue:** #2570
- **Model:** sonnet
- **Date:** 2026-08-04

### Scope

Give creation code (C++ and Lua) a read-only query over what is already
bound in the `CommandManager`, so ad-hoc key binds can be guarded against
the engine's own registrations without a hand-maintained shadow list.
Additive only: `createCommand` behavior, dedup policy, and every existing
surface are untouched.

### Verified current state

All issue premises re-verified against the tree at plan time:

- `CommandManager::createCommand` appends unconditionally to
  `m_userCommands` (`engine/command/include/irreden/command/command_manager.hpp:56-72`);
  only named+`PRESSED` binds also stamp `m_commandRegistrations`. So
  `m_userCommands` is the authoritative scan target — it holds unnamed and
  `RELEASED`-status rows the registration map never sees.
- `CommandStruct<COMMAND_BUTTON>` already stores and exposes everything the
  query needs: `getType()`, `getTriggerStatus()`, `getButton()`
  (`engine/command/include/irreden/command/command.hpp:35-53`). No new
  storage.
- **Negative claim source-verified:** `fleet-rules-sweep --pattern
  'isButtonBound|getRegisteredBindings'` → 0 matches, 3935 files swept
  (non-zero coverage, real clean pass). No query API exists on either side.
- All current `getCommandRegistrations()` consumers (help overlay, settings
  menu, `buildCommandListText()`, tests) are read-only; none conflict.
- The issue's flagged **adjacent gap is already closed**: `IRInput.Key`
  carries the full keypad block (`KP_0`–`KP_EQUAL`,
  `engine/script/include/irreden/script/lua_command_bindings.hpp:297-315`,
  landed with the #2666 suite-remap work). No carve-off needed.
- Sibling/in-flight reconciliation: #2550 (registry + generation counter)
  and #2666 (default-binding manifests / `suiteDefaults`) are merged;
  #2551's settings menu is in-tree. No open engine PR touches
  `engine/command/` or `lua_command_bindings.hpp`.

### Approach

1. **`command_manager.hpp`** — add
   `bool isButtonBound(IRInput::InputTypes inputType, IRInput::ButtonStatuses triggerStatus, int button) const`:
   linear scan of `m_userCommands` matching all three of
   `getType()` / `getTriggerStatus()` / `getButton()`. Modifier masks are
   deliberately ignored — key-level granularity per the issue; a row with
   `requiredModifiers`/`blockedModifiers` still counts as bound. Doc
   comment states the cost contract: O(bindings) scan, an init/registration-time
   guard query, not a per-tick call.
2. **`ir_command.hpp`** — free-function wrapper
   `inline bool isButtonBound(...)` forwarding through
   `getCommandManager()`, matching the existing `fire` / `fireByName` /
   `createCommand` module-API shape, so C++ creations query via the
   `ir_command.hpp` entry point.
3. **`lua_command_bindings.hpp`** — two entries in
   `bindCommandFunctions`, added inside the existing idempotent-guard
   block (the early return keys on `IRCommand.bindPrefab`; new entries ride
   the same guard — do NOT add a second guard):
   - `IRCommand.isButtonBound(inputType, status, button)` → boolean.
     Casts mirror the `bindPrefab` lambda; forwards to the manager query.
   - `IRCommand.getRegisteredBindings()` → array of
     `{name, description, button, status, modifiers}` tables built from
     `getCommandRegistrations()`, using the `sol::this_state` +
     `rows.add(row)` shape `suiteDefaults` already uses. (Superset of the
     issue's row sketch: `description` is included for full help-overlay
     parity — it is already on `CommandRegistration`.)
4. **Tests** — one per new public surface (per `test/CLAUDE.md`):
   - `test/common/command_registry_test.cpp` (existing bare-`CommandManager`
     fixture): an **unnamed** binding and a **RELEASED**-status binding are
     both visible to `isButtonBound` (the rows `getCommandRegistrations()`
     cannot see); the same key at an unregistered status and an unbound key
     both return false in the same tests.
   - `test/script/lua_command_test.cpp` (existing `LuaCommandTest`
     fixture): register the camera suite C++-side via
     `IRCommand::registerCameraCommands()` (registration-only, headless-safe
     — the manifest test already does this with the same manager set), then
     from Lua assert `IRCommand.isButtonBound(IRInput.InputType.KEY_MOUSE,
     IRInput.ButtonStatus.PRESSED, IRInput.Key.W)` is true **and** the
     RELEASED-W row (`MOVE_CAMERA_UP_END`,
     `engine/prefabs/irreden/common/command_suite_registry.hpp:53`) is
     true — proving the scan reads `m_userCommands`, not the registration
     map — while an unbound key in the same script returns false. A
     no-suite test on the fresh fixture asserts W is false (the "creation
     that never registers the camera suite" acceptance arm).
     `IRCommand.getRegisteredBindings()` coverage: row count and the
     name/button fields of a known registration.
5. **Docs** — `engine/command/CLAUDE.md` (§`CommandManager`: the query +
   its cost contract) and `engine/script/CLAUDE.md` (§"Commands and
   input": the two new API bullets).
6. Commit `.fleet/plans/issue-2570.md` as the first commit of the
   implementation branch, then implement; one PR, `Closes #2570`.

### Affected files

- `engine/command/include/irreden/command/command_manager.hpp` — add `isButtonBound` const method
- `engine/command/include/irreden/ir_command.hpp` — free-function wrapper
- `engine/script/include/irreden/script/lua_command_bindings.hpp` — `IRCommand.isButtonBound` + `IRCommand.getRegisteredBindings` inside `bindCommandFunctions`
- `test/common/command_registry_test.cpp` — C++ query-surface tests
- `test/script/lua_command_test.cpp` — Lua seam tests
- `engine/command/CLAUDE.md`, `engine/script/CLAUDE.md` — surface docs
- `.fleet/plans/issue-2570.md` — this plan (first commit)

### Acceptance criteria

All on the existing `IrredenEngineTest` target (no new fixture needed;
build + run: `fleet-build --target IrredenEngineTest`, then filtered runs):

- **Positive-fire (Lua):** `--gtest_filter='LuaCommandTest.*'` — after
  `registerCameraCommands()`, `IRCommand.isButtonBound(KEY_MOUSE, PRESSED,
  Key.W)` returns **true**, and `(KEY_MOUSE, RELEASED, Key.W)` returns
  **true** (RELEASED visibility — the acceptance bullet the registration
  map structurally cannot satisfy). False arms sit in the same tests
  against a suite-registered manager, plus the fresh-fixture no-suite test.
- **Positive-fire (C++):** `--gtest_filter='CommandRegistryTest.*'` — an
  unnamed RELEASED binding flips `isButtonBound` false→true across the
  `createCommand` call.
- `IRCommand.getRegisteredBindings()` returns the registered rows with
  name/description/button/status/modifiers populated.
- **Unchanged behavior:** existing `CommandRegistryTest.*`,
  `DefaultBindingManifestTest.*`, and `LuaCommandTest.*` suites stay green
  (no dedup policy imposed; registration path untouched). Note when running
  the full suite: `SaveTrait.InventoryIsComplete` is red on master (#2834)
  — pre-existing, not this task's.

### Gotchas

- This is **not** a new prefab command — the five-site checklist in
  `engine/command/CLAUDE.md` (enum / `kCommandInfo` / `Command<NAME>` /
  `ir_command.cpp` cases / `IR_BIND_CMD`) does not apply. No
  `CommandNames` entry, no catalog row.
- MIDI note/CC bindings live in separate registries
  (`m_midiNoteDeviceCommands` / `m_midiCCDeviceCommands`) and are invisible
  to this query; passing `MIDI_NOTE` / `MIDI_CC` as `inputType` scans only
  `m_userCommands` and returns false. Say so in the method's doc comment —
  the guard use case (keyboard/mouse/gamepad) is what the issue asks for.
- `command_manager.hpp` has `using namespace IRInput` at namespace scope —
  match the file's existing type spellings.
- `LuaCommandTest`'s member order is load-bearing (`LuaScript` declared
  first so it destructs last) — add tests to the existing fixture, don't
  build a new one.
- Additive-only public surface, specified verbatim in the human-approved
  issue body — planned low-stakes (no `human:review-plan` hold).
- Out of scope, tracked nowhere yet by design: a modifier-mask-aware
  overload (add only when a concrete caller needs it) and any dedup/policy
  change in `createCommand`.

