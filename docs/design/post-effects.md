# Framebuffer post effects

Bloom, blur and a colour grade over the composed main framebuffer, each
switched independently and all off by default. One world-scoped singleton,
`C_PostEffectsState`, holds every parameter; a creation drives it from C++
(`IRPrefab::PostEffects::setState`) or Lua (`IRRender.setPostEffects{...}`),
per frame if it wants to animate them. The pass is `FRAMEBUFFER_POST_EFFECTS`.

## Design decisions

Where the plain reading of "post effects on the main framebuffer" and the
tree disagree, and what the pass does about it:

- **The composite is never written in place.** The glitch pass snapshots its
  history from it, `video_capture_post_effects = false` records it, and
  fixtures read it back as their control. The pass draws into its own output
  image, the composite's twin texel for texel, and publishes that as the
  presented image, exactly as `FRAMEBUFFER_GLITCH` does.
- **The recorder and the auto-screenshot read different images.**
  `--auto-record` and a canvas screenshot read the presented main colour at
  the framebuffer's own resolution. `--auto-screenshot` reads the screen
  target, after the blit, the sprites and the on-screen GUI. Both show the
  effects, because the blit presents the same output the recorder reads, but
  a screenshot also holds what draws after the pass and a clip never does.
- **Twin shaders are a review property, not a checked one.**
  `scripts/shader_parity_audit.py` pairs files by name and never compares
  contents. The shaders here are written as twins (same helpers, same order,
  integer texel reads only), and only the Metal side has been run: the GLSL
  side is unverified until an OpenGL host captures the fixture.
- **One frame-data upload per frame, so no per-pass uniform.** A bloom chain
  and a separable blur are up to fourteen draws. Their one varying input, the
  blur axis, is a compile-time specialisation of a shared body, so a single
  upload serves every draw.
- **No LUT.** Exposure, lift/gamma/gain, saturation and contrast are all
  closed-form, so the CPU twin can check them to the last bit; a LUT needs a
  texture upload path and an asset contract this pass does not otherwise
  have. It can be added behind one more switch.
- **Enabling an effect from Lua does not raise when the pass is absent.** A
  script may set the state before the creation builds its RENDER pipeline, so
  the binding cannot tell "not registered" from "not registered yet". Like
  the glitch state, a state with no pass is inert.

## Insertion point and order

| Pass | Slot | Reads | Writes |
|---|---|---|---|
| `FRAMEBUFFER_POST_EFFECTS` | RENDER, after every main-framebuffer producer and after `FRAMEBUFFER_GLITCH` when registered, immediately before `FRAMEBUFFER_TO_SCREEN` | the presented main colour so far: the glitch output on a tick that pass drew one, else the composite | its own output image |

The order is fixed: **composite, then glitch, then post effects, then the
screen blit.** The glitch pass keeps its contract unchanged (whole-trixel
moves of the 8-bit composite, history taken from the unglitched composite),
and the lens-like effects come last, so a bloom spreads a glitch bar's light
and a grade colours the whole presented picture. A creation that registers
both passes puts the glitch first; the post pass reads whichever image is
presented when it runs.

`IRPrefab::PresentedFrame::mainColor()` names the image presented for the
main framebuffer on the current RENDER tick and which pass drew it: the post
output, else the glitch output, else the composite. `FRAMEBUFFER_TO_SCREEN`
places it with the blit's own placement, so the screen target is still
cleared and drawn once per frame, and `VideoManager` records it. The config
key `video_capture_post_effects = false` still records the raw composite.

Sprites and the on-screen GUI composite draw after the blit and are not
affected. A GUI composited into the main framebuffer is.

## State

All fields are plain data on `C_PostEffectsState`; `IRPrefab::PostEffects::validate`
is the one rule set and `setState` asserts it.

