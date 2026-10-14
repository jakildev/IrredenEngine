# Fog ceiling retention — native Metal evidence

Two-ref captures for PR #4381, retaken in one macOS session on 2026-10-14
(local time) by Claude Fable 5.1 (fleet worker, pool-3) after the rebase onto
master, so both arms carry the snapped rotated-view scatter that moved the
yawed rows. The original pairs (Codex, GPT-6, 2026-10-13, before
`8a1bc317a` / after `50c685205`) are in this directory's history; the
cardinal retakes are byte-identical to them and the yawed retakes differ by
the scatter move on both arms.

- Before: `9d9cecc3f7a490b7735f667b16049d7f3382b07b`, the PR's merge base,
  built in a scratch worktree (`/private/tmp/pool3-4381-base/build`).
- After: `2ed29dcf59d88f726e65c2511074c1a1d60a7c02`, the shipped renderer
  with the no-ceiling compact declaring master's prefix mirror of
  `FogObserverData`, built in the pool-3 worktree.
- Host: macOS 26.5.2 arm64, Apple M4 Max, Metal, `macos-debug`, Debug with
  the repository's `-O3` setting.
- Captures use `fleet-run --build-dir <arm build> --timeout 60 IRFogDemo
  <scene-arguments> --auto-screenshot 10`; every run exits `RESULT=CLEAN`.
  Logs, probe lines, commands and capture hashes are in
  `verification/rebased/`; the earlier session's evidence stays under
  `verification/`.
- Every `-after` full frame is byte-identical to the committed
  `macos-debug` render-verify reference of the same view.
- Viewport 1280 × 720; full PNG framebuffer 2560 × 1440. Requested camera (0,0),
  yaw 0 and .35 radians, requested zoom 9 / effective zoom 8 (column-top
  requests 8), subdivisions 8. Effective iso position is (0,0) at yaw 0;
  at yaw .35 it is (-1.4001657,.24756145) for ceiling/shadow and
  (-2.428859,.42944288) for column-top in both arms. No object rotation, culling toggle or
  occlusion-refine toggle. Phase is 0. No authored local lights.
- All full before/after frames and the unscaled detail crops were inspected.
  The demo has no auto-record wiring, so no clip is claimed.

## Scene settings and observed change

| Pair labels | Scene arguments | Population and fog | Light |
|---|---|---|---|
| `fog_ceiling9`, `fog_ceiling_yaw9` | `--ceiling-treatment` | Two 4×4×16 FIELD columns, one governed BODY twin, one 60×60×3 FIELD slab: four solids, 11,568 authored voxels. Source (0,0,4), radius 14, softness 2, ceiling 5 / fade 4. Treatment off. | One vertical sun (0,0,-1), shadows on. |
| `fog_ceiling_treatment9`, `fog_ceiling_treatment_yaw9` | `--ceiling-treatment --reveal-treatment` | Same geometry/source; treatment density .5, cap tone .5. | Same vertical sun. |
| `fog_occlusion_column_top`, `fog_occlusion_column_top_yaw` | `--occlusion=column-top --fog-debug-color` | Two 6×6×9 FIELD plateaus and a 60×60×3 FIELD slab: three solids, 11,448 voxels. Gated foot source (-8,-8,4), radius 20, hard ceiling 4; gated top source (0,0,-5), radius 6, no ceiling; softness 1 each. Treatment off. | Same vertical sun. |
| `fog_ceiling_shadow9`, `fog_ceiling_shadow_yaw9` | `--ceiling-shadow` | Three 4×4×16 columns (two FIELD, one BODY), 4×4 control, 60×60×3 FIELD slab. Before control height 8; after 9: 11,696 → 11,712 authored voxels, five solids. Source (0,0,4), radius 30, softness 2, ceiling 5 / fade 4. Treatment off. | One sun, normalized (.35,.85,-.4), shadows on. |

The ceiling pairs show the magenta upper FIELD silhouettes on master.
Head removes those voxels, exposes caps, and keeps the blue BODY twin
whole. Treatment makes voxel-shaped gaps in the partial band. Column-top
preserves the main plateau while removing the control's hidden upper
section. The shadow pairs compare the old midpoint rule against the
retained-solid rule and the deliberately re-authored control height.

