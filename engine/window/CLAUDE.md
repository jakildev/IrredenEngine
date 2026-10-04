# engine/window/ — GLFW window + GL context

Wraps GLFW window creation, the OpenGL context (when the GL backend is
selected), and the input-event queue that `InputManager` drains each
frame.

## Entry point

`engine/window/include/irreden/ir_window.hpp` — exposes `IRWindow::` free
functions around the global `g_irglfwWindow` pointer. Most code does not
touch the window directly; input goes through `InputManager`, rendering
goes through `RenderManager`.

## `IRGLFWWindow`

Owns:

- `GLFWwindow*` and the monitor list.
- Fullscreen state.
- Event queues for keys, mouse buttons, and scroll that `InputManager` drains
  each frame.

Construction sets GLFW hints: OpenGL 4.5 core profile when
`IR_GRAPHICS_OPENGL`, no API (`GLFW_NO_API`) for Metal/Vulkan builds.
4.5 is the floor — WSLg's Mesa-d3d12 driver caps there, while real
Linux/Windows GPU drivers expose 4.6. The engine uses no 4.6-only
features (no SPIR-V, no `gl_HelperInvocation`), so 4.5 lets a single
binary boot on every host.

`swapBuffers()` is called by `World::gameLoop()` at the end of each
render frame.

## Window modes

`IRWindow::WindowMode` (`ir_glfw_window.hpp`) selects how the window is
presented at creation, so an unattended launch never takes the screen from
whoever is using the host:

| Mode | What GLFW does | Use |
|---|---|---|
| `normal` | shown, focused, placed on the preferred monitor | a human at the keyboard |
| `background` | shown, `GLFW_FOCUSED` / `GLFW_FOCUS_ON_SHOW` off; lands behind the active window | watchdog (`--timeout`) runs a human may glance at |
| `hidden` | `GLFW_VISIBLE` off — never mapped; context, framebuffer, synthetic input and screenshot readback all work | capture runs (`--auto-screenshot` / `--auto-record` / `--auto-profile`) |

Resolution order is config `window_mode` < the `IR_WINDOW_MODE` env var <
`--window-mode` (engine-common arg; `IRArgs::Parser::windowMode()`). The
launcher fills the env rung only for a launch marked unattended
(`FLEET_UNATTENDED=1`, which `fleet-run` exports before exec'ing `ir-run` and
`fleet-up` exports to every pane), by the run's shape: capture verb → hidden,
watchdog → background, plain exec untouched; `FLEET_WINDOW_MODE` replaces
both defaults. No demo or skill names the flag, and a bare `ir-run` in a
human's shell carries no marker, so it shows its window with no flags.
`--window-mode normal` after the executable watches a fleet-shaped run.

Per-OS hooks live in `ir_window_platform.hpp`: on macOS both non-normal modes
switch the process to the accessory activation policy after `glfwInit` (no
Dock icon, no Cmd-Tab entry — and AppKit will not order an inactive app's
window over the active app's key window, which is what makes `background`
land behind). On Windows `background` pushes the new window to the bottom of
the z-order with `SWP_NOACTIVATE`, since GLFW's non-activating show still
inserts it on top. X11 needs nothing beyond the hints.

Minimized is deliberately not a mode: a minimized window reports a 0x0
framebuffer on Windows, and `VideoManager::captureScreenshot` skips every
shot. Fullscreen is ignored (with a log line) outside `normal`.

## Framebuffer vs. window size

Under HiDPI / scaling, the framebuffer is larger than the window. Use
`getFramebufferSize()` when you need the actual render target dimensions
and `getWindowSize()` when you need "logical" pixels. `RenderManager`
calls `getFramebufferSize()` internally.

## Gotchas

- **GLFW callbacks route via module-scope functions.** They look up the
  global `g_irglfwWindow` to post events. Creating a second window
  instance will not work without rethinking the callback plumbing.
- **No vsync flag in the public API.** Vsync is controlled via GLFW
  context hints at construction time. Flipping it post-init requires
  `glfwSwapInterval` plus a context-current check.
- **Fullscreen toggling recreates the GL context on some drivers.** Any
  GL resource handles become invalid after a fullscreen transition —
  `RenderManager` has to re-seat its buffers. Test before shipping.
- **Focus is not tracked by the input queue.** Background-window keys
  still enqueue. `engine/input/CLAUDE.md` covers the workaround.
- **`swapBuffers()` blocks on vsync.** If render FPS looks capped at
  60 for no reason, vsync is on.