| Field | Lua key | Range | Default | Meaning |
|---|---|---|---|---|
| `bloomEnabled_` | `bloom` | boolean | false | switch |
| `bloomThreshold_` | `bloomThreshold` | [0, 1] | 0.7 | brightness (largest channel) above which a texel glows |
| `bloomKnee_` | `bloomKnee` | [0, 1] | 0.2 | width of the soft shoulder below the threshold |
| `bloomIntensity_` | `bloomIntensity` | [0, 8] | 1 | scales the glow added to the picture |
| `bloomScatter_` | `bloomScatter` | [0, 1] | 0.7 | how much of each wider level reaches the next finer one; larger is a wider glow |
| `bloomLevels_` | `bloomLevels` | integer [1, 6] | 5 | length of the downsample chain; each level halves the resolution |
| `bloomBlend_` | `bloomBlend` | `IRRender.BloomBlend.ADD` / `SCREEN` | `ADD` | `ADD` sums and clips; `SCREEN` is `1 - (1 - base)(1 - glow)` and never clips |
| `blurEnabled_` | `blur` | boolean | false | switch |
| `blurRadius_` | `blurRadius` | [0, 32] | 2 | Gaussian radius in framebuffer pixels (two standard deviations); 0 draws no blur |
| `gradeEnabled_` | `grade` | boolean | false | switch |
| `exposureStops_` | `exposure` | [-8, 8] | 0 | multiplies colour by `2^exposure` |
| `lift_` | `lift` | each channel [-1, 1] | 0 | raises (or lowers) the blacks, leaving white fixed |
| `gamma_` | `gamma` | each channel [0.1, 8] | 1 | mid-tone power, applied as `1 / gamma` |
| `gain_` | `gain` | each channel [0, 8] | 1 | per-channel multiply; a tint is a gain |
| `saturation_` | `saturation` | [0, 4] | 1 | 0 is greyscale (Rec. 709 luma) |
| `contrast_` | `contrast` | [0, 4] | 1 | scale about mid-grey 0.5 |

Parameters keep their values while their effect is switched off. The pass is
enabled when any switch is on.

**Lua.** `IRRender.setPostEffects{...}` is a partial update: omitted keys
keep their values, and every key and value is checked before any field
changes. An unknown key, a non-boolean switch, a non-finite or out-of-range
number, a non-integral `bloomLevels`, or a `bloomBlend` that is not a member
of `IRRender.BloomBlend` raises with the state untouched. `lift`, `gamma` and
`gain` take a number (all three channels) or a table of exactly three
channels, keyed `r`, `g`, `b` or indexed 1 to 3.
`IRRender.postEffects()` returns a fresh table of every key, with the three
colour fields as `{r, g, b}`; it reads the off defaults, and creates nothing,
when no state was ever set.

## Effects and composition

For one output texel, in order:

1. **Blur** replaces the picture with a separable Gaussian of it: a
   horizontal pass into one image, a vertical pass from that into a second.
   Taps sit on whole texels at offsets `-T..T`, `T = ceil(radius)`, with
   weights `exp(-o^2 / (2 s^2))`, `s = radius / 2`, normalised over the taps
   on the CPU. All four channels blur; reads clamp at the framebuffer edge.
2. **Bloom** adds a glow built from the (blurred, when blur is on) picture.
   - *Prefilter* into level 0 at half resolution: each source texel is scaled
     by `max(soft, b - threshold) / b`, where `b` is its largest channel and
     `soft = clamp(b - threshold + knee, 0, 2 knee)^2 / (4 knee)`, then the
     4x4 block around the destination is averaged with weights
     `(1, 3, 3, 1) / 8` per axis.
   - *Downsample* level `i - 1` into level `i` with the same 4x4 weights.
     Level `i` is `max(size >> (i + 1), 1)` per axis.
   - *Upsample* from the coarsest level back down:
     `up[i] = mix(down[i], tent(up[i + 1]), scatter)`. `tent` is a 2x
     bilinear upsample convolved with a 3-tap tent, which reduces to four
     whole-texel taps per axis with weights `(1, 5, 7, 3) / 16` or its mirror
     by destination parity.
   - The resolve adds `tent(up[0]) * intensity` to the picture, or screens
     it, per `bloomBlend`.
3. **Grade** runs last, on colour only:
   `c = c * 2^exposure`; `c = max(gain * c + lift * (1 - c), 0)^(1 / gamma)`;
   `c = mix(luma(c), c, saturation)`; `c = (c - 0.5) * contrast + 0.5`.

The result clamps to [0, 1]; alpha is the (blurred) picture's alpha.

Every texture read is a whole-texel fetch at a computed integer coordinate,
never a filtered sample, so the two backends and the CPU twin
(`post_effects_math.hpp`) read the same texels. The images are RGBA8, like
the composite, so each pass quantises to 8 bits between stages.

