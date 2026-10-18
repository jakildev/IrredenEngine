# Period-quantised glitch passes

A tempo-locked scene wants its picture to tear, freeze and flash on a musical
division, not on frame time. The glitch passes take no scene data: they read a
composed image plus one world-scoped singleton, `C_GlitchState`, which any
creation drives from C++ (`IRPrefab::Glitch::setState`) or Lua
(`IRRender.setGlitch{...}`).

## Insertion points

| Pass | Slot | Reads | Status |
|---|---|---|---|
| `FRAMEBUFFER_GLITCH` | RENDER, after every main-framebuffer producer (`TRIXEL_TO_FRAMEBUFFER`, `VIEWPORT_TO_FRAMEBUFFER`, framebuffer debug overlays), immediately before `FRAMEBUFFER_TO_SCREEN` | main framebuffer colour + depth, one history image | shipped |
| `TRIXEL_GLITCH` | the `TRIXEL_TO_TRIXEL` slot, per entity in canvas space | canvas colour, distance and entity-id textures | follow-up, consumes this contract |
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

Off (no singleton, zero intensity, all weights zero) does no GPU work: no copy,
no draw, no timing scope. The live `framebufferGlitch` row is written to zero
and the ordinary blit runs unchanged. `IRShapeDebug --glitch-test standard-off`
proves the off path byte-identical against the standing references.

## Cost

The pass shades every pixel of the screen target, so its GPU cost scales
with the output scale squared, not with the game resolution. Measured on an
Apple M4 Max (macOS Metal, 1280x720 game resolution on a 2560x1440 HiDPI
target, 300 profiled frames per run): the plain blit it replaces is about
0.12 ms; one effect alone is 0.31–0.53 ms; every effect at once, with a
history copy every frame, averaged 1.06 ms across five runs. CPU cost per
call is about 0.04 ms.

GPU timing gotcha: the pass's vertex stage reads the composite (texel size
and the depth probes). A Metal render pass whose vertex stage reads no
texture can start before the composite finishes, and its timestamp pair then
includes that wait; the ordinary blit avoids it the same way.

## GUI

A GUI composited *into* the main framebuffer (a pixelated GUI canvas,
`IRPrefab::GuiCanvas::compositesOnScreen()` false) is glitched with the
world. Sprites and the on-screen GUI composite (`GUI_CANVAS_TO_SCREEN`) draw
after the pass and stay clean. This is deliberate and not special-cased.

## Evidence fixture

`IRShapeDebug --glitch-test <mode>` captures one shot per effect at a fixed
seed and phase, plus a cube stepping one voxel per tick so held history is
visibly stale. `[glitch-probe]` lines log the ticks, trixel step, copy, draw
and ordinary-blit counts and the tear offsets. Rows live in
`creations/demos/shape_debug/test/references/manifest.json`.
