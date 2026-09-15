# engine/command/ — input-to-action binding

Binds `CommandNames` enum values to callables and wires them to input
triggers (keyboard/mouse/gamepad/MIDI). Commands are fire-and-forget: they
return void, can't be undone, and don't queue. Design contract for the Lua
surface: [`docs/design/lua-input-commands.md`](../../docs/design/lua-input-commands.md).

Validators: `test/command/default_binding_manifest_test.cpp`,
`test/command/keyboard_dispatch_test.cpp`, `test/common/command_registry_test.cpp`,
`test/script/lua_command_test.cpp` (all in `IrredenEngineTest`).

## The `Command<NAME>` pattern

Every prefab command specializes `IRCommand::Command<NAME>` with a static
`create()` returning a `void()` callable; a creation binds it with
`IRCommand::createCommand<NAME>(InputType, ButtonStatus, button, fn, mods)`.

- `CommandNames` (`command/ir_command_types.hpp`) is authoritative — the enum
  value *is* the identifier, and every prefab command needs its entry first,
  same discipline as `SystemName`. A missing entry is a linker error.
- A command body may run a one-shot ECS query (`IRSystem::executeQuery`,
  `engine/system/CLAUDE.md` §"Runtime-typed systems and one-shot queries") —
  the "act on every matching entity, once" shape with no persistent system.
  A query command's header includes `ir_system.hpp`, which is why
  `engine/command` PRIVATE-links `IrredenEngineSystem`.

## Adding a prefab command

Five hand-listed sites, in order:

1. the `CommandNames` enum + `kCommandNameCount` in `command/ir_command_types.hpp`;
2. the `kCommandInfo` row in `ir_command.hpp` (display name + description);
3. the `Command<NAME>` specialization under `engine/prefabs/irreden/<domain>/commands/`;
4. the `bindPrefabCommand` **and** `fireByName` cases in `src/ir_command.cpp`
   (plus the `#include` of the new header);
5. the `IR_BIND_CMD(name)` line in `engine/script/include/irreden/script/lua_command_bindings.hpp`.

Sites 1–2 are a compile error (`kCommandInfo` static_asserts); a missing
site 4 logs at firing / registration time and asserts in debug when reached
through a manifest; a missing site 5 resolves to nil in Lua.

## Default-binding manifests (#2666)

The engine's default keys are data: one `constexpr DefaultBinding` table per
suite (`kCameraSuite`, `kCaptureSuite`) in
[`engine/prefabs/irreden/common/command_suite_registry.hpp`](../prefabs/irreden/common/command_suite_registry.hpp);
`registerCameraCommands()` / `registerCaptureCommands()` are loops over them.
The one primitive is `IRCommand::registerBindings(span<const DefaultBinding>,
const BindingOverrides &)` — it filters by `omit_`, substitutes buttons per
`remap_`, and dispatches each surviving row through `bindPrefabCommand`. A
creation's own `DefaultBinding` table gets the same machinery. In-tree
creations pass overrides through `IRPrefab::Camera::registerStandardKeyboardCommands`.

- **`omit_` matches on command, `remap_` on button.** A pan axis is two
  distinct commands on one button (`MOVE_CAMERA_UP_START` + `_END` on `W`):
  one remap moves both rows; omitting `_START` alone leaves the RELEASED row
  bound. Omit both to drop the axis.
- **Remaps never chain.** First matching pair wins per row, so `{W→A, A→UP}`
  leaves `W` on `A`.
- **Registration-time only; no `unbind` / `rebind`.** `CommandId` is an index
  into an append-only vector (removal would invalidate outstanding ids or put
  tombstones on the per-tick dispatch loop), and `CommandNames` is not a
  unique key in the live registry, so rebind-by-command is ambiguous by
  construction. Never-bound also keeps the help overlay and
  `getCommandRegistrations()` consistent. A runtime rebind surface (settings
  menu, persisted keymaps) is a separate design reachable via in-place
  `CommandId` mutation.
- **The registration map is not the manifest.** Enumerate defaults via
  `IRCommand::suiteDefaults(Suite)` (reads the constexpr tables); ask what is
  bound *now* via `isButtonBound` (scans the live command list).
- **A manifest row needs its `bindPrefabCommand` case first.** A row whose
  command has no case makes `registerBindings` log an error and assert in
  debug — deliberately loud, so a missing case can't silently thin a suite.

Lua parity is `IRCommand.{Suite, suiteDefaults, registerSuite}` —
`engine/script/CLAUDE.md` §"Commands and input".

## `CommandManager`

Owns three registries: button commands, MIDI note commands keyed by
`(device, note)`, and MIDI CC commands keyed by `(device, cc)`. It does not
poll — the input systems look up matching commands each tick and invoke them.

- **The introspectable registry.** `getCommandRegistrations()` returns
  `CommandRegistration{name, description, button, triggerStatus,
  requiredModifiers}` rows — populated only for **named `PRESSED`** bindings;
  it is what `System<HELP_OVERLAY>` and the settings menu render.
  `getRegistrationGeneration()` bumps only when the vector grows; a consumer
  caching text built from the registry compares it against its own snapshot
  to decide whether to rebuild (keeps "zero cost while hidden" true).

### Querying what is bound (`isButtonBound`)

