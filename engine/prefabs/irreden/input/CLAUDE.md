# engine/prefabs/irreden/input/ — key/mouse/gamepad + hover

Input state components, hitbox-based hover/click detection, and the
systems that populate button state. The underlying polling lives in
`engine/input/`; this directory is the ECS surface.

## Key components

- `C_HitBox2D` — half-extents + `hovered_` flag. Tested against mouse
  position per frame in **world space** (camera-transformed iso).
- `C_HitBox2DGui` — full width/height + `hovered_` flag. AABB extends
  `[pos, pos + size_)` from a sibling `C_GuiPosition`, in
  **GUI-canvas-trixel coordinates** (top-left origin, +X right, +Y down).
  Kept separate from `C_HitBox2D` because the world hitbox lives in
  camera-transformed iso and the GUI hitbox lives in screen-space
  trixels — mixing the two coordinate systems silently mis-locates hover
  in either path.
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
- `HITBOX_MOUSE_TEST_GUI` (INPUT pipeline) — tests `C_HitBox2DGui +
  C_GuiPosition` against the mouse projected into GUI-canvas-trixel
  coordinates. Computes the projection once per frame in `beginTick`
  by inverting the screen-residual-yaw rotation (matching
  `HITBOX_MOUSE_TEST`'s convention) and scaling
  `mouseFb / framebufferRes * guiCanvasSize`.
- `SYSTEM_ENTITY_HOVER_DETECT` (INPUT pipeline) — dispatches
  `onHovered`/`onUnhovered`/`onClicked`/`onRightClicked` callbacks for
  entities whose hover state changed. Resolves the hovered entity from
  three sources in priority order: **GUI > world > trixel**. The two
  hitbox sources are scanned once per frame via `forEachComponent` (no
  per-entity `getComponent`) and the first archetype-iteration entry
  with `hovered_=true` wins; ties within a single hitbox source resolve
  in archetype-iteration order. Callbacks are `sol::protected_function`s
  registered from Lua.

## Pipeline order

For hover-driven creations, register the INPUT pipeline as:

```
HITBOX_MOUSE_TEST,
HITBOX_MOUSE_TEST_GUI,
SYSTEM_ENTITY_HOVER_DETECT,
```

The two hitbox systems write `hovered_` flags that
`SYSTEM_ENTITY_HOVER_DETECT` reads; reordering breaks the cascade.
Creations that don't use one of the hitbox sources can omit it — the
hover-detect scan walks zero entities for a missing component archetype
and falls through to the next priority tier.

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
