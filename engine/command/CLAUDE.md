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
