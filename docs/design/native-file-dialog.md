# Native file dialog — let the user pick files or a folder

A creation loads files by path. Until now the path had to be known to the
creation: there was no way for the person at the keyboard to point at a set
of images or a folder through the operating system. `IREngine::openFileDialog`
and the opt-in Lua `IRDialog.open` show the host's native "open" panel and
return what was picked; everything after the pick stays in engine code and
Lua.

Code: [`file_dialog.hpp`](../../engine/window/include/irreden/window/file_dialog.hpp)
(request, result, `runFileDialog`, the per-host `platformOpenFileDialog`),
`World::openFileDialog` (the modal call),
[`path_grants.hpp`](../../engine/script/include/irreden/script/path_grants.hpp)
(`PathGrants`), and
[`lua_file_dialog_bindings.hpp`](../../engine/script/include/irreden/script/lua_file_dialog_bindings.hpp)
(`IRDialog`). Lua surface summary:
[`engine/script/CLAUDE.md`](../../engine/script/CLAUDE.md) §"Engine service bindings".

## The call

```cpp
IRWindow::FileDialogRequest request;
request.mode_ = IRWindow::FileDialogMode::OPEN_FILES;   // OPEN_FILE | OPEN_FOLDER
request.title_ = "Choose images";
request.directory_ = "data/images";                     // where the panel starts
request.filters_ = {{"Images", {"png", "jpg"}}};
const IRWindow::FileDialogResult result = IREngine::openFileDialog(request);
// result.status_, result.paths_ (absolute, UTF-8), result.error_
```

```lua
-- once, in the creation's binding registration: script.bindLuaFiles(); script.bindLuaFileDialog()
local r = IRDialog.open{
    mode = IRDialog.Mode.OPEN_FILES,
    title = "Choose images",
    directory = "data/images",
    filters = { { name = "Images", extensions = { "png", "jpg" } } },
}
for _, path in ipairs(r.paths) do          -- empty unless r.status == IRDialog.Status.ACCEPTED
    IRGui.setImagePath(image, path)        -- or IRFile.readText(path)
end
```

- `mode` defaults to `OPEN_FILE`. `OPEN_FILES` allows several; `OPEN_FOLDER`
  picks one directory and takes no filters.
- `directory` is where the panel opens. A relative one resolves from the
  working directory; one that is not a directory is ignored. It is shown to
  the user, never read on the caller's behalf, so it needs no grant.
- A filter is a name plus bare extensions. `.png` and `*.png` are accepted
  and stripped, `*` matches everything, and an empty name becomes the pattern
  list. An empty filter list shows every file.
- The result is one of five statuses. `paths_` is non-empty exactly when the
  status is `ACCEPTED`.

| Status | Meaning |
|---|---|
| `ACCEPTED` | the user chose; every path is absolute |
| `CANCELLED` | the user closed the panel |
| `REFUSED` | no panel was shown because this run must not block on one |
| `UNSUPPORTED` | this host has no panel (Linux without `zenity` or a display) |
| `FAILED` | the request was invalid, or the OS reported an error |

From Lua, a malformed option table raises before anything is shown: an
unknown key, a `mode` that is not an `IRDialog.Mode` integer, a non-string
title or directory, a sparse or non-array `filters`, an extension that is not
one. Everything else comes back as a status, so a script can call it
unconditionally and branch on the answer.

## Decisions

### 1. The call is modal and blocks the main thread

`World::openFileDialog` parks the main thread inside the OS panel, the same
shape as [`IREngine.handOff`](engine-hand-off.md). While it is open:

- **No update and no render run.** The engine keeps its window, its context
  and its last presented frame. Nothing is drained: no other process needs
  the GPU.
- **The clock is re-baselined on return** (`TimeManager::resumeAfterSuspend`).
  Without it the panel's whole lifetime would arrive as a clamped burst of
  UPDATE catch-up ticks and one long RENDER delta. The fixed-step tick count
  does not advance while the panel is open; the manual fixture prints equal
  frame and tick readings on either side of it.
- **Held keys and buttons are released, not stuck.** The engine window loses
  input focus to the panel, and GLFW answers a focus loss by sending a
  release for every key and mouse button it believed held. Those releases
  land in the engine's input queue and are processed on the next INPUT
  frame, so the shortcut or click that opened the panel does not read as
  held afterwards.
- **Input aimed at the engine window is blocked by the OS** on macOS (the
  panel is a sheet on that window) and Windows (the owner is disabled). On
  Linux the `zenity` panel is a separate, unparented window: a click on the
  engine window while it is open is queued by the display server and
  delivered when the loop resumes.

