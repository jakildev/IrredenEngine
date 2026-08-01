# engine/command/ — input-to-action binding

Binds `CommandNames` enum values to callables and wires them up to input
triggers (keyboard/mouse/gamepad/MIDI). Commands are fire-and-forget:
they return void, can't be undone, and don't queue.

`IRCommand::` exposes `createCommand<COMMAND_NAME>(InputType, ButtonStatus,
button, fn, mods)` plus the introspectable command registry that the F1 help
overlay renders (see "The command catalog" below).

## The `Command<NAME>` pattern

Every prefab command specializes:

```cpp
template <>
struct IRCommand::Command<IRCommand::CommandNames::ZOOM_IN> {
    static auto create() {
        return []() { IRRender::setZoom(getZoom() * 2); };
    }
};
```

- `CommandNames` is an enum in
  `engine/command/include/irreden/command/ir_command_types.hpp`. **Every
  prefab command must have an entry there first** — same discipline as
  `SystemName`. Missing entries cause linker errors, not runtime errors.
- `create()` returns a callable (often a lambda). No `SystemId`-like
  handle exists — the enum value *is* the identifier.

A command body can run a one-shot ECS query rather than a plain side effect —
the "act on every matching entity, once" shape. `Command<RANDOMIZE_VOXELS>`
(`engine/prefabs/irreden/voxel/commands/`) uses
`IRSystem::executeQuery<C_VoxelSetNew, Exclude<C_Locked>>(...)` to recolor
every unlocked voxel set with no persistent system behind it — see
`engine/system/CLAUDE.md` "One-shot queries (`executeQuery`)". A query-command
header includes `ir_system.hpp`, so `engine/command` PRIVATE-links
`IrredenEngineSystem`.

A creation binds it to a trigger:

```cpp
IRCommand::createCommand<IRCommand::CommandNames::ZOOM_IN>(
    InputTypes::KEY_MOUSE,
    ButtonStatuses::PRESSED,
    KeyMouseButtons::kKeyButtonZ,
    Command<ZOOM_IN>::create());
```

## `CommandManager`

Owns three registries: button commands (keyboard/mouse/gamepad), MIDI note
commands keyed by `(device, note)`, and MIDI CC commands keyed by
`(device, cc)`. The registration map is only populated for **named
`PRESSED`-status** bindings and is what the help overlay renders.

`CommandManager` does **not** poll. The input systems look up matching
commands each tick and invoke them directly.

### The introspectable registry (#2550)

`getCommandRegistrations()` returns `CommandRegistration{name, description,
button, triggerStatus, requiredModifiers}` rows — the read-only
`(binding, name, description)` iterable that `System<HELP_OVERLAY>` renders
and #2551's settings menu consumes.

