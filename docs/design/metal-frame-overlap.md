# Metal frame overlap

This implementation is WIP. The [complete measurement record](../perf/metal-frame-overlap/README.md)
retains the failing pre-rebase matrix. After rebasing over render-pass batching,
one fresh million-entity 0° pair has correct overlap coverage and latency
within the established noise bound, but it is not the required five-pair
matrix. The display is now awake; the attempted exact-base render control was
invalidated by shared GPU-lock timeouts, and the remaining Release matrix was
blocked by repeated macOS launch-service failures. This must not be treated as
a demonstrated latency improvement or an accepted default.

## Mechanism

`MetalRenderDevice` uses a late join. `present()` submits and retains one
command buffer; the CPU can record the following frame while it executes.
Before any subsequent commit, the device waits for that submitted buffer.
The admission bound is one executing submission, independent of drawables.
`finish()` joins it, commits the current recording buffer, and waits for both.

Completion polling, serial advancement, GPU frame timing and
`FrameCompletionQueue::finished` run on the main thread. Completion callbacks
were ruled out: they would require a synchronized handoff to the main thread,
while command-buffer status already supplies the needed completion witness.
No render state is touched by a Metal completion handler.

Command-buffer failures retain the submitted frame's number when collected
during a later CPU frame. Shutdown collects the final submission and folds any
late error into the profile's final counter sample before writing the report.

`WorldConfig::metal_frame_overlap` defaults to true. False restores the
blocking present in the same binary; the PerfGrid blocking presets load the
same scene preset and change only that field. OpenGL's frame preparation is
unchanged. The backend-neutral `prepareCpuFrame` hook lets World schedule input
without depending on Metal types.

## Input scheduling and measurement

Before input or fixed-update accounting, `prepareCpuFrame` delays the next CPU
frame toward predicted completion minus 30% of predicted CPU work, capped at
0.15 ms. Exponential means
track CPU encoding and submit-to-completion durations. The CPU estimate ends
before the admission wait. The short lead leaves room for CPU cost variation
without deliberately waiting behind most of the preceding GPU frame. A
coalesced sleep covers the long part of the delay; an active wait of at most
two milliseconds avoids sleeping through a short CPU frame. This consumes
CPU time while pacing and remains subject to the latency and throughput gates.
The next poll's placement changes; fixed updates still consume input through
the same `runMainLoopFrame` sequence.

When the CPU estimate is at least the submit-to-completion estimate, admission
keeps the blocking present. Always overlapping the CPU-bound fixture reduced
updates per rendered frame and made more renders reuse the preceding update's
input poll. In five guarded pairs that increased mean input latency by 1.638 ms,
outside the 0.140 ms bound. The CPU-bound fallback preserves the serialized
cadence; overlap targets frames whose GPU half is longer. Its measurements
must still pass the same latency gate.

The frame-time series includes this delay. The input probe's CPU interval
starts after it, and its GPU interval ends at the command buffer's GPU end,
mapped to the steady clock. `commandBufferSpans` remains actual GPU execution
time. The report counts steady frames that started with unfinished GPU work.

The pick ring still drains by completed submission serial. Each producer
retains its cursor sample time. Profiled INPUT ticks sample the served record
once after input state advances, reporting age relative to that tick's poll.
The pick-age row covers all profiled INPUT ticks, including warmup; the frame
latency rows exclude their first quarter. Stale picks remain counted once per
INPUT tick.

## Resource ownership

`MetalRuntimeState` has two submission resource sets: recording and submitted.
Each owns its staging arena, retired buffer backings and encoded-buffer set.
A submit exchanges them only after the preceding submission completes.
Completion recycles only the submitted set, preserving slices already written
for the frame the CPU is recording. Buffer writes consult both encoded sets.
Each buffer upload polls completion before deciding which rule applies.
Whole writes replace backings; aligned partial writes use staged GPU copies;
unaligned writes deliberately drain before writing.

Texture clears and 2D/3D uploads use ordered blits while a command buffer is
open. A buffer remains open between overlapped frames, including a frame with
no drawable, so no upload or pick copy mistakes that interval for idle startup.
Immutable clear patterns are replaced rather than modified. Command buffers
retain their encoded resources, so destroying a wrapper does not free memory
still used by its commands. Apple's [command-buffer contract](https://developer.apple.com/documentation/metal/mtlcommandqueue/makecommandbuffer%28%29)
sets `retainedReferences` for this creation API.

CPU readbacks remain completion-gated or explicitly drained. `mapRange`
drains if its backing is encoded. A stage timestamp pair stays pending until
its command buffer completes; the timing observer skips a busy slot instead
of reusing it. One pair per stage avoids exhausting the device's counter-buffer
quota; skipped invocations produce no fabricated timing sample.

## Deliberate drains

Every `finish()` that submits a recording buffer increments the profile's
mid-frame GPU drain counter. Deliberate callers are unaligned buffer writes,
mapping encoded buffers, framebuffer screenshots/video, composite-depth reads
at a camera gesture, legacy stage timing, child-process handoff, and debug
probes. Normal frame admission and final collection of an already submitted
frame are not mid-frame drains. A profiling-mode transition joins the preceding
submission before resetting its measurement window. The million control must
show zero drains during that window.

Capture keeps its existing fixed-step setup and screenshot/video readback
boundary. Offscreen pacing remains enabled. All captures, the headless
resource tests and the same-binary latency comparisons are required before
this implementation is ready.
