# engine/command/ — input-to-action binding

Binds `CommandNames` enum values to callables and wires them up to input
triggers (keyboard/mouse/gamepad/MIDI). Commands are fire-and-forget:
they return void, can't be undone, and don't queue.

`IRCommand::` exposes `createCommand<COMMAND_NAME>(InputType, ButtonStatus,
button, fn, mods)` and `buildCommandListText()` for the debug overlay.

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
`(device, cc)`. The registration map is only populated for `PRESSED`-status
bindings and is used by `buildCommandListText()` for the debug help overlay.

`CommandManager` does **not** poll. The input systems look up matching
commands each tick and invoke them directly.

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
- **`fireByName` is hand-listed.** Adding a new prefab command requires
  appending cases to BOTH `commandNameToString` in `ir_command.hpp`
  AND `fireByName` / `bindPrefabCommand` in `src/ir_command.cpp`, plus
  the `IR_BIND_CMD(name)` line in `engine/script/include/irreden/
  script/lua_command_bindings.hpp`. The omission classes are
  asymmetric: a missing `commandNameToString` entry shows "UNKNOWN"
  silently; a missing `fireByName` entry logs an error at firing
  time; a missing `IR_BIND_CMD` entry resolves to nil in Lua at
  binding time.
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
  If you need undo, build it on top (the command list in the debug
  overlay is informational only).
- **Modifier keys only work for KEY_MOUSE.** Gamepad and MIDI commands
  ignore the `modifiers` field even if you pass one.
- **Callbacks capture by value at bind time.** If the captured state
  changes later (e.g. a pointer is re-seated), the command still holds
  the old value.
- **Only `PRESSED` commands appear in the help overlay.** Bindings for
  `HELD` / `RELEASED` / `PRESSED_AND_RELEASED` are invisible to
  `buildCommandListText()`.