`getRegistrationGeneration()` is a counter bumped **only when the vector
actually grows**. A consumer that caches text built from the registry
compares it against its own snapshot to decide whether to rebuild. This is
load-bearing: the pre-#2550 overlay built its text once (`if
(commandList_.empty())`) and never invalidated, so any command registered
after the first visible frame never appeared. Bumping on a *filtered*
registration would be equally wrong — it would turn "zero cost while hidden"
into a per-frame rebuild.

Two filters keep the list readable, both intentional:

- **Unnamed bindings are excluded.** `name` defaults to empty, so every
  ad-hoc `createCommand(..., fn)` lambda stays out unless the call site
  opts in by passing `name` / `description`.
- **Only `PRESSED` appears.** The `HELD` / `RELEASED` /
  `PRESSED_AND_RELEASED` halves (the WASD `MOVE_CAMERA_*_END` bindings)
  stay hidden so a key isn't listed twice.

## The command catalog (`kCommandInfo`)

`ir_command.hpp` carries one `CommandInfo{name_, displayName_, description_}`
row per `CommandNames` value, **indexed by the enum value itself**.
`commandNameToString()` and `commandDescription()` are O(1) lookups over it.

This replaced a hand-listed switch whose `default: return "UNKNOWN"` arm
silently rendered omitted values as "UNKNOWN" with no build or runtime signal
(`SCREENSHOT_CANVAS` and `TOGGLE_CULLING_FREEZE` had both shipped that way).
Two `static_assert`s close the gap: one ties `std::size(kCommandInfo)` to
`kCommandNameCount`, the other (`commandInfoRowsAligned()`) proves row `i`
describes enum value `i`. Adding an enum value without its row is now a
**compile error**.

Because the enum-templated `createCommand<NAME>(...)` forwards both strings
from this table, every prefab command appears in the overlay fully described
with no per-creation wiring — the camera bundle is described for every
`registerStandardKeyboardCommands()` demo for free.

**Description conventions.** The trixel font is uppercase-only, so
descriptions render uppercase — spell them that way so the source matches the
render. Keep each to one short clause (~40 chars) so a line fits the overlay
column. They are engine-public text: generic engine wording only, no
creation- or game-specific references.

## Lua-defined commands (T-193)

`LuaScript::bindLuaCommands()` exposes `IRCommand.{bindPrefab,
createCommand, fire, fireByName, CommandName}` and the input enum tables
(`IRInput.{InputType, ButtonStatus, Key, Modifier, GamepadButton,
GamepadAxis}`) so a creation can declare commands and input bindings
entirely from Lua. The design contract lives in
[`docs/design/lua-input-commands.md`](../../docs/design/lua-input-commands.md);
`creations/demos/default/commands.lua` is the canonical migration
example.

The C++ entry points added for the Lua surface are also usable directly:

- `IRCommand::fire(CommandId)` invokes a registered command by id,
  bounds-checked. Out-of-range ids log + return — no exception. Same id
  is returned by both `createCommand<NAME>(...)` and Lua's
  `IRCommand.bindPrefab`/`createCommand`.
- `IRCommand::fireByName(CommandNames)` dispatches to the matching
  `Command<NAME>::create()` body without registering an input trigger.
  Enum values without a `Command<NAME>` specialization log an error
  and return.
- `IRCommand::bindPrefabCommand(name, ...)` is the runtime-`name`
  counterpart to the existing `createCommand<NAME>(...)` template;
  the Lua binding's `IRCommand.bindPrefab` forwards here.

`Command<NAME>::create()` specializations remain the source of truth
for prefab command bodies. PR 2 does not delete any existing command.

## Gotchas

- **`CommandNames` enum is required.** Same linker-error footgun as
  `SystemName`. Add the enum value before implementing the command.
- **Adding a prefab command touches five hand-listed sites.** In order:
  1. the `CommandNames` enum + `kCommandNameCount` in
     `command/ir_command_types.hpp`;
  2. the `kCommandInfo` row in `ir_command.hpp` (display name +
     description);
  3. the `Command<NAME>` specialization header under
     `engine/prefabs/<domain>/commands/`;
  4. `bindPrefabCommand` **and** `fireByName` cases in
     `src/ir_command.cpp` (plus its `#include` of the new header);
  5. the `IR_BIND_CMD(name)` line in `engine/script/include/irreden/
     script/lua_command_bindings.hpp`.

  The omission classes used to be asymmetric and mostly silent. Sites 1–2
  are now a **compile error** (the `kCommandInfo` static_asserts); a missing
  site 4 logs an error at firing time; a missing site 5 resolves to nil in
  Lua at binding time.
- **Lua command body errors are caught in-VM.** The
  `IRCommand.createCommand` wrapper traps `sol::protected_function`
  errors and logs via `IRE_LOG_ERROR`. The error does not propagate up
  the dispatch loop; the next command's trigger check still runs. This
  depends on `SOL_EXCEPTIONS_ALWAYS_UNSAFE=1` in
  `engine/script/CMakeLists.txt` — see `engine/script/CLAUDE.md`.
- **Lua command lifetime is bounded by `LuaScript`.** Destroying the
  `sol::state` invalidates every captured `sol::protected_function`
  inside `CommandManager::m_userCommands`. `World` declares
  `m_lua` before `m_commandManager` so `CommandManager` destructs
  FIRST — wrapper lambdas release their `sol::protected_function`
  refs while `sol::state` is still alive. Reverse that order and
  shutdown UAFs on the registry index. Test fixtures that mix
  `LuaScript` + `CommandManager` outside of `World` must mirror this
  declaration order.
- **No undo / history / queue.** A command is a `std::function<void()>`.
  If you need undo, build it on top (the help overlay's command list is
  informational only).
- **Modifier keys only work for KEY_MOUSE.** Gamepad and MIDI commands
  ignore the `modifiers` field even if you pass one.
- **Callbacks capture by value at bind time.** If the captured state
  changes later (e.g. a pointer is re-seated), the command still holds
  the old value.
- **Only NAMED `PRESSED` commands appear in the help overlay.** Bindings
  for `HELD` / `RELEASED` / `PRESSED_AND_RELEASED`, and any binding
  registered without a `name`, are invisible to the registry. An ad-hoc
  lambda binding that should be discoverable passes the trailing
  `name` / `description` args (`random_voxels/main.cpp` is the reference).
- **`buildCommandListText()` is legacy.** It predates the overlay and
  formats the same registry without descriptions. Kept working as #2551's
  declared fallback and per the engine API removal rule; new code reads
  `getCommandRegistrations()` directly.
