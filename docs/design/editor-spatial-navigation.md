# Editor spatial navigation

The voxel editor enables the engine's spatial view by default. Orbit changes
the camera's azimuth and elevation while retaining the engine's voxel/trixel
rendering, picking, depth, and lighting paths. It does not rotate authored parts
relative to their local grids. Other creations opt in with
`IRPrefab::Camera::setSpatialViewEnabled(true)`; the engine camera default
remains the legacy view. `IRVoxelEditor --legacy-view` selects the yaw-only
compatibility path for existing validation and comparisons.

World up is **-Z**. Ordinary orbit stays upright, with elevation clamped just
short of either pole. Drag or swipe through the horizon to inspect the underside;
crossing a pole does not flip the view. Roll is a separate, explicit gesture.

## Controls

| Input | Spatial-view behavior |
| --- | --- |
| Precise two-finger horizontal / vertical swipe | Orbit azimuth / elevation around the active construction-plane center |
| Shift + swipe | Pan |
| macOS native pinch | Zoom continuously at the gesture's cursor position |
| Mouse wheel | Zoom continuously at the cursor |
| Right-button drag | Orbit around the construction-plane center |
| Option/Alt + left-button drag | Orbit around the surface or floor under the initial click |
| Control + middle-button drag | Orbit around the initial click |
| Middle-button drag, or Space + left-button drag | Pan |
| Option/Alt + Shift + left-button drag | Roll around the initial click |
| Q / E | Step azimuth to the previous / next quarter turn |
| RESET VIEW or Home | Center the construction plane, restore the upright isometric orientation, and apply the configured home zoom |
| Command+0 on macOS | Reset view; Fn+Left is also the compact-keyboard Home equivalent |

A gold marker identifies the orbit anchor. Camera drags keep ownership through
button release, including when their modifier releases first; sculpting does
not consume that release. Native scroll and pinch ownership is decided at the
gesture's initial position and retained through its end or cancellation. A
gesture beginning on editor UI does not become a camera gesture when it crosses
into the scene. OS scroll momentum does not move the camera.

Zoom is continuous within the engine's 1–64 range. The point under the cursor
stays fixed while zoom changes; authored LOD tiers still switch discretely.
The legacy view retains horizontal-scroll yaw and vertical-scroll zoom.

## Device detection and configuration

On macOS the window receives native magnification and scroll phase metadata.
`AUTO` treats OS-reported precise scroll as orbit/pan and other scroll as wheel
zoom. Precision is an event property: a mouse can produce precise scroll too.
The GLFW fallback on other platforms reports an unknown source, which `AUTO`
treats as wheel input. Use `--scroll-input trackpad` for a device whose two-axis
scroll should orbit, or `--scroll-input wheel` for precise mice that should zoom.
The override does not synthesize native pinch support on platforms without it.
Native scroll deltas retain the OS natural-scrolling direction; the editor does
not invert them a second time.

On an ordinary launch, a missing `save_files/editor_navigation.cfg` is created
beside the executable. Set `IR_EDITOR_NAVIGATION_PATH` to choose another file.
Edit the file and restart to change defaults. The complete default file is:

```text
IRREDEN_EDITOR_NAVIGATION 1
home_zoom 4
wheel_zoom_rate 0.125
orbit_sensitivity 0.04
drag_sensitivity 0.005
pinch_zoom_rate 1
pan_sensitivity 1
scroll_mode 0
```

| Field | Meaning and accepted values |
| --- | --- |
| `home_zoom` | Reset zoom, 1–64; 0 preserves the current zoom |
| `wheel_zoom_rate` | Octaves per vertical scroll unit; 0.0001–4 |
| `orbit_sensitivity` | Radians per normalized precise-scroll unit; 0.0001–4 |
| `drag_sensitivity` | Radians per logical window-point drag; 0.0001–1 |
| `pinch_zoom_rate` | Octaves per native magnification delta; 0.0001–16 |
| `pan_sensitivity` | Multiplier on precise-scroll pan distance; 0.0001–100 |
| `scroll_mode` | 0 = AUTO, 1 = WHEEL, 2 = TRACKPAD |

All numeric values must be finite. Invalid files leave defaults intact; partial
files keep defaults for omitted fields. `--home-zoom`, `--scroll-zoom-rate`, and
`--scroll-input auto|wheel|trackpad` override the loaded values for that launch.
They do not rewrite the file. Named authoring sessions and profiling runs skip
navigation preferences. Automatic captures also skip them unless the explicit
scripted-preference opt-in (`IR_PREFS_DIR`) is present.

## Input and replay contract

`IRInput::pointerGestures()` exposes ordered events for the current frame,
preserved across INPUT/UPDATE/RENDER. Each event carries a kind, source, phase,
momentum phase, modifiers, and a position in logical window points with a
top-left origin. Positions belong to individual events, not the final cursor
snapshot. Zero-delta begin/end/cancel events remain meaningful.

Scroll deltas retain GLFW units; macOS precise native point deltas are scaled
by 0.1. Magnification is a separate native delta and never enters legacy scroll
totals. The window-scoped Cocoa monitor replaces that event's GLFW scroll
delivery, avoiding duplicate deltas. Existing scroll consumers retain their
scroll offsets and entities.

Synthetic input accepts `IRInput::injectPointerGesture(event)` in C++ and a Lua
table through `IRInput.injectPointerGesture`. Lua enum tables are
`PointerGestureKind`, `PointerGestureSource`, and `PointerGesturePhase` under
`IRInput`; table fields are `kind`, `source`, `phase`, `momentumPhase`, `x`, `y`,
`magnification`, `windowX`, `windowY`, `modifiers`, and `directionInverted`.
GUI replay uses `GuiInputEvent::Type::POINTER_GESTURE` and `pointerGesture_`.
Injection does not move the mouse snapshot: replay a MOVE separately when a
test also needs cursor hover, and resolve dynamic aims into the payload's own
`windowPoint_`.

Screenshot pose tables can set `AutoScreenshotShot::hasViewRotation_` and
`viewRotation_` to enable spatial rendering with a full camera quaternion in
both ordinary captures and GUI sessions. Without that override, the existing
`yawRadians_` path remains active and does not disable a creation's spatial
opt-in. This allows the same mixed-scene demo to exercise upright, underside,
and rolled views without replacing its rendering setup.

## Validation targets

The `spatial_navigation` native session exercises navigation and unchanged
authored geometry. Unit coverage targets ordered gesture metadata, legacy
scroll isolation, Lua/replay injection, and navigation-file validation and
round trips. These are validation entry points, not a claim that a particular
build has passed. Real-device acceptance should include diagonal swipes,
underside views, pinch at an off-center point, crossing UI boundaries during a
gesture, modifier release before button release, and the device-mode override.