`isButtonBound(inputType, triggerStatus, button)` answers "is this key
already taken" for creation code guarding an ad-hoc bind, instead of a
hand-maintained "reserved keys" table that drifts. Exposed on
`CommandManager`, as a free function in `ir_command.hpp`, and to Lua as
`IRCommand.isButtonBound`. It scans `m_userCommands`, not the registration
map, so it sees unnamed lambdas and RELEASED rows. Contract (full text on the
declaration in `command/command_manager.hpp`):

- Modifier-blind: a row with `requiredModifiers` / `blockedModifiers` still
  counts as bound; a modifier-aware refinement is a new overload.
- Data, not policy: `createCommand` still appends unconditionally; collision
  policy is the caller's.
- MIDI note/CC bindings live in their own per-device maps and report false.
- O(bindings) — a registration-time query, not a per-tick call.
- **The query is type-exact; the dispatcher is not.** `isButtonBound` matches
  on `getType()`, but `executeUserKeyboardCommandsAll` never consults it, so
  a row bound with a non-`KEY_MOUSE` input type would fire on the keyboard
  press while the query calls it unbound. Latent (every button binding in
  the tree is `KEY_MOUSE`; there is no gamepad dispatch loop). The type check
  belongs to the query; the missing filter belongs to the dispatcher.
  `CommandRegistryTest.IsButtonBoundIsTypeExact` locks the query half.

## The command catalog (`kCommandInfo`)

`ir_command.hpp` carries one `CommandInfo{name_, displayName_, description_}`
row per `CommandNames` value, indexed by the enum value; two `static_assert`s
make an enum value without its row a compile error. `createCommand<NAME>`
forwards both strings, so every prefab command appears in the overlay fully
described with no per-creation wiring.

Description conventions: the trixel font is uppercase-only, so spell
descriptions in uppercase; one short clause (~40 chars) so a line fits the
overlay column; engine-public wording only — no creation- or game-specific
references.

## Lua-defined commands

`LuaScript::bindLuaCommands()` exposes `IRCommand.{bindPrefab, createCommand,
fire, fireByName, CommandName}` and the `IRInput.*` enum tables so a creation
declares commands and bindings from Lua; `creations/demos/default/commands.lua`
is the canonical example. The C++ entry points behind it —
`IRCommand::fire(CommandId)`, `fireByName(CommandNames)` and the
runtime-`name` `bindPrefabCommand(name, ...)` — are usable directly and
carry their contracts as doc comments in `ir_command.hpp`. `CommandId` is
the same value from `createCommand<NAME>` and from Lua's `bindPrefab` /
`createCommand`; `Command<NAME>::create()` specializations remain the source
of truth for prefab command bodies.

## Gotchas

- **Lua command body errors are caught in-VM.** The `IRCommand.createCommand`
  wrapper traps `sol::protected_function` errors and logs via `IRE_LOG_ERROR`;
  the next command's trigger check still runs. Depends on
  `SOL_EXCEPTIONS_ALWAYS_UNSAFE` — `engine/script/CLAUDE.md` §"Lua runtime: LuaJIT 2.1".
- **Lua command lifetime is bounded by `LuaScript`.** Every captured
  `sol::protected_function` in `CommandManager::m_userCommands` dies with the
  `sol::state`. `World` declares `m_lua` before `m_commandManager` so
  `CommandManager` destructs first; reversing that order UAFs at shutdown.
  Test fixtures that mix `LuaScript` + `CommandManager` outside `World`
  mirror this declaration order.
- **No undo / history / queue.** A command is a `std::function<void()>`; build
  undo on top if you need it.
- **Modifier keys only work for `KEY_MOUSE`.** Gamepad and MIDI commands ignore
  the `modifiers` field.
- **A bare start/end pair is admitted once, at the press.** Dispatch resolves
  same-key ambiguity by specificity (a bare-mask binding is skipped on any
  frame a modifier-bearing binding on the same button matches), and that rule
  is frame-local — so `executeUserKeyboardCommands` groups bare-mask rows by
  button plus `blockedModifiers`, and a group holding both a PRESSED and a
  RELEASED row is a *pair* whose eligibility is decided on the press frame and
  held until the release. Consequences: a chord shadows both halves or
  neither; a modifier pressed mid-hold cannot cancel a release with a live
  start behind it; two rows on one key with **different** blocked masks are
  two groups, not a pair; a paired RELEASED row with no admitted press does
  nothing; unpaired bare rows and every modifier-bearing row keep
  frame-by-frame matching. Algorithm text on the declaration in
  `command/command_manager.hpp`; `test/command/keyboard_dispatch_test.cpp`
  drives it through `executeUserKeyboardCommands(KeyMouseInputProbe)` from a
  deterministic snapshot — no GLFW, no display.
- **Callbacks capture by value at bind time.** A pointer re-seated later is
  not seen by the command.
- **Only named `PRESSED` commands appear in the help overlay.** `HELD` /
  `RELEASED` / `PRESSED_AND_RELEASED` bindings and any binding registered
  without a `name` are invisible to the registry. An ad-hoc lambda that should
  be discoverable passes the trailing `name` / `description` args
  (`creations/demos/random_voxels/main.cpp` is the reference).
- **`buildCommandListText()` is legacy.** It formats the same registry without
  descriptions and stays per the engine API removal rule
  (`docs/agents/CLAUDE-BASELINE.md`); new code reads
  `getCommandRegistrations()` directly.
