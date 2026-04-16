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

Construction sets GLFW hints: OpenGL 4.6 core profile when
`IR_GRAPHICS_OPENGL`, no API (`GLFW_NO_API`) for Metal/Vulkan builds.

`swapBuffers()` is called by `World::gameLoop()` at the end of each
render frame.

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
