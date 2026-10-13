# Fog ceiling retention — WIP evidence

Two-ref capture for PR #4381, taken on 2026-10-13 in the same macOS session.
The original ceiling frames below predate the corrected signed overflow
encoding. The later validation section records fresh controls and the curved
fixture correction. Full image identity and performance remain unverified.
No reference baselines have been replaced.

- Before: `90503255f84bf5046b04fc9a31788bc17c2447ed` (master at integration,
  including the merged shadow receiver work).
- After: `4467fb517ce9b4ca350a8fc048d412f90b0e4cc6` (committed WIP code).
- Runtime / author: Codex, GPT-6.
- Host: macOS, Apple M4 Max, 32 GPU cores, Metal, `macos-debug`.
- Both builds: `fleet-build --detach --target IRFogDemo`, with a successful
  terminal `fleet-jobs wait`; the before build also included IRLightingEmissive.
- Build waits: before `20261013T191626Z-build-fbb25a succeeded exit=0`;
  after `20261013T192322Z-build-490b92 succeeded exit=0`.
- Both captures: `fleet-run IRFogDemo --ceiling-treatment --reveal-treatment --auto-screenshot 10`.
- Render viewport: 1280 × 720; PNG framebuffer: 2560 × 1440.
- Scene: two FIELD voxel columns and one governed BODY column, each 4 × 4 × 16;
  one 60 × 60 × 3 FIELD ground slab. Four scene solids, 11,568 authored voxels.
  One fog source at (0,0), radius 14, softness 2, observer Z 4;
  ceiling height 5 / fade 4; treatment density .5, cap tone .5, phase 0.
- Lighting: one vertical sun (0,0,-1), sun shadows enabled, no authored local lights.
- Camera: yaw 0 and .35 radians, requested zoom 9 clamped to effective (8,8),
  subdivisions 8; camera (0,0); effective iso (0,0) and (-1.4001657,.24756145).
  Culling toggle 0, no object rotation, no occlusion-refine toggle.
- Two full frames per arm, paired by auto-screenshot label. Both runs terminate
  `ir-run: RESULT=CLEAN exe=IRFogDemo exit=0`.
- Clip skipped: this demo has no auto-record wiring.

Before, both FIELD columns retain their full silhouette and paint the hidden
upper section magenta. The old probe reads 272 carrier texels per column and
40/40 top texels painted; the BODY twin stays full-height and unpainted.

After, the hidden upper FIELD sections are absent and the partial band has
voxel-shaped gaps and caps; the blue BODY twin stays full-height. Full frames
were inspected at both yaws. Carrier probes, rather than colour alone, check
that the solid core has no missing faces.

The pinned before ref is intentionally recorded: master moved during this
iteration. These WIP pairs are not a substitute for retaking all required
current-master pairs after the design blocker is resolved.

Final head carrier readings:

```text
column=centre texels=157 top=8/8 aboveCut=0 capFaces=37 core=16 hollow=0
column=rim texels=150 top=8/8 aboveCut=0 capFaces=50 core=16 hollow=0
roi=band phase=0 changed=0 outsideBand=0
body=twin texels=272 aboveCeiling=192 painted=0 partial=0 ratioMin=1 ratioMax=1
route=per-axis column=centre capFaces=26 topCaps=17 core=24 hollow=0
route=per-axis column=rim capFaces=35 topCaps=18 core=24 hollow=0
```

## Corrected-head validation

Codex (GPT-6) captured these controls in one macOS / Apple M4 Max / Metal
session on 2026-10-13. The amended code is `63732ea126a859105fe5a00178ace05f2509fcfe`;
the before control is PR-base master `759145786cf69de39afeb4457c7a5aedfe5bbee7`.
The original four ceiling-treatment PNGs are byte-identical to fresh captures
at these two refs, checked with `cmp`. Explicit `--ceiling-phase=0` is also
byte-identical to omitting the phase argument, at both yaws.

The new column-top pairs use:

```text
fleet-run IRFogDemo --occlusion=column-top --fog-debug-color --auto-screenshot 10
```

Both runs exit CLEAN. The scene has two 6×6×9 FIELD plateaus and a 60×60×3
FIELD ground slab (three solids, 11,448 authored voxels), two gated fog
sources, one vertical sun with shadows enabled, and no local lights.
The foot source at (-8,-8,4) has radius 20 and hard ceiling 4; the top source
at (0,0,-5) has radius 6 and no ceiling. Both have softness 1. Treatment and
phase are off. Camera (0,0), zoom 8, yaw 0 / .35, subdivisions 8, no object
rotation or occlusion refine; viewport 1280×720, full PNG 2560×1440. All four
frames were inspected. The control's painted upper section is absent after
the change; the main plateau stands.

```text
before: bandTexels=120 bandOracleHidden=0 bandPainted=0
        controlTexels=120 controlPainted=120
after:  bandTexels=108 bandOracleHidden=0 bandPainted=0
        controlTexels=0 controlCap=72 controlCore=48 controlHollow=0
```

The curved after-only frame uses
`fleet-run IRFogDemo --ceiling-shadow-curved --auto-screenshot 10` and exits
CLEAN. It has two radius-2, height-10 cylinders (one FIELD, one BODY),
4×4×16 and 4×4×9 FIELD columns, and a 60×96×3 FIELD slab (five solids;
17,680 authored voxels plus two analytic shapes). Camera (0,0), yaw 0,
requested zoom 9 / effective zoom 8, density one; the sun direction is the
normalized (0.35,0.85,-0.4), with shadows enabled and no local lights.
The one radius-30 fog source is at (0,0,4), softness 2, ceiling 5 / fade 4;
treatment and phase are off. Viewport and PNG dimensions match the paired
frames. The full frame was inspected; it supplements the numeric oracle.

```text
centre=100:20.996 body=twin=108:22.845 sunTexel=0.463
curved gap=1.849 min=1.149 receiver=39.000/30.841
```

The old floor ended the BODY lane at 20.996. Restoring that floor fails the
new receiver-clipping guard. Bypassing the FIELD caster predicate with the
extended floor gives centre/BODY reach 22.845 and fails the unchanged gap
threshold at 0.000. Both mutations were restored; the final fixture passes.

Build evidence: head `20261013T211924Z-build-4b343c` and base
`20261013T212242Z-build-1cc687` have terminal `succeeded exit=0` waits.
Clip skipped: the demo has no auto-record wiring. These captures are evidence,
not a reference re-blessing or a claim that the full render sweep passed.