## Resources and publication

The pass owns, through the rendering resource manager, every image sized
from the main framebuffer's resolution-plus-buffer:

- the **output** framebuffer, the composite's twin, created on the first
  enabled frame;
- two full-size **blur** images, created the first frame blur draws;
- six **down** and five **up** bloom levels, created the first frame bloom
  draws.

All are RGBA8 with a depth attachment that only completes the target. A
framebuffer resize withdraws the publication and replaces whichever exist.
Switching an effect off keeps its images: an animated parameter crossing
zero must not reallocate.

An enabled frame runs `encodeFrame`, which issues its GPU work through a
`PostEffectsEncoder`: `recreateImages` when the size changed or an effect
needs images it does not have, one `uploadFrameData`, then one `drawPass` per
stage in the order above, ending with `RESOLVE` into the output. The pass
publishes only after that draw. `IRPrefab::PostEffects::outputPublishedForCurrentRenderTick()`
is true only for that tick. `test/render/prefab_system_probe_test.cpp` runs
the pass, the glitch pass and the blit against recording encoders and asserts
the whole schedule of a tick for each combination of effects, without a GPU;
`test/render/post_effects_test.cpp` pins the arithmetic, the frame plan and
the packing.

Frame data is one std140 block, `FrameDataPostEffects`, packed by
`packPostEffectsFrameData` from the state and uploaded once per enabled
frame; nothing is cached or compared between frames. The blur axis is the
only per-draw input and is compiled in
(`f_post_effects_blur_horizontal` / `_vertical` over
`ir_post_effects_blur_body`).

**Off** (no singleton, or every switch off) does no GPU work: no upload, no
draw, no timing scope. The live `framebufferPostEffects` row is written to
zero and the ordinary blit runs unchanged.
`IRShapeDebug --post-effects-test standard-off` proves the off path
byte-identical against the standing references.

## Cost

Every pass shades framebuffer texels, not screen pixels, so the cost does not
grow with the output scale; the blit that places the output costs what the
composite's blit did. An enabled frame is one render pass per draw, and each
pass has a fixed encoder cost, so the bloom chain's price is mostly its draw
count: fewer `bloomLevels` is the lever. The blur reads `2 * ceil(radius) + 1`
texels per pass per texel, so its price follows the radius.

Executor: `fleet-run IRShapeDebug --post-effects-test <mode> --auto-profile
300 --config-preset configs/perf/glitch-1x.lua`, read from the
`framebufferPostEffects` row of the GPU stage table in
`save_files/profile_report.txt`.

Measured on an Apple M4 Max (macOS Metal, 1280x720 output at scale 1, 1284x722
framebuffer, 300 profiled frames per run, three runs per mode interleaved, on
a host that was also building; two of the fifteen runs reported
`RESULT=CONTAMINATED` and are included):

| Mode | Draws | GPU per-run means (ms) | CPU (ms) |
|---|---|---|---|
| `off` | 0 | no sample | 0.004 |
| `grade` | 1 | 0.064, 0.066, 0.070 | 0.02 |
| `blur`, radius 6 | 3 | 0.415, 0.419, 0.390 | 0.05 |
| `bloom`, 5 levels | 10 | 0.697, 0.758, 0.664 | 0.13 |
| `all` (radius 2, 5 levels, grade) | 12 | 0.851, 0.871, 0.812 | 0.15 |

These are readings, not a budget: no gate enforces them. The OpenGL side is
unmeasured.

## Evidence fixture

`IRShapeDebug --post-effects-test <mode>` captures one shot per mode of the
standard scene: `off` (the control), `blur`, `bloom`, `grade` and `all`. On
the capture frame `[post-effects-probe]` reads the composite and the pass's
output back and replays the frame through the CPU twin:

- `blur`, `grade`: `cpuMaxDelta` is the largest channel difference between
  the output and the twin's result, in 8-bit steps. The twin quantises where
  the GPU does, so the bound is 1.
- `bloom` (additive): `darker` counts output channels below the composite
  (must be 0) and `brighter` those above (must be positive).
- every mode: `changed` counts texels that differ from the composite; `off`
  reports no output at all.

Rows live in `creations/demos/shape_debug/test/references/manifest.json`.
