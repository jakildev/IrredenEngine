# engine/prefabs/irreden/input/ — key/mouse/gamepad + hover

Input state components, hitbox-based hover/click detection, and the
systems that populate button state. The underlying polling lives in
`engine/input/`; this directory is the ECS surface.

## Key components

- `component_hitbox_2d.hpp` — `C_HitBox2D`, half-extents + `hovered_`
  flag. Tested against mouse position per frame.
- `component_keyboard_key.hpp` — `C_KeyboardKey`, GLFW key code.
- `component_key_mouse_button.hpp` — `C_KeyMouseButton`, wraps the
  `KeyMouseButtons` enum value. One entity per physical button, created
  at engine init.
- `component_key_status.hpp` — `C_KeyStatus`, a button state machine
  (`NOT_HELD`/`PRESSED`/`HELD`/`RELEASED`/`PRESSED_AND_RELEASED`) plus
  press/release frame counts.
- `component_mouse_position.hpp` — `C_MousePosition`, cached cursor.
- `component_mouse_scroll.hpp` — `C_MouseScroll`, ephemeral per-scroll
  event (`C_Lifetime{1}`).
- `component_glfw_gamepad_state.hpp` — `C_GLFWGamepadState`, 15 button
  states + 6 axes.

## Key systems

- `INPUT_KEY_MOUSE` (INPUT pipeline) — converts press/release counts
  into `C_KeyStatus` transitions.
- `INPUT_GAMEPAD` (INPUT pipeline) — polls GLFW gamepad state via
  `IRWindow::getGamepadState`.
- `HITBOX_MOUSE_TEST` (INPUT pipeline) — tests `C_HitBox2D` against the
  mouse position in screen space. Caches camera pos/zoom at
  `beginTick` to translate world → screen consistently.
- `SYSTEM_ENTITY_HOVER_DETECT` (INPUT pipeline) — dispatches
  `onHovered`/`onUnhovered`/`onClicked`/`onRightClicked` callbacks for
  entities whose hover state changed. Callbacks are
  `sol::protected_function`s registered from Lua.

## Commands

- `command_close_window.hpp` — `IRWindow::requestClose()`.

## Entity builders

- `entity_key_mouse_button.hpp` — two prefabs:
  - `kKeyMouseButton` — persistent button entity
    (`C_KeyMouseButton + C_KeyStatus`).
  - `kMouseScroll` — ephemeral scroll event (`C_Lifetime{1}`).
- `entity_joystick.hpp` — joystick entity; if `isGamepad`, also adds
  `C_GLFWGamepadState`.

## Gotchas

- **Hover tests need cached camera state.** `HITBOX_MOUSE_TEST`'s
  `beforeTick` caches `C_CameraPosition2DIso + C_ZoomLevel`. Moving
  the camera inside the same tick with a separate system can desync
  the hover test by one frame.
- **Mouse scroll is ephemeral.** Only valid for exactly one frame. Query
  it in the same pipeline event it's created, or you miss it.
- **`C_KeyStatus` transitions after counts.** The system updates press/
  release counts first, then transitions the state machine. If you read
  state and counts in the same system, make sure you read them in the
  right order.
- **Hover callbacks are Lua-only.** `SYSTEM_ENTITY_HOVER_DETECT` stores
  `sol::protected_function`. C++ callers need to round-trip through a
  Lua registration path, or you have to add a new handler type.
- **`C_GLFWGamepadState::updateState()` must run once per frame.**
  Calling it twice swallows transitions.
