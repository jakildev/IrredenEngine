# Plan: render: Metal per-stage GPU timestamps undercount multi-encoder stages (only the first encoder is bracketed)

- **Issue:** #1746
- **Model:** opus
- **Date:** 2026-06-13

## Scope

After #1738 fixed the GPU-stage aggregation (Max==Avg), Metal per-stage GPU
times still grossly undercount on stages that issue **more than one command
encoder** (e.g. `VOXEL_TO_TRIXEL_STAGE_1`'s per-axis canvas passes): the
tracked stages sum to <1 ms against a ~57 ms frame. Make the Metal timestamp
pair span **all** encoders of a stage, so per-stage GPU times are trustworthy
for `/optimize` passes. OpenGL is correct already and must stay byte-for-byte
unchanged.

## Verified current state (premise confirmed in code)

All in `engine/render/src/metal/metal_render_impl.cpp`:

- `MetalTimestampSampleAttachment g_nextComputeTimestampAttachment;` (line 33)
  is a **one-shot**: `writeTimestamp(START)` (line 785) sets it
  `{sampleBuffer, startIdx=0, endIdx=1}`; the **first** encoder created after
  START consumes it and resets it to `{}`.
- `createComputeEncoder` (lines 114–132): if the attachment is set, attaches
  `setStartOfEncoderSampleIndex(0)` + `setEndOfEncoderSampleIndex(1)` to **one**
  compute encoder, then `g_nextComputeTimestampAttachment = {}` (line 130).
- `attachTimestampSamples` (lines 134–144): same for a render encoder via
  `setStartOfVertexSampleIndex(0)` / `setEndOfFragmentSampleIndex(1)`, then
  resets.
- `writeTimestamp(END)` (line 793) only flips `pair->hasEnd_ = true;` — no GPU
  effect, because the end sample was already bound to index 1 on the *first*
  encoder. Encoders 2..N of the stage get no attachment → **unmeasured**.
- `readTimestampPairMs` (line 798) resolves `Range::Make(0, 2)` and returns
  `samples[1] - samples[0]` — i.e. the first encoder's duration only.
- The sample buffer is created with `setSampleCount(2)` (line 715). The fix
  stays within this existing 2-sample buffer (overwrite index 1); **no resize**.
- OpenGL (`opengl_render_impl.cpp:202`): `glQueryCounter(GL_TIMESTAMP)` is
  inserted into the command stream at the exact START/END call sites, so the
  pair already spans the whole stage regardless of encoder count. **Metal-only
  bug; OpenGL untouched.**
- `recommendedTimestampPairsInFlight() == 1` and the present()/wait model mean
  one 2-sample buffer per stage is sufficient — no in-flight-depth change.
- Observer `engine/prefabs/irreden/render/gpu_stage_timing_observer.hpp`
  brackets each stage (onBeforeTick→START, onAfterTick→END) correctly — **no
  change needed there**.

## Approach (single, committed)

Make `g_nextComputeTimestampAttachment` **sticky across all encoders between
START and END**, sampling the start boundary on the first encoder only and the
end boundary on every encoder. Sequential GPU execution means the last
encoder's index-1 write wins, yielding `[first-encoder start, last-encoder
end]` — the whole stage — in the existing 2-sample buffer.

Edits, all in `engine/render/src/metal/metal_render_impl.cpp`:

1. **Extend the struct** `MetalTimestampSampleAttachment` (line 27): add
   `bool firstEncoder_ = true;` (tracks whether the start boundary has been
   claimed yet).

2. **`writeTimestamp(START)`** (line 785): set
   `g_nextComputeTimestampAttachment = { pair->sampleBuffer_, 0, 1, /*firstEncoder_=*/true };`
   (unchanged semantics, plus the new flag). Keep `hasStart_/hasEnd_` as-is.

3. **`createComputeEncoder`** (lines 122–131): when the attachment is active —
   - `startIndex = firstEncoder_ ? startSampleIndex_ : MTL::CounterDontSample`
     (`MTLCounters.hpp` defines `CounterDontSample = (uint64_t)-1`);
   - `endIndex = endSampleIndex_` (always 1);
   - attach via `setStartOfEncoderSampleIndex/​setEndOfEncoderSampleIndex`;
   - then **`g_nextComputeTimestampAttachment.firstEncoder_ = false;`** — do
     **NOT** clear `sampleBuffer_` (drop the `= {}` reset at line 130). Keep it
     sticky so encoders 2..N still attach the end sample.

4. **`attachTimestampSamples`** (render encoders, lines 139–143): mirror step 3
   —`setStartOfVertexSampleIndex(firstEncoder_ ? startSampleIndex_ : CounterDontSample)`,
   `setEndOfFragmentSampleIndex(endSampleIndex_)`, then
   `firstEncoder_ = false;` (drop the `= {}` reset at line 143). The shared
   global means a stage that mixes compute + render encoders spans correctly
   (whichever encoder type runs first claims the start boundary).

5. **`writeTimestamp(END)`** (line 793): now **clear** the sticky state —
   `g_nextComputeTimestampAttachment = {};` — in addition to `pair->hasEnd_ =
   true;`. This stops tagging once the stage ends, so encoders created after
   END (next-stage gap, or before the next START) stay untracked, exactly as
   today.

`readTimestampPairMs` is unchanged: index 0 = first-encoder start, index 1 =
last-encoder end.

## Affected files

- `engine/render/src/metal/metal_render_impl.cpp` — struct field +
  `writeTimestamp` (START + END) + `createComputeEncoder` +
  `attachTimestampSamples`. No other file.

## Acceptance criteria

- On macOS/Metal, `IRPerfGrid --auto-profile 200 --zoom 8 --subdivision-mode
  full --base-subdivisions 1` reports per-stage GPU times whose **sum is within
  a sane fraction of the measured frame time** — no ~100× undercount on
  multi-encoder stages (`voxelStage1`). Confirm the multi-encoder stage's time
  jumps from <0.1 ms to a realistic multi-ms value.
- **No Metal validation-layer errors** (run with the Metal validation layer
  enabled — this is the primary risk gate; see Gotchas).
- OpenGL behavior unchanged — no OpenGL file edited; spot-check on Linux via
  the existing render-verify / perf harness if convenient, but the
  no-edit guarantee is the contract.

## Gotchas

- **On-device validation is the real acceptance gate, and it must run first.**
  The one unknown is whether Metal's validation layer accepts **multiple
  encoders in one command buffer writing the same 2-sample CounterSampleBuffer
  at index 1**. The approach is committed to the shared-buffer overwrite (it
  matches the present()/wait, 1-pair-in-flight model and needs no buffer
  resize). **If — and only if — the validation layer rejects the shared-buffer
  multi-write**, do not silently swap approaches: escalate `fleet:design-blocked`
  with the validation error. The likely architect-approved resolution is to
  size the buffer to `2 × maxEncodersPerStage` (or grow on demand), sample each
  encoder into its own slot pair, and have `readTimestampPairMs` return
  `max(ends) − min(starts)`; flag that as the contingency, don't pre-build it.
- `CounterDontSample` (= `(uint64_t)-1`, `MTLCounters.hpp:139`) is the correct
  sentinel for "don't sample this boundary." A non-first encoder samples **only**
  the end boundary (start = `CounterDontSample`, end = 1).
- Single-encoder stages must stay identical to today: the first (only) encoder
  claims both boundaries; END clears the sticky state. Verify a known
  single-encoder stage's reported time is unchanged.
- Mixed compute+render stages share the one global `firstEncoder_`, so the
  first-executed encoder of *either* type claims the start. Don't add a second
  per-type flag.
- Zero-encoder window between START and END (skipped/CPU-only stage): index 0
  never written → `readTimestampPairMs` already returns false on
  `CounterErrorValue`. No new handling needed.
- This is a render-pipeline + Metal-API change requiring on-device judgment —
  opus class. Build with the macOS preset (`fleet-build --target IRPerfGrid` on
  the macos-debug preset) and run the exact `--auto-profile` invocation above;
  the bug is invisible on a single-encoder demo.
