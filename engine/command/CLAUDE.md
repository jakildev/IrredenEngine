# engine/command/ — input-to-action binding

Binds `CommandNames` enum values to callables and wires them up to input
triggers (keyboard/mouse/gamepad/MIDI). Commands are fire-and-forget:
they return void, can't be undone, and don't queue.

## Entry point

`engine/command/include/irreden/ir_command.hpp` — exposes `IRCommand::`:

- `getCommandManager()`.
- `createCommand<COMMAND_NAME>(InputType, ButtonStatus, button, fn, mods)`.
- `buildCommandListText()` — formats the registered-command table for
  debug overlay output.

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

A creation binds it to a trigger:

```cpp
IRCommand::createCommand<IRCommand::CommandNames::ZOOM_IN>(
    InputTypes::KEY_MOUSE,
    ButtonStatuses::PRESSED,
    KeyMouseButtons::kKeyButtonZ,
    Command<ZOOM_IN>::create());
```

## `CommandManager`

Owns three registries:

- **Button commands** — keyboard/mouse/gamepad bindings. Stored as
  `CommandStruct<COMMAND_BUTTON>` wrapping a `std::function<void()>`.
- **MIDI note commands** — keyed by `(device, note)`.
- **MIDI CC commands** — keyed by `(device, cc)`.
- `m_commandRegistrations` — only filled for `PRESSED`-status bindings,
  used by `buildCommandListText()` for the debug help overlay.

`CommandManager` does **not** poll. The input systems
(`system_input_key_mouse`, `system_input_gamepad`, MIDI systems) look up
matching commands each tick and invoke them directly.

## Internal layout

```
engine/command/
├── include/irreden/
│   ├── ir_command.hpp            — public facade
│   └── command/
│       ├── ir_command_types.hpp  — CommandNames enum, InputTypes
│       ├── command_manager.hpp
│       └── command_struct.hpp    — CommandStruct<T>
└── src/
    ├── ir_command.cpp
    └── command_manager.cpp
```

Per-command specializations live in
`engine/prefabs/irreden/<domain>/commands/command_<name>.hpp`.

## Gotchas

- **`CommandNames` enum is required.** Same linker-error footgun as
  `SystemName`. Add the enum value before implementing the command.
- **No Lua execution path.** Commands fire from input systems only. Lua
  can *bind* a command (via a registered callback from the creation's
  bindings) but can't invoke `Command<X>` by name.
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
