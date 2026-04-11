# engine/prefabs/irreden/video/ — screenshot and recording commands

Thin wrapper around `engine/video/`. Mostly commands; components are
placeholder tags. There is no per-entity capture state machine — all
capture is global, triggered via `IRVideo::` free functions.

## Components

All currently tags / placeholders:

- `component_framebuffer_capture.hpp` — `C_FramebufferCapture`, empty
  tag. Intended to mark framebuffers that participate in capture, but
  the capture pipeline doesn't read it yet.
- `component_output_resolution.hpp` — `C_OutputResolution`, empty.
- `component_framebuffer_output_position.hpp` —
  `C_FramebufferOutputPosition`, empty.

Treat these as sketches until they're wired into `engine/video/`.

## Commands

- `command_take_screenshot.hpp` — `IRVideo::requestScreenshot()`.
- `command_take_screenshot_canvas.hpp` —
  `IRVideo::requestCanvasScreenshot()`.
- `command_toggle_recording.hpp` — `IRVideo::toggleRecording()`.

All three just call the corresponding free function. Requests are
serviced on the next render frame.

## Systems

- `system_video_encoder.hpp` — largely commented out. An earlier
  attempt at ECS-driven encoder integration; **do not resurrect without
  a design pass**, and read `engine/video/CLAUDE.md` first.

## Entity builders

None.

## Gotchas

- **Components are stubs.** Adding `C_FramebufferCapture` to an entity
  *does nothing* right now. Capture is global.
- **Commands only request.** The actual framebuffer readback and mp4
  muxing happens async in `VideoManager`. A command returning success
  just means the request was queued.
- **FFmpeg optional at compile time.** If `IR_VIDEO_HAS_FFMPEG=0`, the
  recording commands are silent no-ops. Check build logs before
  debugging "recording doesn't work".
- **No per-entity recording state machine.** Recording is a global
  flag. Two commands racing to `toggleRecording()` on the same frame
  can cancel each other out.
- **Screenshot numbering is global.** `IRVideo::` scans the output
  directory at startup to seed the next index. Deleting screenshots
  during a run can cause the next one to overwrite an old file.
