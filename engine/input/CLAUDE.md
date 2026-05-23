# engine/input/ — keyboard, mouse, gamepad state

Polls GLFW event queues each frame and exposes per-button state machines
and mouse position. Input state is split by pipeline event (INPUT, UPDATE,
RENDER) so each stage sees the state relevant to its tick.

## `IRInput::` public API

### Keyboard / mouse

- `checkKeyMouseButton(button, status)` — is this button in this state?
- `checkKeyMouseModifiers(requiredMods, blockedMods)` — Shift/Ctrl/Alt mask check.
- `getMousePosition()` — cursor position in iso/world space for the current
  pipeline event's snapshot.
- `getMousePositionScreen()` — cursor position in screen (pixel) space.
- Per-button press/release frame counters.

### Gamepad

- `checkGamepadButton(button, status, irGamepadId = 0)` — is this gamepad button in
  this state? Uses `GamepadButtons` enum (A/B/X/Y, bumpers, triggers-as-buttons,
  D-pad, thumbsticks, Back/Start/Guide). Asserts if no gamepad was connected at startup.
- `getGamepadAxis(axis, irGamepadId = 0)` — analogue axis value. Uses `GamepadAxes`
  enum: `kGamepadAxisLeftX/Y`, `kGamepadAxisRightX/Y` report [-1, 1]; triggers
  (`kGamepadAxisLeftTrigger`, `kGamepadAxisRightTrigger`) report -1.0 unpressed,
  +1.0 fully pressed. Asserts if no gamepad was connected at startup.

Both functions query the snapshot from the INPUT pipeline tick via the ECS
`C_GLFWGamepadState` component. The `INPUT_GAMEPAD` system must be registered
in the creation's pipeline (all current demos do this).

**Controller compatibility.** GLFW uses the SDL gamepad mapping database to
normalise button/axis layout. Any controller with a `gamecontrollerdb.txt`
entry (covers Xbox, PlayStation, most generic HID gamepads) reports through
the above enums without per-type special-casing. Unmapped HID devices are
treated as joysticks with no `C_GLFWGamepadState`.

## `InputManager`

Creates one entity per physical button and one per gamepad. Input state is
snapshotted per pipeline event (INPUT, UPDATE, RENDER) so each stage gets
the cursor position and button states that were current at its tick.

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

Input-driven but render-aware. `C_HitboxRect` (axis-aligned rectangle,
`u8vec2 size_`) and `C_HitboxCircle` (circle, `int radius_`) are
screen-space hitbox components. The hover-detect system tests the cursor
against every hitbox each INPUT tick and fires registered callbacks:

- `onHovered` / `onUnhovered` — entity-wide.
- `onClicked` — matches a button status.

Callbacks are `sol::protected_function` (Lua) or plain `std::function`
(C++). They're stored by handler id so you can deregister them.

## Gotchas

- **Must `advanceInputState(event)` per frame.** If a pipeline forgets
  the advance, `PRESSED` lingers and fires the same handler every tick.
  Engine prefabs already do this; custom pipelines need to as well.
- **Gamepads are detected at startup only.** `InputManager` discovers
  connected joysticks once in its constructor via `initJoystickEntities()`.
  Hot-plugging a controller mid-session will not create a new entity; a
  restart is required. (`glfwSetJoystickCallback` is not registered.)
- **Only gamepad 0 is queried.** Gamepad axis reads are hardcoded to
  `irGamepadId = 0`. Supporting multiple pads requires edits to the
  gamepad system, not just additional entities.
- **Hover needs hitbox + transform.** No fallback — an entity with only
  a `C_LocalTransform` / `C_WorldTransform` will never register a hover.
- **No window-focus tracking.** Input fires regardless of whether the
  window has focus. If this matters, filter in the system using a GLFW
  focus query.
- **Lua callback lifetime.** Follows the same `sol::protected_function`
  ownership rules as all Lua callbacks — see `engine/script/CLAUDE.md`
  Gotchas ("LuaScript lifetime is absolute"). Register the callback for
  the lifetime of the entity.
- **`C_MouseScroll` is ephemeral, not persistent.** Each scroll event
  creates a short-lived entity carrying `C_MouseScroll` (with a
  `C_Lifetime` so it expires after one frame). Unlike persistent key
  state (one entity per button, alive for the session), scroll entities
  are born per-event and die via the LIFETIME system. Systems that
  iterate `C_MouseScroll` must therefore accumulate deltas in their
  `endTick` rather than reading a single "current scroll" component —
  multiple scroll events can arrive in one frame, each as its own
  entity. The scrollZoomSystem in `creations/editors/voxel_editor/` is
  the canonical example.
