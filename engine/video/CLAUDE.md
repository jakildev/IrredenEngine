# engine/video/ — screenshots and recording

Captures the main framebuffer (or a specific canvas) to disk as PNG
screenshots or mp4 video via FFmpeg. All screen-capture code in the engine
funnels through `VideoManager`.

## Entry point

`engine/video/include/irreden/ir_video.hpp` — exposes `IRVideo::` free
functions:

- `requestScreenshot()` — next frame, dump the framebuffer to a numbered
  PNG in the configured output dir.
- `requestCanvasScreenshot(canvasName)` — same, but from a named canvas.
- `startRecording()` / `stopRecording()` / `toggleRecording()`.
- `recordFrame()` — called every render frame by the
  `RECORD_FRAME` system; no-op if recording isn't armed.
- `configureScreenshotOutputDir(path)`.
- `notifyFixedUpdate()` — called by the UPDATE pipeline to keep
  audio/video sync.

## VideoManager owns

- **`VideoRecorder`** — FFmpeg state machine (libavcodec, libavformat,
  libavutil, libswscale). Opens a format context, encodes frames, muxes
  with optional audio, finalizes on async thread.
- **PBO readback infrastructure** — double-buffered Pixel Buffer Objects
  for async GPU → CPU frame copies. Priming skips the first few frames so
  the PBO ring is valid.
- **Screenshot numbering** — seeded by scanning the output dir for
  existing files, so numbers don't collide across runs.
- **Audio arming** — delegates to `IRAudio::Audio` (or an external
  `IAudioCaptureSource`) for the recording's soundtrack.
- **Async finalize thread** — encoder shutdown runs off the main thread
  so stopping a long recording doesn't block. `toggleCapture` drops every
  toggle while it runs, so a stop-then-restart must wait it out:
  `IRVideo::recordingState()` reports `IDLE` / `RECORDING` / `FINALIZING`
  from atomics only (`m_captureEnabled`, `m_finalizeInProgress`) — never
  the recorder mutex, which the finalize thread holds for the whole flush.
  The flag → state table is the `constexpr` `recordingStateFrom` in
  `ir_video_types.hpp` (`FINALIZING` wins while both flags are set), so
  `test/video/recording_state_test.cpp` pins it at compile time.
  `VideoRecorder::m_isRecording` is atomic for the same reason: `stop()`
  clears it on that thread while `isRecording()` polls it on the main one.

## Frame timing

`notifyFixedUpdate()` increments a counter each UPDATE tick. `render()`
decides how many video frames to submit to the encoder based on the ratio
`(totalFixedUpdates × targetFps) / engineFps`. If rendering is behind,
duplicate frames are submitted (capped at 4 in a row) to catch up.

The upshot: **the video's clock is driven by UPDATE ticks, not wall clock
or RENDER ticks.** If the UPDATE pipeline stalls, the recording also
stalls instead of skipping time.

## FFmpeg and conditional compilation

`#if IR_VIDEO_HAS_FFMPEG` wraps every encoder call. If FFmpeg isn't found
at configure time, `IR_VIDEO_HAS_FFMPEG=0` and recording becomes a no-op
— recording calls still succeed, they just do nothing. Screenshots still
work either way.

On Windows, the FFmpeg DLLs (`avcodec-*.dll` etc.) live at
`C:\msys64\mingw64\bin` and must be on `PATH` at run time — see
[`docs/agents/BUILD.md`](../../docs/agents/BUILD.md) for the PATH-fix wrapper.

## Auto-screenshot helper

`engine/video/include/irreden/video/auto_screenshot.hpp` — declarative
shot-cycling helper that any creation can opt into with a shot table
plus five lines of wire-up. Used by the `render-debug-loop` and
`render-verify` skills.

- `AutoScreenshotShot` — one shot: zoom, camera-iso position,
  Z-yaw radians (default 0 — set non-zero to cover rotation
  regressions per render-debug-loop rotation-stable criterion), label,
  optional ROI crops, and `cullAction_` (NONE / FREEZE / UNFREEZE,
  default NONE). FREEZE pins the shared cull viewport at that shot's
  camera pose so later shots free-fly with the cull held — used by
  `shape_debug --cull-validate` to prove the cull retains the on-screen
  set (see [`docs/design/cull-validation-harness.md`](../../docs/design/cull-validation-harness.md)).
- `AutoScreenshotConfig` — warmup/settle frame counts and a
  caller-owned `const AutoScreenshotShot *` table.
- `createAutoScreenshotSystem` — RENDER-pipeline system that cycles
  through shots, triggers one screenshot per shot, and calls
  `IRWindow::closeWindow()` when done.
