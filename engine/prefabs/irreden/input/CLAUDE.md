# engine/prefabs/irreden/input/ — key/mouse/gamepad + hover

Input state components, hitbox-based hover/click detection, and the
systems that populate button state. The underlying polling lives in
`engine/input/`; this directory is the ECS surface.

## Key components

- `C_HitBox2D` — half-extents + `hovered_` flag. World-anchored; tested
  against mouse position in framebuffer-pixel space per frame.
- `C_HitBox2DGui` — full width/height (top-left + size AABB) + `hovered_`
  flag, anchored to a sibling `C_GuiPosition`. Coordinates live in **gui
  canvas trixel** units, the same space the GUI canvas writes into. Kept
  separate from `C_HitBox2D` so the two coordinate systems never get
  mixed inside one tick.
- `C_KeyboardKey` — GLFW key code.
- `C_KeyMouseButton` — wraps the `KeyMouseButtons` enum value. One entity
  per physical button, created at engine init.
- `C_KeyStatus` — a button state machine
  (`NOT_HELD`/`PRESSED`/`HELD`/`RELEASED`/`PRESSED_AND_RELEASED`) plus
  press/release frame counts.
- `C_MousePosition` — cached cursor.
- `C_MouseScroll` — ephemeral per-scroll event (`C_Lifetime{1}`).
- `C_GLFWGamepadState` — 15 button states + 6 axes.

## Key systems

- `INPUT_KEY_MOUSE` (INPUT pipeline) — converts press/release counts
  into `C_KeyStatus` transitions.
- `INPUT_GAMEPAD` (INPUT pipeline) — polls GLFW gamepad state via
  `IRWindow::getGamepadState`.
- `HITBOX_MOUSE_TEST` (INPUT pipeline) — tests `C_HitBox2D` against the
  mouse position in screen space. Caches camera pos/zoom at
  `beginTick` to translate world → screen consistently.
- `HITBOX_MOUSE_TEST_GUI` (INPUT pipeline) — tests `C_HitBox2DGui` +
  `C_GuiPosition` against the mouse mapped into gui-canvas trixel
  coordinates. Caches the mapped mouse position at `beginTick` so the
  per-entity tick is two compares.
- `SYSTEM_ENTITY_HOVER_DETECT` (INPUT pipeline) — resolves the hovered
  entity from three sources in priority order **GUI > world > trixel**
  (first GUI hit wins; if none, first world hit; otherwise the GPU
  trixel entity-id readback) and dispatches
  `onHovered`/`onUnhovered`/`onClicked`/`onRightClicked` callbacks for
  the entity whose hover state changed. Tie-break across entities
  within a source is archetype iteration order. Callbacks are
  `sol::protected_function`s registered from Lua.

**Pipeline registration order (INPUT) for hover dispatch.** Both hitbox
systems must run **before** `SYSTEM_ENTITY_HOVER_DETECT` so their
`hovered_` flags are current when the resolver scans them:
`HITBOX_MOUSE_TEST` → `HITBOX_MOUSE_TEST_GUI` → `SYSTEM_ENTITY_HOVER_DETECT`.
A creation that wants only one source can register the relevant subset
— the resolver tolerates absent sources (no entities → no hits → falls
through to the next priority).

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
