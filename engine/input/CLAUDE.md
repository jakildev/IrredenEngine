# engine/input/ — keyboard, mouse, gamepad state

Polls GLFW event queues each frame and exposes per-button state machines
and mouse position. Input state is split by pipeline event (INPUT, UPDATE,
RENDER) so each stage sees the state relevant to its tick.

## Entry point

`engine/input/include/irreden/ir_input.hpp` — exposes `IRInput::` free
functions:

- `getInputManager()`.
- `checkKeyMouseButton(button, status)` — is this button in this state?
- `checkKeyMouseModifiers(requiredMods, blockedMods)` — Shift/Ctrl/Alt mask check.
- `getMousePosition()` — cursor position in iso/world space for the current
  pipeline event's snapshot.
- `getMousePositionScreen()` — cursor position in screen (pixel) space.
- Per-button press/release frame counters.

## `InputManager`

Owns:

- One entity per `KeyMouseButton` (keyboard keys + mouse buttons), with
  `C_KeyMouseButton` holding the current `ButtonStatuses`.
- A vector of gamepad entities (`C_GlfwGamepad`, `C_GlfwJoystick`).
- Scroll deltas, mouse position cache, press/release accumulators.
- One `EventInputState` per pipeline event (INPUT, UPDATE, RENDER). Each
  state holds its own `mousePosition_` snapshot; `getMousePosition()` returns
  the snapshot for whichever event is currently advancing.

### Button state machine

`ButtonStatuses`:

```
NOT_HELD → (press)   → PRESSED
PRESSED  → (hold)    → HELD
HELD     → (release) → RELEASED
RELEASED → (idle)    → NOT_HELD
PRESSED  → (release in same frame) → PRESSED_AND_RELEASED
```

`advanceInputState(IRTime::Events event)` transitions buttons forward
(`PRESSED → HELD`, `RELEASED → NOT_HELD`) at the start of each pipeline
stage. Forgetting to advance for an event means stale state persists.

## Hover / hitbox

Input-driven but render-aware. `C_Hitbox2D` is a screen-space axis-aligned
rectangle on an entity. `system_entity_hover_detect` (in
`prefabs/irreden/input/systems/`) tests the cursor against every hitbox
each INPUT tick and fires registered callbacks:

- `onHovered` / `onUnhovered` — entity-wide.
- `onClicked` — matches a button status.

Callbacks are `sol::protected_function` (Lua) or plain `std::function`
(C++). They're stored by handler id so you can deregister them.

## Key components (prefabs/irreden/input)

- `C_KeyMouseButton` — holds a `KeyMouseButtons` enum value + current
  status. One per physical button.
- `C_GlfwGamepad`, `C_GlfwJoystick` — per-controller state.
- `C_MousePosition` — cached cursor.
- `C_CursorPosition` — hover detection target.
- `C_Hitbox2D` — rectangle for hit testing.

## Internal layout

```
engine/input/
├── include/irreden/
│   ├── ir_input.hpp              — public facade
│   └── input/
│       ├── ir_input_types.hpp    — ButtonStatuses, KeyMouseButtons
│       └── input_manager.hpp     — InputManager class
└── src/
    ├── ir_input.cpp
    └── input_manager.cpp
```

Plus the prefab components, systems, and commands under
`engine/prefabs/irreden/input/`.

## Gotchas

- **Must `advanceInputState(event)` per frame.** If a pipeline forgets
  the advance, `PRESSED` lingers and fires the same handler every tick.
  Engine prefabs already do this; custom pipelines need to as well.
- **Only gamepad 0 is queried.** Gamepad axis reads are hardcoded to
  `irGamepadId = 0`. Supporting multiple pads requires edits to the
  gamepad system, not just additional entities.
- **Hover needs hitbox + position.** No fallback — an entity with only a
  `C_Position3D` will never register a hover.
- **No window-focus tracking.** Input fires regardless of whether the
  window has focus. If this matters, filter in the system using a GLFW
  focus query.
- **Lua callback lifetime.** `sol::protected_function` captured in a
  handler must outlive the handler or the callback will crash. Register
  the callback for the lifetime of the entity.