- `appendAutoScreenshotIfRequested(pipeline, warmupFrames, shots,
  settleFrames = 3)` — the registration convenience entry point (#2969).
  Array-deducing template: pass the file-scope shot table directly and
  `N` (hence `numShots_`) is deduced, so there is no
  `sizeof(kFoo) / sizeof(kFoo[0])` to accidentally pair against a
  differently-named table. No-ops (pushes nothing) when `warmupFrames <=
  0`, so the caller's `if (warmupFrames > 0) { ... }` block collapses to
  one call. This is the **default** for an ordinary single-table
  registration.
- `setAutoScreenshotShots(config, shots)` — the array-deducing table
  binder without the pipeline/warmup wrapping, for a configurator that
  needs the full `AutoScreenshotConfig` surface (selects among several
  candidate tables, sets a non-default `settleFrames_`, or wires
  `onCaptureFrame_`). Still closes the `shots_`/`numShots_` pairing seam
  for each candidate table; the configurator keeps its own
  `pipeline.push_back(createAutoScreenshotSystem(cfg))`.

Wire-up order in `main.cpp` (the common single-table case):

1. Call `IREngine::init(argc, argv)` — the engine parser handles
   `--auto-screenshot [frames]` as a built-in. Read the warmup count
   back via `IREngine::args().autoScreenshotWarmupFrames()`. This read
   stays in `main.cpp` — `engine/video` cannot call `IREngine::args()`
   itself without pulling `ir_engine.hpp`'s `World` dependency backward
   into a module `World` already links (see `engine/CLAUDE.md` "Module
   include discipline"), so the warmup count is always a caller-supplied
   parameter, never read by the helper.
2. Declare a `constexpr AutoScreenshotShot kShots[]` table at file scope
   (must outlive the game loop).
3. Call `IRVideo::appendAutoScreenshotIfRequested(renderPipeline,
   warmupFrames, kShots)` before `registerPipeline` fires.

A configurator with multiple candidate tables, a non-default settle
count, or an `onCaptureFrame_` hook keeps the explicit
`AutoScreenshotConfig` + `createAutoScreenshotSystem` + `push_back` form,
using `setAutoScreenshotShots(cfg, kCandidateTable)` per candidate instead
of a hand-computed `sizeof`. Reference callers:
`creations/demos/metal_clear_test/main.cpp` (single-table, via
`appendAutoScreenshotIfRequested`) and `creations/demos/fog_demo/main.cpp`
(multi-table selection, via `setAutoScreenshotShots`).

A creation whose RENDER pipeline is already registered before the warmup
check runs (the three Lua-driven demos that call `IREngine::runScript`
first and append post-hoc via `IRSystem::appendToPipeline`) also uses
`setAutoScreenshotShots` to bind the table, then calls
`IRSystem::appendToPipeline` directly — `appendAutoScreenshotIfRequested`
assumes a not-yet-registered pipeline vector, which these don't have.

**Creations that compose the RENDER pipeline inside a Lua-bindings
callback** (`IREngine::registerLuaBindings`) must read the warmup count
*inside that callback* instead — the callback fires from
`World::setupLuaBindings` partway through `IREngine::init`, so a step-1 read
placed after `init` returns lands after the pipeline is already built. The
failure is total: the `warmupFrames > 0` guard sees 0, no capture system is
ever created, nothing calls `IRWindow::closeWindow()`, and the run hangs to
the harness timeout with no screenshot (#2502). It is no longer *silent* —
`IREngine::gameLoop()` warns when `--auto-screenshot` or `--auto-record` was
provided and nothing armed a capture (#2941), so the hang is greppable
instead of mute (the `--auto-record` case also closes the window, so that
run exits clean with no clip). A creation that instead builds its pipeline
in `main()` after `init` (the step-1–3 order above) is unaffected.

## Auto-record helper

`--auto-record [frames]` (bare = `IRArgs::kDefaultAutoRecordFrames`, 180) is
the clip twin of `--auto-screenshot`, same header and wire-up: read
`IREngine::args().autoRecordFrames()` in `main.cpp` (or inside the bindings
callback, per the note above), then
`IRVideo::appendAutoRecordIfRequested(renderPipeline, frames)` before
`registerPipeline` (no-op at 0; `createAutoRecordSystem` is the explicit
form; `canvas_stress` and `shape_debug` are the reference callers). The
system warms up 10 frames, starts the recorder via the toggle path, counts
`frames` RENDER frames, stops, and closes the window on the next frame — one
start, one stop, never a toggle during the async finalize; a recorder that
fails to start (FFmpeg absent) warns and exits clean with no clip. It marks
auto-capture active (fixed step), so `frames` is `frames / kFPS` seconds of
sim time and the encoder emits `frames × video_capture_fps / kFPS` frames
(minus PBO priming on OpenGL) to `capture.mp4` beside the exe. `ir-run`
treats the arg as a self-terminating GPU verb (`RESULT=CLEAN`).

Clip size: `video_capture_output_width` / `_height` (config keys, default 0
= follow the render output resolution) set the encoded frame size (odd
values round down for yuv420p). Both zero follows the render output; setting
just one derives the other from the render output's aspect at recording
start (`IRVideo::resolveCaptureOutputResolution`), so a non-16:9 creation
isn't squished by a preset tuned for a 16:9 one; setting both replaces the
render output resolution outright. `video_capture_bitrate` is the budget at
the render output resolution and scales with the resolved output's
pixel-area ratio, so a smaller output is proportionally smaller. Pass them
per run with `--config-preset <file>` (its `config = { … }` overlays
`config.lua`, see `engine/world/CLAUDE.md`).

## Commands and components (prefabs/irreden/video)

- `command_take_screenshot` → `requestScreenshot()`.
- `command_take_screenshot_canvas` → `requestCanvasScreenshot()`.
- `command_toggle_recording` → `toggleRecording()`.

## Gotchas

- **PBO priming.** First 2+ frames of a recording are dropped to fill the
  PBO ring. Starting/stopping rapidly truncates short clips.
- **Partial files on crash.** Writes are not atomic. A crash during
  encoding leaves a half-written mp4 that most players refuse to open.
- **macOS mic permission.** `IAudioCaptureSource` asks for microphone
  access; if denied, audio is silently dropped and video is muxed mute.
- **Latency compensation is auto.** `getInputLatencyMs()` from the audio
  source is applied as a sync offset; an obvious A/V offset starts there.
- **World flags delay recording.** `World::m_waitForFirstUpdateInput` and
  `m_startRecordingOnFirstInput` delay recording until the first input
  arrives. If a recording isn't starting, check those flags in
  `engine/world/` before blaming this module.