An asynchronous dialog (poll for a result each frame) was rejected: it needs a
handle, a lifetime and a cancel path on three platforms, and every caller so
far wants the answer before its next line.

### 2. A capture run or an unattended run never opens a panel

A modal panel in a run nobody is watching is a hang. `runFileDialog` refuses,
without calling the panel, when any of these hold:

- the run is a capture run: `--auto-screenshot`, `--auto-record` or
  `--auto-record-realtime` on the command line (`IREngine::init` tells the
  world before any capture system has armed), or an armed capture system
  (`IRVideo::isAutoCaptureActive()`, which also covers the GUI-test path);
- the window is not `WindowMode::NORMAL`. `background`, `hidden` and
  `offscreen` are the modes the launcher gives an unattended run
  ([`engine/window/CLAUDE.md`](../../engine/window/CLAUDE.md) "Window modes"),
  so a watchdog run, a probe or a fleet smoke test is covered without a
  capture flag;
- the call is off the main thread.

The request is validated first, so an invalid one is `FAILED` in every kind
of run, a capture run included: the bug surfaces where the tests are.

### 3. A picked path is granted, not trusted

Lua file access is confined. `IRFile`, the `IRGui` image widget and
`IRCanvas.loadImage` resolve a path with `IRUtility::resolveContainedPath`, which refuses anything absolute
and anything that leaves the base directory. A panel returns arbitrary
absolute paths, so something has to carry "the user chose this" to those
readers.

**The decision: `IRDialog.open` registers what it returns in a per-script
grant set, and the confined readers accept an absolute path only when that
set holds it.** `PathGrants` lives on `LuaScript`:

