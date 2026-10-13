# Fog ceiling retention — WIP evidence

Two-ref capture for PR #4381, taken on 2026-10-13 in the same macOS session.
These frames show the ceiling fixture; they do not certify the known-broken
signed overflow encoding, full image identity, curved-shadow acceptance or
performance. No reference baselines have been replaced.

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
