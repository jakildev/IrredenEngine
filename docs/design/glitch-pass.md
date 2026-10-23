# Period-quantised glitch passes

A tempo-locked scene wants its picture to tear, freeze and flash on a musical
division, not on frame time. The glitch passes read a composed image plus one
world-scoped singleton, `C_GlitchState`, which any creation drives from C++
(`IRPrefab::Glitch::setState`) or Lua (`IRRender.setGlitch{...}`). The
canvas-space pass also reads which entities carry `C_GlitchSubject`, so a
glitch can belong to an object (see [Subjects](#subjects)).

## Insertion points

| Pass | Slot | Reads | Status |
|---|---|---|---|
| `FRAMEBUFFER_GLITCH` | RENDER, after every main-framebuffer producer (`TRIXEL_TO_FRAMEBUFFER`, `VIEWPORT_TO_FRAMEBUFFER`, framebuffer debug overlays), immediately before `FRAMEBUFFER_TO_SCREEN` | main framebuffer colour + depth, one history image | shipped |
| `TRIXEL_GLITCH` | RENDER, after every main/per-axis colour producer (`LIGHTING_TO_TRIXEL`, `FOG_TO_TRIXEL` when registered), immediately before `TRIXEL_TO_FRAMEBUFFER` | the main and active per-axis canvases' colour, distance and entity-id planes, tick-start history | shipped |
| World-space jumps | none | — | creation-authored whole-voxel `TRANSFORM_TRANSLATION` modifiers from Lua, keyed off the same `IRRender.glitch().tick` |

## Clock

Every pass runs on the engine's fixed-step clock,
`timeSeconds = IRTime::tick() / IRConstants::kFPS`, never wall-clock time.

- `tick = floor((timeSeconds - phaseSeconds) / periodSeconds)`, signed 64-bit.
- `holdTick` uses `periodSeconds / holdDivisor`: the hold history re-samples
  `holdDivisor` times per period while bands, cells, splits and bars re-roll
  once per period.
- `phaseSeconds` is an absolute offset on the engine clock. A creation locks
  to an event by reading `IRRender.glitch().timeSeconds` when the event fires
  and writing it back as `phase`; envelope shaping (attack on the tick, decay
  to the next) stays in Lua, which writes `intensity` every frame.
- Between ticks no random choice moves. The only in-period variation is the
  bar envelope (1 at a tick, falling toward 0 before the next) and whatever
  intensity Lua writes.

The CPU computes both ticks once per frame and uploads each as two `uint32`
words. No shader divides floating-point time, so the backends cannot disagree
at a tick boundary.

## Shared ABI

`ir_glitch_common.glsl` / `ir_glitch_common.metal` are macro-free twins; their
CPU mirror is `engine/prefabs/irreden/render/glitch_math.hpp`, and every pass
consumes them instead of re-deriving the math.

- Seed, effect id, lane id and hash output are `uint32`; a tick is
  `{low32, high32}` of its two's-complement value.
- `glitchHash(seed, tick, effect, lane) = hash3(hash3(seed, low32, high32), effect, lane)`
  with `ir_iso_common`'s `hash3`. Multiplies wrap modulo 2^32, shifts are
  logical, signed overflow never reaches the hash.
- Effect ids and lane bases are fixed constants mirrored in all three files.
  `GlitchMathTest.ShaderTwinsCarryTheCpuConstants` reads both shader twins;
  fixed hash vectors pin the arithmetic.

Every choice that is the same for the whole frame (band rows, tear offsets,
cell size, split distance, bar rows and colours, the depth mode, offset and
probe start) is evaluated once on the CPU through this ABI
(`IRPrefab::Glitch::frameTables`) and uploaded with the frame data. The
fragment does integer compares against those tables; only per-texel choices
hash on the GPU (which cell moves, through `glitchCellLane`). The depth
interval's centre reads the GPU composite, so the probe scan runs in the
vertex stage and reaches the fragment as a flat varying.

## Effects and composition

Effects compose independently; each weight in [0, 1] scales its own effect
and `intensity` scales band counts, displacement and bar brightness together.
For one destination texel, in order:

1. **Tear** shifts the source x inside hashed horizontal bands.
2. **Cells** move whole cells (4..16 trixels) to a hashed neighbour inside
   bands of cell rows, never per pixel.
3. **Depth** selects a composite-depth interval: centred on the first
   non-background depth among a 4x4 probe grid scanned from a hashed start,
   with a hashed half-width. Each period tick chooses exactly one mode:
   `DEPTH_TEAR` adds its own x shift to pixels in the interval;
   `DEPTH_HOLD` blends them toward the history. Selection is by the
   destination texel's depth, so the objects in range shear or freeze while
   the rest of the frame holds.
4. **Hold** substitutes the history image inside hashed bands.
5. **Split** reads red and blue at opposite whole-trixel x offsets.
6. **Bars** add 1–3 saturated rows from a fixed palette.

**Medium rule:** every spatial offset is a whole number of trixels at the
current zoom (`trixelStepForZoom`); source coordinates clamp at the
framebuffer edge. A sub-trixel offset reads as a different medium.

## Resources and publication

`FRAMEBUFFER_GLITCH` owns exactly one image: an RGBA8 history texture sized to
the main framebuffer's resolution-plus-buffer, allocated on first enabled use,
recreated with the framebuffer, and owned through the rendering resource
manager. It is refreshed with `RenderDevice::copyTexture2D` from the
*unglitched* composite on a hold-tick change (and on first enable or resize),
and only when hold or depth reads it. Feeding glitched output back would
accumulate artifacts.

There is no output attachment. On an enabled frame the pass binds and clears
the default screen target itself and draws the glitched framebuffer with the
blit's placement (`framebuffer_screen_math.hpp`), then stamps the RENDER event
tick. `IRPrefab::Glitch::screenCompositedForCurrentRenderTick()` is true only
for that tick, and `FRAMEBUFFER_TO_SCREEN` then skips its bind, clear and
draw. The target is cleared and drawn exactly once per frame.

An enabled frame runs `encodeFrame`, which issues its GPU work through a
`GlitchEncoder`: `recreateHistory` on first use or on a framebuffer resize
(after withdrawing the publication), `copyHistory` on a hold-tick change and
after an off tick, then one `bindAndClearScreen` and one `drawScreenBase`. The
pass publishes only after that draw. `tick` resolves the camera and hands
`encodeFrame` a `GlitchGpuEncoder`, which makes the device and resource calls.
`FRAMEBUFFER_TO_SCREEN` has the same shape: `beginFrame` and `encodeBlit`
decide and count, and issue their work through a `ScreenBlitEncoder`
(`beginScreen`, then one `drawFramebuffer` per framebuffer) that
`ScreenBlitGpuEncoder` encodes.
`test/render/prefab_system_probe_test.cpp` runs both passes against recording
encoders, with a recording `RenderDevice` catching any call made outside them,
and asserts the whole schedule of each RENDER tick, enabled and off, without a
GPU.

Off (no singleton, zero intensity, all weights zero) does no GPU work: no copy,
no draw, no timing scope. The live `framebufferGlitch` row is written to zero
and the ordinary blit runs unchanged. `IRShapeDebug --glitch-test standard-off`
proves the off path byte-identical against the standing references.

## Cost

The pass draws the default screen target, so it shades the game-resolution
pixel count times `outputScale²`: a 1280x720 game on a 2x HiDPI target is
2560x1440 fragments. Its budget is therefore stated at 1x output (one output
pixel per game pixel), as the GPU cost it adds over the base composite it
replaces:

    UCB95(framebufferGlitch per-run means, --glitch-test all)
      - mean(fbToScreen per-run means, --glitch-test off)  <  0.30 ms

On a 2x host, `configs/perf/glitch-1x.lua` (a 640x360-point window against
the 1280x720 game resolution) gives a 1280x720 target at output scale 1; a
1x host gets it from the default config. Executor: five quiet runs each of
`fleet-run IRShapeDebug --glitch-test <all|off> --auto-profile 300
--config-preset configs/perf/glitch-1x.lua`, read from the GPU stage table
in `save_files/profile_report.txt`.

Measured on an Apple M4 Max (macOS Metal, 300 profiled frames per run, runs
interleaved all/off):

| Output | `framebufferGlitch`, all effects | `fbToScreen`, pass off | Added |
|---|---|---|---|
| 1x, 1280x720 | mean 0.335 ms, UCB 0.341 | 0.052 ms | 0.284 ms, UCB 0.289 |
| 2x, 2560x1440 | mean 1.085 ms, UCB 1.109 | 0.124 ms | 0.961 ms, UCB 0.985 |

Four times the pixels cost about 3.2 times as much: roughly 0.08 ms is fixed
per enabled frame (encoder boundaries, the clear, the history copy) and the
rest scales with output pixels. CPU cost per call is about 0.025 ms.

GPU timing gotcha: the pass's vertex stage reads the composite (texel size
and the depth probes). A Metal render pass whose vertex stage reads no
texture can start before the composite finishes, and its timestamp pair then
includes that wait; the ordinary blit avoids it the same way.

## GUI

A GUI composited *into* the main framebuffer (a pixelated GUI canvas,
`IRPrefab::GuiCanvas::compositesOnScreen()` false) is glitched with the
world. Sprites and the on-screen GUI composite (`GUI_CANVAS_TO_SCREEN`) draw
after the pass and stay clean. This is deliberate and not special-cased.

## Subjects

`TRIXEL_GLITCH` glitches the trixels of the entities that carry
`C_GlitchSubject`, on the main canvas, before the composite. It shares the
clock: it reads `periodSeconds`, `phaseSeconds` and `seed` from
`C_GlitchState` (the off default when there is no singleton) and keys every
choice on the same period tick `FRAMEBUFFER_GLITCH` uses. Framebuffer
`intensity`, its effect weights and `holdDivisor` neither gate nor retime
subjects.

### The subject contract

| Field | Lua key | Meaning |
|---|---|---|
| `weight_` | `weight` | [0, 1], default 1; scales every effect weight below |
| `tearWeight_` | `tear` | whole-trixel x shifts of hashed row bands; a displaced read is accepted only from the subject's own trixels |
| `holdWeight_` | `hold` | hashed row bands re-show the tick-start history |
| `splitWeight_` | `split` | red and blue read whole trixels to either side, from the subject's own trixels only |
| `echoWeight_` | `echo` | two offset copies of the subject's one-trixel silhouette edge, tinted |
| `dissolveWeight_` | `dissolve` | hashed 1-3 trixel cells of the subject's own texels drop out (transparent colour) |
| `spill_` | `spill` | integer [0, 4]: Chebyshev radius in canvas trixels around the silhouette that the subject's glitch reaches |
| `depthEnabled_`, `depthNear_`, `depthFar_` | `depthNear`, `depthFar` | optional inclusive band of decoded single-canvas iso-depth (`decodeDepthSingle`, smaller is nearer) that confines tear and hold |

A subject is active when `weight > 0` and some effect weight is positive.
Effect weights are independent and are not normalized. Every spatial offset is
a whole canvas trixel and every colour operation is 8-bit integer arithmetic,
so the two backends and the CPU twin agree bit for bit.

The id a trixel carries is the entity that stamped it: the voxel set (child
sets stamp their own ids) or the shape. A multi-set object marks each set.

**Lua.** `IRRender.setGlitchSubject(entity, table)` replaces the whole
configuration: omitted keys take the defaults above. An unknown key, a dead
entity, a non-finite or out-of-range weight, a non-integral or out-of-range
`spill`, a lone depth bound or `depthNear > depthFar` raises before anything
changes. `IRRender.clearGlitchSubject(entity)` removes the mark;
`IRRender.glitchSubject(entity)` returns a fresh table, or `nil` when the
entity is unmarked. Entities are a `LuaEntity` or an integer id. Adding or
removing the mark is a structural change, so call these outside a system tick
or through `IREntity.deferredCall`; replacing an existing mark is not.

### Spill and interference

For each canvas texel the kernel finds the two nearest distinct subjects whose
spill reaches it: Chebyshev distance from a subject silhouette texel, at most
that subject's `spill`, ties broken by ascending entity id. A subject's own
texel is at distance zero. With one influence the texel takes that subject's
effects. With two, the pair (ordered by id) hashes
`(seed, tick, low and high words of both ids)` once per tick and picks one
interference mode: `ROWS` (each canvas row takes one subject's effects),
`MIX` (the two blend half and half) or `CROSS` (each subject's tear and split
read the other subject's trixels; the results blend). Proximity therefore
changes the glitch only where both spill bands overlap, and a texel never
weighs more than two subjects.

Every id is decoded through the carrier mask (`decodeEntityId`) before lookup
or equality, so priority, fog and analytic-surface carrier bits never split
one entity in two.

### Hold and history

The main canvas owns history planes of colour and entity id. They are copied
from the live planes when an active subject reads history (`hold > 0`) and the
snapshot is missing, stale after a frame with no hold subject, or from an
earlier period tick. Hold therefore freezes a subject to its pose at the start
of the current tick: inside a hold band a texel shows history colour where the
history id is the subject, or where the subject now stands on texels that were
empty at the tick start. It never shows history whose id was another entity. A
subject that does not move within its tick shows no hold. Showing a ghost of
the previous tick would need a second history pair; it is not built.

At an eligible cardinal, lighting also publishes a source-lit colour plane.
The glitch kernel reads that plane for every influenced texel and for hold
history, while uninfluenced copy-through texels retain the ordinary live
albedo. It writes a one-channel effect-reach mask beside its colour output.
The gather skips destination relighting only where that mask is set, so the
glitched source sample is lit exactly once and unaffected cardinal surfaces
keep their continuous destination-surface lighting.

During smooth yaw, each axis canvas has its own history pair. Its snapshot is
valid only while the store size and nearest cardinal match. A store-origin
move inside that cardinal reads history at `cell - currentOrigin +
snapshotOrigin`, so camera-anchor motion does not move the held pose. A
cardinal rebracket invalidates and retakes history even inside one period
tick.

### Resources and cadence

The main canvas (`C_TriangleCanvasTextures`) owns three ordinary glitch images,
created by the pass on its first enabled frame and destroyed by a canvas
resize, so they return at the new size, and by `onDestroy`: an RGBA8 scratch
colour destination, the RGBA8 history colour and the RG32UI history id. An
active eligible cardinal lazily adds one RGBA8 source-lit plane and one RGBA8
effect-reach mask. Merely registering the pass, or running with no active
subject, allocates neither presentation image. Once created, their handles
survive ordinary glitch image creation and ping-pong swaps until canvas resize
or destruction.

The active per-axis store owns the corresponding images in each of its three
`AxisTextures`: scratch is created on the first enabled rotating frame, while
history colour/id are created only for an active hold. They follow the store
through park/unpark without re-creation and are destroyed with it on resize or
release. The store also owns one three-region tile-flag buffer, tile-list
buffer and indirect-args buffer. Those buffers are created with scratch,
follow the same lifetime, and contribute to the reported lazy-resource bytes.
A cardinal frame allocates none of them.

An enabled frame uploads the subject buffer, takes the two history copies when
due, runs one 16x16 compute dispatch over the whole canvas, then swaps the
scratch plane in as the live colour after the barrier. The kernel reads the
live planes and history and writes every scratch texel, so it never reads what
it writes. Distance and entity id are never written or swapped. Effects never
accumulate frame over frame: each frame starts from the producers' fresh
colour, and history is always pre-glitch.

The subject buffer is one SSBO of `uvec4` words: a header (seed, tick words,
count; table mask, table base, record base, max spill), an open-addressed id
-> slot table of power-of-two capacity at or below half load (the null id is
the empty sentinel, linear probing), and five words per subject holding every
per-tick choice the CPU evaluated (`trixel_glitch_math.hpp`). Subjects are
sorted by id, so a slot is the rank of its id and upload order never depends
on archetype iteration. A two-word tail carries the per-axis store origin,
history origin, face step and affine depth map. The buffer grows by doubling
and is reused.

Each 16x16 workgroup stages the subject slots of a 26x26 tile (a five-texel
halo: spill reaches four, and the echo edge test reads one beyond an echo
offset of up to four) in threadgroup memory. A tile with no subject copies
its texels straight through.

**Bindings.** Images 0-2 are the live colour, distance and entity-id planes
(read), 3-4 the history colour and id (read), 5 the scratch colour (write), 6
the source-lit colour (read), and 7 the effect-reach mask (write). Ordinary and
non-cardinal routes bind existing dummy images at 6-7 and retain their prior
colour behavior.
The subject buffer borrows storage slot 8 (`kBufferIndex_TrixelGlitchSubjects`,
the voxel active mask's slot) and the pass rebinds `VoxelActiveMaskBuffer`
there after its dispatch, as `FOG_TO_TRIXEL`'s per-axis path does. A
storage-table slot matters on Metal, where the uniform and storage tables are
flushed into one buffer table at each dispatch and a stale storage entry
would shadow a uniform binding at the same index. The kernel is registered at
16x16 in `threadgroupSizeForFunctionName`.

**Off.** With no active subject the pass uploads, copies, dispatches, swaps
and allocates nothing and writes the live `trixelGlitch` timing row to zero.
`IRShapeDebug --glitch-subject-test standard-off` proves it byte-identical
against the standing references.

### Smooth-yaw per-axis route

When the main canvas's per-axis set is allocated, the same enabled tick also
runs a three-pass subject-tile worklist for each X/Y/Z axis canvas:

1. Mark scans compacted occupied cells. A subject cell marks every 16x16 store
   tile intersecting its `2 * spill` reach; the first marker appends the tile
   and increments that axis's indirect group count.
2. Resolve runs one 16x16 workgroup per marked tile. It stages subject slot and
   spill for a 36x36 window (ten-cell halo), then assigns the window's distinct
   subjects local ranks in ascending slot order. A five-level threadgroup
   slot-mask pyramid records which ranks lie within store Chebyshev radii
   0, 2, 4, 6 and 8. Per-cell level and spill masks recover the unchanged
   closest-two rule without scanning the window, and occupied cells write to
   scratch. Up to 32 distinct subjects in a window take this path; a larger
   window, a frame with more than 1024 subjects, or
   `IR_TRIXEL_GLITCH_PER_AXIS_SCAN=1` takes the exact step-2 lattice scan.
3. Write-back copies scratch to the live colour for those occupied cells and
   clears the tile flag. The fixture oracle retains its own pre-pass colour
   readback because the one-way write-back does not swap the two planes.

The live colour, distance and entity-id handles never swap. The pass clears
the three axes' args with one fill, issues nine indirect dispatches, and
restores borrowed slots 8 and 25-29 to their owners. Resolve and write-back do
not bind their indirect-args buffer for write. `IR_TRIXEL_GLITCH_PER_AXIS_DISABLE=1`
skips this route for defect isolation. Its copies, fill, and dispatches alone occupy the
`trixelGlitchPerAxis` timing row; frames without the route write zero.

All three axes use the nearest-cardinal store frame. One logical trixel of a
spatial effect is one voxel face, or two store cells
(`kPerAxisFaceStep = 2`). Tear/split x offsets, echo offsets and edge steps,
band height, and dissolve cells use that step. Spill scans every checkerboard
offset `(dx + dy) mod 2 = 0` within twice its radius—including odd/odd face
neighbours—and measures distance as `ceil(max(abs(dx), abs(dy)) / 2)`.
Band and dissolve hashes use `cell - storeOrigin`, including floor division
for negative coordinates, so an anchor move does not re-roll choices inside a
tick. A cardinal rebracket intentionally re-keys them.

Spill and echo appear only at occupied compacted cells; reaching an empty
store cell draws nothing. Dissolve writes transparent colour and the scatter
reveals the framebuffer behind it. Per-axis depth is one value per face and
is mapped to main-canvas units by `mainDepthAtFaceOrigin = subdivisions *
perAxisDepth`; depth-band edges therefore resolve per face rather than per
sub-face trixel.

### Limits

- **Main canvas only.** Detached, viewport, entity and GUI canvases are
  untouched.
- **Per-axis overflow lane.** It carries no entity id, so only compacted cell
  faces participate during smooth yaw. Convex subjects in empty sky remain on
  the cell path; overflow count is reported by the fixture.
- **Shape probe and shape lighting gather modes** relight a glitched texel
  from the shape geometry sample at its destination, so a moved shape texel
  survives but takes its new position's lighting.

### Cost

The main pass shades canvas texels. The per-axis mark performs one subject
lookup per compacted occupied cell. Resolve performs a fixed amount of
threadgroup work per marked tile while at most 32 distinct subjects share its
window; only the explicit or overflow fallback scans the step-2 lattice. Its
cost therefore follows main-canvas size and the rotating subjects' reached
tiles rather than every occupied face in the scene. Budget: eight subjects,
every effect at weight 1, `spill = 4`, at the committed 1x take preset:

    UCB95(trixelGlitch per-run means, --glitch-subject-test all)  <  0.40 ms
    UCB95(trixelGlitchPerAxis per-run means, yaw 22.5)            <  0.40 ms

Executor: five quiet runs of `fleet-run IRShapeDebug --glitch-subject-test all
--auto-profile 300 --config-preset configs/perf/glitch-1x.lua`, read from the
GPU stage table in `save_files/profile_report.txt`, interleaved with
`--glitch-subject-test off` runs whose glitch rows collect no sample. The
per-axis arm adds `--glitch-subject-yaw 22.5` to both modes and records its
nine dispatches, one args fill, marked tiles per axis, hold-copy cadence,
canvas sizes and lazy resource bytes.

Measured on an Apple M4 Max (macOS Metal, 1280x720 output at scale 1, main
canvas 642x722, one dispatch per frame and two history copies per period
tick): per-run means 0.240, 0.227, 0.231, 0.229 and 0.232 ms, mean 0.232 ms,
spread 0.013 ms, UCB95 0.237 ms. CPU cost per frame is about 0.017 ms. The
per-axis arm uses 1284x1364 stores and measures 0.397, 0.270, 0.289, 0.301
and 0.294 ms, mean 0.310 ms and UCB95 0.358 ms; the forced exact-scan path
measures 0.399 ms. Its all-effects capture marks 20, 18 and 18 tiles on the
X, Y and Z stores and allocates 84,234,240 lazy-resource bytes. The fixture's
canvas probes run only under `--auto-screenshot`; their readbacks stall the
queue and must not ride a profiled run.

### Choreography

Both passes read one period tick, so a creation alternates them by tick
parity: mark the subject on tick N with the framebuffer state off, then clear
the subject (or zero its weight) and enable a framebuffer tear on tick N + 1.
A world-space jump is the creation's own whole-voxel `TRANSFORM_TRANSLATION`
modifier for the same tick; this pass draws the glitch wherever the entity is.

## Evidence fixture

`IRShapeDebug --glitch-test <mode>` captures one shot per effect at a fixed
seed and phase, plus a cube stepping one voxel per tick so held history is
visibly stale. `[glitch-probe]` lines log the ticks, trixel step, output
scale and viewport, copy, draw and ordinary-blit counts and the tear
offsets. Rows live in
`creations/demos/shape_debug/test/references/manifest.json`.

On the capture frame the probe reads the screen target back and compares
each pixel with the unglitched composite texel the ordinary blit would have
drawn there (the inverse of `framebufferScreenModel`'s placement). That
texel is what `glitch_off` shows at the same pixel, so the `screen ...
changed=` count is the diff against the control, and an `off` capture must
report zero. For the depth modes it also reads the full depth attachment,
reselects the interval through the CPU twin (`selectDepthInterval`), and
splits the pixels into in-range and out-of-range, changed and unchanged. The
depth effect passes when in-range changed is positive, out-of-range changed
is zero and out-of-range unchanged is positive.

Every capturing mode also spawns a one-colour sentinel sprite. Sprites draw
after the pass, so the sentinel's pixel set (`sentinelPx`, `sentinelRect`,
`sentinelHash`) must be identical in every capture. A pass that drew or
cleared after the sprites would erase it.

`IRShapeDebug --glitch-subject-test <mode>` is the subject pass's fixture: two
striped voxel subjects in empty sky at a fixed seed and phase, one shot per
effect plus far/near spill controls and a two-shot choreography. On each
capture frame `[glitch-subject-probe]` reads the canvas back (glitched colour,
the pre-glitch input left in the scratch plane, ids, distances, history),
replays the kernel through the CPU twin and reports `cpuMismatch` (zero on
every mode) and the per-effect counts. The spill modes pose subject B far and
near on two earlier frames of the same tick, a pure horizontal canvas shift at
unchanged depth, and the oracle checks that the far and near effect masks,
B's part shifted by its measured displacement, differ only inside the near
pair-overlap band.

Adding `--glitch-subject-yaw 22.5` poses both screenshot and profile runs
inside one cardinal bracket. The yaw rows read all three axis stores, replay
the step-2 CPU twin with uploaded store/history origins and depth map, census
the subject's expected X/Y/Z face cells, and report mismatches, changed
subject cells, odd displacements, foreign reads, history, depth, pair,
dispatch/copy and overflow evidence. The yaw `off` row is the visual control;
the cardinal rows and labels are unchanged.