| Probe | Master | Head |
|---|---|---|
| Soft ceiling, each FIELD column | 272 carrier texels, 40/40 top texels painted | 160 texels, top 8/8, aboveCut 0, capFaces 32, core 48, hollow 0 |
| Treated ceiling, centre / rim | 272 texels each, 40/40 top painted | 157 / 150 texels, top 8/8, aboveCut 0, caps 37 / 50, hollow 0 |
| Treated per-axis route | Full upper silhouette | Top caps 17 / 18, hollow 0 |
| BODY twin | 272 texels, 192 above ceiling, painted/partial 0, ratio 1 | Same |
| Column-top main plateau | bandTexels 120, oracleHidden/painted 0 | bandTexels 108, oracleHidden/painted 0 |
| Column-top control | controlTexels = controlPainted = 120 | controlTexels 0, cap 72, core 48, hollow 0 |
| Shadow centre / rim / control | 17.460 / 17.460 / 17.079 | 19.690 / 19.690 / 19.690 |
| Shadow BODY / sun texel | 20.996 / .463 | Same |

The two new hard-ceiling references use `--ceiling-treatment
--ceiling-hard`, with the same settings and fade 0. Both FIELD columns
report top 5/5, aboveCut 0, capFaces 32, hollow 0; each per-axis route has
16 top caps. The BODY twin remains whole.

Detail crops preserve the original pixels. Rectangles are
`(left, top, right, bottom)` in the 2560×1440 full frames:
`treatment-yaw-cap` (750,180,1450,920), `shadow-cardinal-edge`
(500,800,1800,1420), `shadow-yaw-edge` (350,800,1900,1360).

## Phase, grid and curved controls

Treatment phase .37 changes 76 band samples with `outsideBand=0`.
Both explicit phase-zero PNGs equal both omitted-phase PNGs byte-for-byte.
Replacing the complete Metal phase-height helper with `return 0.0f;` in
an isolated runtime also produces those same two PNGs.

Treated static shadows read centre 19.690 / CPU 19.690 and rim 19.690 /
CPU 19.309, within the .463 sun texel. The live VISIBLE-grid arm reads
centre 31.765 / CPU 31.765. Replacing its Metal grid read with an
uncovered/default-channel placeholder gives 19.690 / 31.765 and fires
the expected shadow-oracle failure.

The curved after-only frame uses `--ceiling-shadow-curved`: two radius-2,
height-10 cylinders (FIELD and BODY), 4×4×16 and 4×4×9 FIELD columns,
and a 60×96×3 FIELD slab (17,680 voxels plus two analytic shapes).
Other settings match the shadow scene; yaw is 0. FIELD/BODY reach is
20.996/22.845, gap 1.849 above the unchanged 1.149 floor, and receiver
reaches are 39.000/30.841.

The earlier fixture correction extended only this floor: its old extent
ended the BODY lane at 20.996. The pushed fixture evidence recorded two
negative controls: restoring that floor fails the receiver-clipping
guard; bypassing the FIELD caster predicate with the extended floor gives
equal reaches 22.845 and fails the gap floor at 0.000. The current normal
curved run repeats the passing measurements. The full frame supplements
the carrier oracle; it is not a before/after comparison.

Performance reports and compiled mutation controls are recorded under
`docs/perf/fog-ceiling-retention/`. Native OpenGL remains a separate host
check.

## Full sweep controls

- Master `--all`: job `20261014T000942Z-render-verify-f33beb`, terminal
  `succeeded exit=0`: all 186 checks across nine demos pass, no skips.
- Head `--all` before reference changes: job
  `20261013T234342Z-render-verify-97e0d0`, terminal `failed exit=1`:
  178 checks pass, exactly the eight approved changed fog rows fail, and
  the two new hard-ceiling views skip because their references are absent.
  No other failing row is hidden by re-blessing.
- `verification/sweep-comparison.json` records all 186 matching checks:
  159 unaffected pixel rows have max delta 0 on both refs, and all
  19 structural checks pass. The repeated ground LOS-edge label names
  two different ROIs; the second occurrence has a `#2` suffix in this
  comparison so neither check is lost.
- The ten frames gated only by structural metrics also have matching PNG
  bytes in separate native captures. Their flags, warmup, slice offsets
  and hashes are under `verification/structural-*` and
  `verification/structural-identity.json`.
- All 36 lighting-domain frames are byte-identical. Head light-verify
  completes all captures/domain assertions and passes 45/46 images.
  Its one inherited failure, `domain_z8_yaw0_inwin`, matches master at
  99.4535%, max delta 124, PSNR 33.67; open issue #4409 was rechecked.
- Final targeted head fog verification: job
  `20261014T003200Z-render-verify-e5513f`, terminal `succeeded exit=0`:
  all 69 checks pass, no skips; every pixel row has max delta 0 after
  the eight reference replacements and two additions.

Archived text logs trim trailing whitespace. PNGs retain their original
bytes; the additional detail crops are unscaled extracts. Full sweep logs,
lighting comparison, capture hashes and native probe logs are adjacent in
`verification/`.