- **It starts empty and no Lua function adds to it.** Only C++ grants:
  `IRDialog.open` for exactly the paths the panel returned,
  `IRInput.droppedFiles` for exactly the paths the user dropped on the window
  ([Dropped files](#dropped-files)), and a creation's own C++ through
  `LuaScript::pathGrants()` when it decides to. A script cannot widen its own
  reach, and replacing either function from Lua grants nothing.
- **A picked file grants that file only**, not its folder: one image picked
  from `~/Pictures` does not make `~/Pictures` readable. **A picked folder
  grants its whole subtree**, which is what choosing a folder means.
- **Grants are read-only.** `IRFile.readText`, `IRFile.mtime`, `IRFile.list`,
  `IRGui.makeImage`, `IRGui.setImagePath` and `IRCanvas.loadImage` consult
  them;
  `IRFile.writeText` does not. A save panel would be a separate grant kind.
- **Grants last for the session** (the `LuaScript`'s lifetime) and are not
  persisted. A creation that wants "recent files" stores the paths and asks
  again, or its C++ re-grants them deliberately.
- **Containment is not weakened for anything else.** A relative path still
  goes through `resolveContainedPath` unchanged. An absolute path is resolved
  (`weakly_canonical`: symlinks followed, `..` folded) and compared against
  the canonical grants, so `<granted>/../x`, a symlink inside a granted
  folder that points out of it, and a string-prefix sibling
  (`<granted>2/x`) are all refused.
- **Every path Lua receives is readable.** A pick that no longer resolves by
  the time it is granted is dropped from `paths`, with the count in `error`;
  if nothing is left the status becomes `FAILED`.

`IRFile.list(path)` exists for the folder pick: a picked folder is of no use to
a script that cannot enumerate it. It returns the directory's immediate
entries, sorted by name, as `{ name, directory }` rows, or nil for anything
unreadable; it takes the same root-relative or granted paths as `readText`.

Rejected alternatives:

- *Opaque tokens the loaders accept.* The token would have to be threaded
  through every reader and every Lua data structure that holds a path, and a
  folder pick would need a token-plus-relative-path form. A granted absolute
  path is a plain string a script can store, join and print.
- *Granting the folder of a picked file.* Convenient for sibling assets, but
  it makes one click expose a whole directory the user never selected.
- *Returning the paths and letting C++ load them.* That is what
  `IREngine::openFileDialog` already is; it does not give Lua-authored
  creations a way to use the pick.

### 4. Where each part lives

- `engine/window/` owns the panels. It already holds the GLFW native handle
  and the Cocoa / Win32 split, and CMake picks one `file_dialog_<host>`
  source the way it picks the window-mode hooks and the render backends.
  `file_dialog.cpp` is the shared half: request validation, the refusal
  gates, the result check, and the pure argument builders the Windows and
  Linux panels use, so every host's tests cover them.
- `World` owns the modal call, because the gates and the clock belong to it:
  the capture state, the window mode, the main-thread check and the time
  manager. The panel is injected (`World::setFileDialogPanel`).
- `engine/script/` owns the grant set and the binding. `LuaScript` holds the
  invoker `IRDialog.open` calls; `World` installs its own, and a bare
  `LuaScript` (a test, a tool) reports `UNSUPPORTED`.

## Dropped files

Dragging files or a folder from the host's file manager onto the engine
window is the same consent as a pick, with no panel and no blocking call. The
window records each drop; the creation polls for it.

Code: [`file_drop.hpp`](../../engine/window/include/irreden/window/file_drop.hpp)
(`FileDropEvent`), `irglfwCallback_drop` and the window's drop queue
(`ir_glfw_window.hpp`), `InputManager::tick` (delivery), `IRInput::droppedFiles`
/ `IRInput::injectFileDrop` (`ir_input.hpp`), and `bindFileDrops` in
[`lua_input_bindings.hpp`](../../engine/script/include/irreden/script/lua_input_bindings.hpp)
(the Lua poll and the grant).

```cpp
for (const IRInput::FileDropEvent &drop : IRInput::droppedFiles()) {
    // drop.paths_ (absolute, UTF-8), drop.windowPoint_
}
```

```lua
-- bound by script.bindLuaCommands(); reading the files also needs bindLuaFiles()
for _, drop in ipairs(IRInput.droppedFiles()) do
    for _, path in ipairs(drop.paths) do
        IRGui.setImagePath(image, path)    -- or IRFile.readText(path), IRCanvas.loadImage(path)
    end
    -- drop.x, drop.y: where it landed
end
```

- **One drop is one event.** A drag of several items arrives as one event
  holding every path, in the order the host reported them. Two drops in one
  frame are two events, in arrival order.
- **The position is the cursor at the drop**, in the logical window points
  `IRInput::getMousePosition()` reports (top-left origin, not framebuffer
  pixels). There are no per-widget drop targets: a creation that cares where
  the drop landed hit-tests the point itself.
- **A drop lives for one frame.** The GLFW drop callback queues it on the
  window, and `InputManager::tick()` moves the queue into the frame's list
  with the frame's key, button and scroll events. INPUT, UPDATE and RENDER
  all read the same list, every poll in the frame returns it, and the next
  `tick()` clears it. A drop nobody polled is gone, exactly as an unread
  `scrollDelta()` or `pointerGestures()` is. Poll once a frame from INPUT or
  RENDER; an UPDATE tick can run zero or several times in a frame.
- **Polling from Lua is what grants.** `IRInput.droppedFiles()` grants each
  path it returns to the polling script, under decision 3's rules: a dropped
  file is granted as that file only, a dropped folder with its whole subtree,
  read-only, for the session. A path that no longer resolves is left out, and
  a drop left with no paths is left out with it, so every path Lua receives
  is readable. A drop that reaches the frame list but that no script polls
  grants nothing, and the C++ accessor grants nothing either: C++ is not
  confined.
- **Nothing is refused.** A drop cannot block, so there is no capture-run or
  window-mode gate. `hidden` and `offscreen` windows are never mapped, so
  nothing can be dropped on them; a `background` window can take a drop, and
  one that arrives is a person's. Registering the callback costs those modes
  nothing. A run under synthetic input ignores the window's drops along with
  the rest of its input.

### Synthetic drops

`IRInput::injectFileDrop(event)` and Lua
`IRInput.injectFileDrop(paths, x, y)` queue a drop for the next frame, so a
GUI test can exercise a creation's drop handling with no drag. Like the other
inject calls they require `beginSyntheticInput()` (an assert in a debug
build), and an injected drop is only ever delivered while synthetic input is
active. The Lua call raises on a `paths` that is not a dense array of at least
one string, or a non-finite `x` / `y`, before anything is queued. The cursor
snapshot is not moved.

**An injected drop is granted exactly as a real one is**, because a test has
to be able to read what it dropped. That makes the synthetic-input surface a
way to name a path to oneself, so it is kept apart from the poll:

- `IRInput.droppedFiles` is bound by `bindLuaCommands()`, the surface every
  Lua creation already binds. It can only ever return what the window or an
  active synthetic session delivered.
- `IRInput.injectFileDrop` is bound only by `bindLuaInput()`, the harness
  surface, next to `beginSyntheticInput`. A script that holds that surface
  can already act as the user in every other respect, and starting a
  synthetic session cuts the real keyboard and mouse off for the rest of the
  run. A creation that runs scripts it does not trust does not bind it, and
  then no Lua function can add a grant.

### What is left out

No hover feedback while a drag is over the window, no drag out of the window,
no text or URL drops, and no per-widget targets. Drops are not part of the
GUI-test shot table (`IRVideo::GuiInputEvent`); a test injects them directly.

### Hosts

GLFW owns the host protocol (Cocoa dragging destination, `WM_DROPFILES`,
XDND) and reports absolute UTF-8 paths. The engine side is identical on every
host. On macOS the tests read the registered callback back from a real engine
window and call it the way GLFW does, through to Lua; **a drag performed by
the OS has not been driven on any host**, and the engine side has not been
built on Windows or Linux.

## Platforms

| Host | Panel | Parented | Verified |
|---|---|---|---|
| macOS | `NSOpenPanel` | a sheet on the engine window | built; opened on the engine window |
| Windows (MSYS2 mingw64) | `IFileOpenDialog` (COM) | owner window | **not built, not run** |
| Linux | `zenity --file-selection` | no | **not built, not run** |

- **macOS.** The panel is presented as a sheet and the call pumps the
  application's own event queue until the sheet's handler runs. That keeps
  the call synchronous while GLFW's window delegate stays live. `NSOpenPanel`
  has no filter menu, so the filters narrow what is selectable to the union
  of their extensions; a `*` filter, or an extension with no uniform type,
  lifts the restriction. A window that is not visible gets the app-modal
  `runModal` instead.
- **Windows.** The dialog runs on the main thread in a single-threaded COM
  apartment, shared if the thread already holds one. A main thread whose
  apartment is multithreaded cannot host the shell dialog and gets
  `UNSUPPORTED` instead of a marshalled dialog on another thread.
  `FOS_NOCHANGEDIR` keeps the working directory, which every relative engine
  path resolves from. `Show` runs its own message loop, so the engine window
  keeps repainting and is never marked "Not Responding".
- **Linux.** GLFW links no toolkit, so the panel is the desktop's `zenity`
  when one is on `PATH` and a display is reachable (`DISPLAY` or
  `WAYLAND_DISPLAY`); otherwise the call is `UNSUPPORTED` with the reason.
  Its output is text, and a path may hold any byte but NUL, so no fixed
  separator can frame a multiple selection: a newline-joined output would
  read a picked `…/name\n/etc/passwd` as two picks and grant the second. A
  single selection is the whole output less zenity's trailing newline; a
  multiple selection is joined by a separator drawn fresh from the OS entropy
  source per call, which a path can hold only by guessing it. An
  XDG portal backend would remove the dependency and can replace this source
  without touching callers.

## Limits

- Open only. There is no save panel and no write grant.
- One panel at a time: the main thread is inside the panel, so nothing can
  ask for a second.
- Paths are UTF-8 `std::string`s. On Windows the confined readers build a
  wide path from a granted one; other engine loaders that take a narrow path
  still read it in the active code page.

## Validators

- `fleet-run IrredenEngineTest --gtest_filter='*FileDialog*:PathGrants*:ResolveReadablePath*'`
  — request and extension normalization, every refusal gate, the result
  check and the backend argument builders (`test/window/file_dialog_test.cpp`);
  the grant set (`test/script/path_grants_test.cpp`); and the Lua seam: option
  validation, the grant reaching `IRFile`, the capture-run refusal reaching
  Lua (`test/script/lua_file_dialog_bindings_test.cpp`). The image loaders'
  grant cases are in `test/script/lua_widget_image_test.cpp` and
  `test/script/lua_canvas_draw_image_test.cpp`.
- `fleet-run IrredenEngineTest --gtest_filter='*FileDrop*'` — on a real,
  never-shown engine window: the drop callback is registered, a drop becomes
  one queued event that owns its paths, delivery lasts one frame, synthetic
  input swaps the source, and injecting outside it asserts
  (`test/input/file_drop_test.cpp`); and the Lua seam: an empty poll, a drop
  through the callback read back through `IRFile`, file and folder grants,
  nothing granted without a poll, per-script grants, and the inject call's
  argument checks and gating (`test/script/lua_file_drop_test.cpp`).
- `fleet-run --timeout 120 IRLuaWidgetsDemo --file-dialog-fixture auto --window-mode normal`
  — the real panel, by hand: it opens 30 ticks in, and the log prints
  `LUA_WIDGETS FILEDIALOG before` / `closed` with equal `frame=` / `tick=`
  readings, the status, and each picked path read back through `IRFile`.
  Without `--window-mode normal` an unattended launch prints
  `status=REFUSED` and nothing opens.
- `fleet-run --timeout 120 IRLuaWidgetsDemo --file-dialog-fixture button --window-mode normal`
  — a real drag, by hand: drop files or a folder on the window and the log
  prints `LUA_WIDGETS FILEDROP` with the drop point and each path's byte or
  entry count read back through `IRFile`.
