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
  so stopping a long recording doesn't block.

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

On Windows, the FFmpeg DLLs (`avcodec-*.dll`, `avformat-*.dll`,
`avutil-*.dll`, `swscale-*.dll`) live at `C:\msys64\mingw64\bin` and must
be on `PATH` when the executable runs. See the top-level CLAUDE.md for
the PATH-fix wrapper.

## Auto-screenshot helper

`engine/video/include/irreden/video/auto_screenshot.hpp` — declarative
shot-cycling helper that any creation can opt into with a shot table
plus five lines of wire-up. Used by the `render-debug-loop` and
`render-verify` skills.

- `AutoScreenshotShot` — one shot: zoom, camera-iso position, label.
- `AutoScreenshotConfig` — warmup/settle frame counts and a
  caller-owned `const AutoScreenshotShot *` table.
- `parseAutoScreenshotArgv` — CLI parser for
  `--auto-screenshot [frames]`.
- `createAutoScreenshotSystem` — RENDER-pipeline system that cycles
  through shots, triggers one screenshot per shot, and calls
  `IRWindow::closeWindow()` when done.

Reference callers: `creations/demos/shape_debug/main.cpp` and
`creations/demos/metal_clear_test/main.cpp`. The shot table must outlive
the game loop — `constexpr AutoScreenshotShot kShots[]` at file scope
is the idiomatic shape.

## Commands and components (prefabs/irreden/video)

- `command_take_screenshot` → `requestScreenshot()`.
- `command_take_screenshot_canvas` → `requestCanvasScreenshot()`.
- `command_toggle_recording` → `toggleRecording()`.
- `C_FramebufferCapture`, `C_FramebufferOutputPosition`,
  `C_OutputResolution` — placeholder components tagging framebuffers
  that participate in capture.

## Gotchas

- **FFmpeg missing → silent.** `IR_VIDEO_HAS_FFMPEG=0` makes
  `startRecording()` a no-op. Check build logs if recording "doesn't
  work".
- **PBO priming.** First 2+ frames of a recording are dropped to fill the
  PBO ring. Starting/stopping rapidly truncates short clips.
- **Finalize-thread races.** Don't call `toggleRecording()` while the
  finalize thread is still joining — locks are there, but you can still
  deadlock if the main thread is holding the recorder mutex.
- **Partial files on crash.** Writes are not atomic. A crash during
  encoding leaves a half-written mp4 that most players refuse to open.
- **macOS mic permission.** On macOS, `IAudioCaptureSource` asks for
  microphone access. If denied, audio is silently dropped and video is
  muxed without a soundtrack.
- **Latency compensation is auto.** `getInputLatencyMs()` from the audio
  source is applied as a sync offset. If your audio has an obvious
  offset in the mp4, that's where to poke.
- **World flags delay recording.** `World::m_waitForFirstUpdateInput` and
  `m_startRecordingOnFirstInput` delay recording until the first input
  arrives. If a recording isn't starting, check those flags in
  `engine/world/` before blaming this module.
