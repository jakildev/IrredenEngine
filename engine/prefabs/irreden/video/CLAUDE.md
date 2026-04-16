# engine/prefabs/irreden/video/ — screenshot and recording commands

Thin wrapper around `engine/video/`. Mostly commands; components are
placeholder tags. There is no per-entity capture state machine — all
capture is global, triggered via `IRVideo::` free functions.

## Components

All currently tags / placeholders. `C_FramebufferCapture` is intended to mark
framebuffers that participate in capture, but the capture pipeline doesn't read
it yet. Treat all components here as sketches until they're wired into
`engine/video/`.

## Commands

Three commands wrap `IRVideo::` free functions: take a screenshot, take a
canvas screenshot, and toggle recording. All three just enqueue requests that
are serviced on the next render frame.

## Systems

A video encoder system exists but is largely commented out — an earlier attempt
at ECS-driven encoder integration. **Do not resurrect without a design pass**;
read `engine/video/CLAUDE.md` first.

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
