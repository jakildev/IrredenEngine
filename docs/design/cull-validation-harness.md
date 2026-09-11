# Frozen-cull free-fly validation harness (#1438, epic #1437 P1)

Engine-side procedure to **prove the camera-yaw cull never drops on-screen
content**, and to characterize how tight/loose the current cull is. This is
phase 1 of the camera-yaw cull epic (#1437); the closed-form cull rewrite
(P2 / #1439) validates against this harness.

## What "freeze the cull, then free-fly" means

The render cull viewport is shared state: `IRRender::CullViewportState`
(`engine/prefabs/irreden/render/cull_viewport_state.hpp`). Each frame
`VOXEL_TO_TRIXEL_STAGE_1` calls `updateCullViewport(liveCameraIso, liveZoom,
canvasSize)`, which tracks the live camera **unless the cull is frozen**, in
which case it pins the viewport at the pose captured on the freeze transition.
Both the CPU chunk-visibility mask and the GPU `cullIsoMin/Max` bounds derive
from `getCullViewport()` (via `IRPrefab::SunShadow::shadowFeederCullViewport`),
so freezing pins the entire cull gate while the camera keeps moving.

Freezing is driven two ways, both writing the same flag
(`IRRender::detail::cullingFreezeFlag()`):

- **Interactive** — `IRCommand::Command<TOGGLE_CULLING_FREEZE>`, bound to **F10**
  in `shape_debug`. Press F10 to freeze, then free-fly with the standard camera
  controls (WASD pan, space/middle-drag pan, alt/ctrl-drag yaw, scroll zoom) to
  watch exactly what the frozen cull retains as the view moves.
- **Programmatic** — `IRRender::setCullingFrozen(bool)`, used by the
  auto-screenshot sweep below so the validation runs headless.

## The automated sweep: `shape_debug --cull-validate`

```bash
fleet-build --target IRShapeDebug
fleet-run IRShapeDebug --auto-screenshot --cull-validate
```

`--cull-validate` (requires `--auto-screenshot`) builds a paired
**live / frozen** camera sweep over one shared pose list — a full yaw rotation
at the focus, then a pan sweep (±12 world units) at a non-cardinal yaw (45°,
where the per-axis smooth-yaw composite is active). The capture order is:

| shots | label             | cull state                              |
|-------|-------------------|-----------------------------------------|
| 1..12 | `cv_live_NNN`     | live (cull tracks the camera)           |
| 13    | `cv_freeze_ref`   | **FREEZE** at a wide reference (zoom 1, origin) |
| 14..25| `cv_frozen_NNN`   | frozen (cull pinned at the reference)   |
| 26    | `cv_unfreeze`     | UNFREEZE (leaves global state clean)    |

The freeze reference is deliberately **wide** (zoom 1 at the origin): its iso
cull viewport is a superset of every zoom-4 sweep pose, so a frozen frame is the
"cull-effectively-disabled" ground truth — every voxel geometrically on-screen
at that pose is drawn, with nothing the live cull might wrongly drop. Pairwise-
diffing `cv_live_NNN` against `cv_frozen_NNN` over the on-screen region answers
the retention question: any on-screen pixel present in the frozen frame but
missing in the live frame is content the live cull dropped.

The full-frame screenshots are numbered `screenshot_<counter>.png`; capture order
makes `cv_live_i` pair with `cv_frozen_i` at counter offset `i + 13`. Diff a pair
with the committed stdlib comparator:

```bash
python3 scripts/render-compare.py <frozen>.png <live>.png --diff-out diff.png --json
```

This pairwise diff is the whole assertion — see
[Cross-run contract](#cross-run-contract-relative-only-by-design-2955) for why
no committed baseline exists.

### Why `--cull-validate` disables sun shadows

The cull viewport also drives the **sun-shadow-feeder AABB**
(`shadowFeederCullViewport` widens `getCullViewport().isoViewport()` by the sun
sweep). A frozen *wide* cull therefore bakes a different shadow set than the live
cull — a lighting difference that has nothing to do with dropped voxels but
dominates a naive full-frame diff. `--cull-validate` calls
`IRRender::setSunShadowsEnabled(false)` so the cull's only on-screen effect is
which voxels rasterize, and the diff isolates voxel retention. (The interactive
F10 path keeps shadows on for realistic free-fly inspection.)

## Cross-run contract: relative-only, by design (#2955)

`scripts/cull-verify.py` asserts one thing: at every pose, `cv_live_i` matches
`cv_frozen_i` **from the same run**. There is no committed baseline and no
bless flag — the harness never compares a capture against a previous run's
frames, and it must not grow a reference set it does not read.

Why not add one:

- Absolute render drift on `shape_debug` is **render-verify's** job. Its
  manifest covers the same pose family (zoom-4 yaw sweep, pan-under-yaw pivot
  arms) on every blessed backend, and it is the gate that went red on the
  registration / camera-pivot merges (#2943) and was re-blessed per shot
  (PR #2945). A second frozen-frame set would have been a duplicate alarm in
  the same window, paid for twice at every deliberate render change — and
  once per backend, since a cull baseline is as host-keyed as a render one.
- A committed frozen frame would not catch the failure the relative check is
  blind to — a freeze that silently stops pinning the viewport. Measured on
  macOS/Metal (2026-09-10): with `setCullingFrozen` stubbed to a no-op, the
  frozen phase's frames differ from a correctly frozen capture by at most
  0.037 % of bytes (9 of 12 poses byte-identical), so even a freshly blessed,
  correct baseline scores ≥ 99.96 % against a vacuous run — inside
  `CULL_THRESHOLDS.match_pct` (99.7 %) — and PASSes. The cull is conservative
  enough that pinning its viewport wide barely changes what rasterizes, which
  is exactly what makes a frozen-frame reference unable to police the freeze.
  (Finding 1 below records ~0.2 % at the inter-cardinal poses; that is the
  #1438-era measurement and remains the record of what was seen then. The
  residue has since shrunk — the point here is the direction, not the exact
  figure: the gap between a working freeze and a broken one is far smaller
  than the gate's tolerance, and has only got smaller.)
- `CULL_THRESHOLDS` are calibrated to that live-vs-frozen AO residue (#1438),
  not to cross-run drift.

**The vacuity above is a live gap, not an argument for the reader to accept.**
The same measurement shows the *relative* check passes at exit 0 with the
freeze fully disabled, and it is now the harness's only assertion — tracked in
**#3209**, whose guard must be a viewport-state assertion rather than an image
one (the images are identical in the failing case).

History: #1441 / `5b6a8859` shipped `--update-baselines` and 12
`macos-debug/cull-verify/cv_frozen_*.png` as a manual-inspection artifact;
nothing ever read them, they went stale unnoticed (80–96 % match by 2026-08-07)
while the harness stayed green — because the harness never looked at them — and
#2955 removed both. Want absolute coverage of a cull pose? Add the shot to
`shape_debug`'s render-verify manifest — one reference discipline, one
re-bless. `scripts/tests/test_cull_verify_contract.py` pins this section.

## Findings (macOS / Metal, 2560×1440 framebuffer)

### 1. The current cull retains the full on-screen set — no drops

Across the entire yaw + pan sweep, **every voxel shape present in the frozen
(ground-truth) frame is also present in the live frame, at the same screen
position.** Visual inspection of the live/frozen diffs shows no shape silhouette
in the diff; the cull is conservative and never drops on-screen content under
camera yaw or movement.

With sun shadows disabled, the live-vs-frozen match is:

| pose         | match % | max byte Δ | PSNR (dB) |
|--------------|---------|-----------|-----------|
| yaw 0°       | 99.997  | 81        | 58.3      |
| yaw 90°      | 100.000 | 0         | ∞ (identical) |
| yaw 270°     | 99.996  | 114       | 54.3      |
| yaw 45/135/225/315° | ~99.80 | 89–127 | 37–40 |
| pan ±x/±y @ 45°     | ~99.80 | 89        | ~39.8 |

Cardinal poses are byte-identical or near-identical. The residual ~0.2% at the
inter-cardinal and panned poses is **not dropped geometry** — see finding 2.

### 2. The cull viewport couples to downstream lighting (a diff confound)

Two lighting stages derive from the same cull viewport, so a frozen *wide* cull
changes their output even though no geometry is dropped:

- **Sun-shadow feeder** (`shadowFeederCullViewport`) — dominant. Disabled in the
  harness; with it on, the floor-shadow contours dominate the diff (~1% of bytes,
  PSNR ~35–40).
- **Emissive light volume** — minor, residual. After disabling shadows, the
  *entire* remaining diff at every confounded pose is a single sphere near the
  scene's emissive cyan light, lit slightly differently (green under the live
  cull, blue under the frozen wide cull). One localized sphere accounts for the
  whole ~0.2% residual.

**Implication for P2 (#1439).** The closed-form cull rewrite must preserve the
`getCullViewport() → shadowFeederCullViewport` derivation (the shadow feeder and
GPU bounds read the same viewport). Validate the rewrite against this harness
with sun shadows disabled to isolate geometry retention from the lighting
coupling.

**Implication for P4 (#1441).** A committed render-verify cull-regression test
should run with sun shadows disabled (and ideally without the emissive light, or
with a per-pixel tolerance that absorbs the one emissive-lit sphere) so the pass
threshold reflects geometry retention, not lighting.

### 3. Tightness / margin

The cull is conservative: at the tested zoom (4) and pose set it retains
everything on-screen with no edge-popping under a ±12-unit pan or a full yaw
rotation. The current per-frame cost is the known gap the epic targets
(`rebuildChunkBounds` re-projects every voxel each rotating frame); this harness
characterizes *correctness*, and P2 rewrites the *cost* while this harness guards
the correctness it just established.

## Reproducing

1. `fleet-build --target IRShapeDebug`
2. `fleet-run IRShapeDebug --auto-screenshot --cull-validate`
3. Screenshots land in
   `build/creations/demos/shape_debug/save_files/screenshots/`.
4. For each `i` in 0..11, diff `cv_live_i` against `cv_frozen_i` (counter offset
   +13) with `scripts/render-compare.py`. A clean run shows only emissive-lit
   surfaces differing; any shape silhouette in a diff is a cull regression.

Interactive: run `IRShapeDebug` with no flags, press **F10** to freeze the cull,
then pan/yaw/zoom to inspect the frozen cull boundary live.
