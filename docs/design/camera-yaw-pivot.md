# Camera Z-yaw rotation pivot

Source of truth for how camera Z-yaw chooses the point it rotates about, and
the offset math that pins it. Consumed by `IRRender::getEffectiveCameraIso`
(`engine/render/src/ir_render.cpp`) and the camera input systems.

## The contract

The composite places a world point on screen at
`screen_iso(W) = pos3DtoPos2DIsoYawed(W, yaw) + getEffectiveCameraIso()`.
To keep a chosen focus `F` at a fixed screen position as the camera Z-yaws
(`F` holds its screen position), the camera offset must **cancel** `F`'s
yaw-induced canvas drift. That single formula lives in one place:

```
IRMath::cameraYawPivotOffset(cameraIso, F, yaw)
    = cameraIso - pos3DtoPos2DIsoYawed(F, yaw) + pos3DtoPos2DIso(F)
```

It yields `screen(F) = cameraIso + pos3DtoPos2DIso(F)`, which is independent of
`yaw` — `F` is pinned. At `yaw == 0` it returns `cameraIso` (the no-rotate fast
path, byte-identical to `ORIGIN` mode). **Never inline this formula** — call the
helper.

## Pivot modes

`RotationPivotMode::CAMERA_CENTER` (the engine default) picks `F`:

1. **Screen center (default).** `F` is the world point under the EXACT viewport
   center, at the depth the content there actually renders at (#2547):
   `isoPixelToPos3D(viewCenterIso, d)`, where `viewCenterIso = canvasSize/2 -
   trixelOriginOffsetZ1(canvasSize) - cameraIso` (derive: a world point lands at
   screen center when `pos3DtoPos2DIso(W) + cameraIso == canvasCenterIso`) and
   `d` is the **iso depth** (`x+y+z`, NOT `z` — `isoPixelToPos3D`'s third
   argument is an iso depth) read back from the composite depth attachment at
   the viewport-center pixel. A background center pixel falls back to `d = 0`,
   the pre-#2547 behavior.

   **What it pins (ratified 2026-07-29, epic #2544).** The default pivot pins
   **the surface point under the crosshair** — that point holds its screen
   position across the yaw sweep. It does NOT pin the axis or centroid of the
   content under the crosshair: a depth buffer knows only surfaces, and "the
   axis of the content" is not a well-formed quantity for terrain, floors, or
   merged voxel fields, which is where a default pivot spends most of its life.
   The choice is also what keeps ONE meaning of "rotate" in the engine — Phase 4
   (#2548) latches a *clicked* surface point (`castVoxelRay`'s `worldHitPos_`),
   so an axis-pinning default would fork the contract by pivot-acquisition
   route. Consequence, and it is correct behavior rather than a defect: an
   extended body swings about its near surface by up to its own radius. If
   literal spins-in-place is ever wanted it is an **object-pivot mode** — its
   own issue, layered on top of the surface latch (pick → entity →
   transform/centroid), never a replacement for it.

   Earlier text in this doc, in #2547, and in the epic plan described the
   contract as "what I'm looking at **spins in place**". That wording was
   authored under a point-probe approximation, where surface ≡ axis and the
   arithmetic works exactly; treat it as descriptive of that case, **not** as a
   contract for extended bodies. The sentence above supersedes it.

   **Latch policy.** `RenderManager::updateDefaultRotationPivotFocus` runs once
   per frame from `beginFrame`, ahead of the RENDER pipeline, so every stage in
   a frame reads ONE focus. It re-derives only while `visualYaw` is unchanged
   between frames (per-frame absolute-yaw delta under
   `RenderManager::kPivotYawSettleDelta`) AND the previous frame rendered the
   current pan/zoom — the depth attachment
   it reads belongs to the previous frame, so a derive is only sound one frame
   after the camera settles. While yaw moves the latch is HELD: that is what
   pins the pre-rotation center content through the whole rotation, identically
   for a mouse drag, a key, or a programmatic `setYaw` (auto-screenshot needs no
   gesture plumbing). A genuinely still camera does ZERO readbacks — a readback
   costs a full GPU flush — but the cost lands on every motion-stop frame during
   real interaction, not once at startup.

   **What is latched is the iso DEPTH, not the point.**
   `getDefaultRotationPivotFocus` recomputes
   `isoPixelToPos3D(viewCenterIso, latchedDepth)` from the *live* `cameraIso` on
   every call. This is required, not stylistic:
   `IRMath::cameraMoveRelativeToYaw` (the pan pre-compensation every pan system
   goes through) inverts `d effCam / d cameraIso`, which equals
   `P(R_z(−yaw)·Pinv(Δ))` only while the focus tracks the camera. Because
   `isoPixelToPos3D`'s depth parameter shifts along `(1,1,1)` — projecting to
   `(0,0)` — the latched depth is invisible to that derivative, so a depth-aware
   pivot and the pan identity coexist exactly. Latching the *world point*
   instead collapses the derivative to the identity and interactive pan at any
   non-zero yaw overshoots (at yaw 90°, a `(10,0)` drag moves content `(20,30)`)
   and pops back on mouse-stop. The pivot-verify harness cannot see this class —
   it holds `cameraIso` fixed while sweeping yaw, the one regime where a frozen
   and a live focus agree — so the guard is the headless unit test
   `test/render/camera_pan_pivot_test.cpp`.

   The offset is the drift-cancel `cameraYawPivotOffset` form
   above — NOT a bare `pos3DtoPos2DIsoYawed(F, yaw)`, which leaves a yaw-varying
   residual that swings a panned scene in an arc (the latent #1352 bug;
   un-panned both forms collapse to 0, so canvas_stress — which never pans —
   never caught it, but shape_debug's panned pivot shots did).

   Do **not** use the mirrored legacy #1352 point `isoPixelToPos3D(cameraIso, 0)`:
   it is the viewport center reflected across the canvas origin, so it pivots about
   the wrong point and the panned scene swings the opposite way.

   The viewport-center focus is ~1 trixel off the canvas origin even un-panned, so
   `getEffectiveCameraIso() != getCameraPosition2DIso()` whenever yaw != 0. For the
   DETACHED entity-canvas composite to pivot WITH the GRID/world content (rather
   than drift, the canvas_stress canary jitter behind #1942 → #1944), the composite
   reads `getEffectiveCameraIso()` for its screen placement too
   (`system_entity_canvas_to_framebuffer.hpp`); its de-tile gather parity stays on
   the entity's fixed world iso. Detached + GRID now share one pivot.
2. **Cursor (`Ctrl+Shift+middle-drag`).** `System<CAMERA_MOUSE_ROTATE>` captures
   the world point under the cursor at drag start
   (`IRRender::mouseWorldPos3DAtIsoDepth(0)`) and sets it as an explicit focus via
   `IRRender::setRotationPivotFocus`. The drag reverts to the screen-center
   default on release.

`RotationPivotMode::ORIGIN` skips the correction (offset == `cameraIso`); Z-yaw
pivots about the world origin.

An explicit focus set by any caller (`setRotationPivotFocus`, #1921/#1927)
overrides the default — used by the cursor mode and by `shape_debug
--pivot-focus-demo`.

## Empirically verified

The **explicit-focus** path: `shape_debug --pivot-focus-demo` pins an explicit
focus on a pillar at world (8,−8,10) (off-origin, z>0 — the hard case) and sweeps
yaw. The pillar's centroid holds the exact screen center (measured 1279.5,720.5 vs
center 1280,720) while the ring of markers orbits it; a broken pivot drifts
hundreds of px (#1926 measured 1024px). See
`docs/pr-screenshots/claude/1926-camera-pivot-screen-center/`.

The **screen-center default** path (the viewport-center fix above): `shape_debug`
panned to cameraIso (16,16) and yawed 0/90/180/270/45 holds screen-center content
fixed — at yaw 180 a landmark at screen-offset (−Δx,−Δy) from center maps to
(+Δx,+Δy), an exact point-reflection through screen center. Before the fix the
panned scene swung off-frame (yaw 0 vs yaw 180 differ 15.9%). yaw 0 stays
byte-identical (fast path). canvas_stress (un-panned, with the detached
composite now on the effective offset) is byte-identical at yaw 0 and differs only
0.21% at yaw 45 — the small whole-composition pivot shift, with detached + GRID
moving together (vs the 1.3–3.4% detached *drift* that #1942 alone caused).

Corollary: a scene that "swings" under rotation now means the **content is laid
out off-center** (not a pivot bug) — pin a focus (cursor mode, or a
content-centroid focus) to rotate it in place. (Before this fix the default pivot
itself swung a panned scene; that path is now correct.)

## Known deviations (2026-07, epic #2544)

The `scripts/pivot-verify.py` harness (isolated cylinder probe, two oracles —
the `[pivot-focus-assert]` pinned point and `jitter_probe --stationary`
whole-silhouette invariance; no reference images) enumerates the defects it has
found in the contract above — all invisible at cardinal yaw 0, so the
"Empirically verified" section below remains true for what it measured while the
pivot is still wrong under rotation. #2545 (deviation 1), #2546 (deviation 3),
and #2547 (deviation 2) are now fixed; deviation 4 turned out not to be a pivot
defect at all (#2645 — a destination-grid quantization floor, see the
subsection after this list). Fixed entries are retained below until the epic
closes:

1. **Half-cell rotation-anchor mismatch — FIXED (#2545).** The voxel raster
   rotated content about `position + (0.5,0.5,0.5)` while the SDF path and
   the CPU pivot/picking math rotate about the exact `position`; under yaw
   the voxel layer orbited any pinned focus ~1 iso px and the voxel/SDF
   layers counter-rotated apart. The settled convention — **an entity's
   `position` is the rotation anchor for every render path** — is now
   enforced in the raster: the smooth per-axis route projects cell positions
   through `pos3DtoPos2DIsoYawedCellAnchor` / `yawedIsoDistanceCellAnchor`
   (`ir_iso_common.{glsl,metal}` — the lower-corner cell lattice shifted half
   a cell so the rendered mass rotates about the authored lattice; exact
   no-op at yaw 0 since `iso(0.5,0.5,0.5) == (0,0)`), and the settled
   cardinal store keeps the **plain** `rotateCardinalZ` position — the
   former `cardinalLowerCornerShift` add (and its undo in
   `trixelCanvasPixelToWorld3D`) was this bug's cardinal form and is retired
   from the store/cull/resolve chain. Exact world positions (SDF centers,
   entity translations, the pivot math itself) keep the un-anchored
   projections.
2. **Default focus depth — FIXED (#2547), gate re-grounded.** The pinned set of
   the drift-cancel offset is the vertical column `{W : W.xy == F.xy}`. `F` was
   the **iso-depth-0** point under the viewport center, so content at screen
   center at another depth sat on the center iso ray, off the pinned column by
   (t, t) in xy, and orbited — 336 px at z=10, zoom 4 on master, 320 px once
   #2545's half-cell landed.

   The depth-aware derivation above now pins the point the depth buffer reports
   under the center pixel. **But a depth buffer yields the SURFACE under the
   center pixel, while the harness's original oracle scored the whole-silhouette
   CENTROID of an extended probe.** Pinning a column on a rigid body's surface
   orbits its centroid by 2r, which that metric converts to `16·(δx+δy)` px —
   ~22.6·r px at zoom 4. Measured: `center-depth` 320 → 92 px, `center-column`
   0.9 → 76 px (both within 2% of that model) for the radius-4 probe. No probe
   radius above ~0.07 world units can pass a 1.5 px centroid gate, so the old
   gate was unreachable by construction, not a tuning gap: it encoded "pin the
   content's axis" while the ratified contract is "pin the surface point".

   Re-grounded per the ruling of record
   ([#2544 comment 5106383295](https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5106383295),
   plan amendments A2/A3). `center-column` / `center-depth` keep their geometry
   — they are what *demonstrates* the contract — but are now scored by the
   **pinned-point oracle**: `shape_debug` emits a per-shot
   `[pivot-focus-assert]` line comparing the focus the engine derived from its
   live composite-depth readback against the analytic ray/surface intersection
   over the probe's own carve constants, and `pivot-verify.py` fails the pass on
   any FAIL, on a latched focus that moves mid-sweep, or on a block that moved
   the camera pan/zoom mid-sweep (`view_held=0`). That last one is the
   moved-focus check's own precondition: the latch re-derives on pan/zoom by
   design, so a block that pans would break the check on correct behavior, and
   it is reported as a misconfigured block rather than a pivot regression. Their
   silhouette deviation is still measured and reported, just not gated.

   Two blocks join them, both new in #2547:

   - `background-center` — the center pixel reads BACKGROUND, so the derive must
     take its `d = 0` fallback (Phase 3 acceptance criterion 2, amendment A1).
     Its probe stays rotationally symmetric about the pinned column, so the
     whole-silhouette oracle remains exact for it: PINNED at 0.91/1.21 px
     (zoom 4) and 0.94/1.25 px (zoom 8).
   - `center-axis` — the probe's axis lies ON the viewport-center ray with its
     near cap at the ray's entry step, so the derived surface point is the
     probe's own axis point and the silhouette *should* rotate onto itself. It
     is the isolating diagnostic for the residual below.

   A third joins them in #2548, for the CURSOR pivot rather than the default:

   - `cursor-latch` — `center-axis` geometry, but the focus comes from
     `IRPrefab::CursorPivot::resolveFocusWorld` (the real `castVoxelRay` path)
     with a synthetic cursor parked on the viewport-center anchor's screen
     pixel, latched once and held for the sweep. Same routing as `center-axis`
     — pinned-point oracle, silhouette reported not gated (16 px at zoom 4, the
     same residual model plus the half-voxel offset below). It runs on the
     GUI-test cycler rather than the plain auto-screenshot one, because it needs
     scripted cursor input and a per-frame hook: the shot cycler clears the
     pivot focus at every shot boundary and the latched point is only known at
     runtime, so it cannot ride the shot table.

     Its tolerance is a whole world unit rather than 0.6, and the reason is
     geometric, not slack: the cursor latch reports a `castVoxelRay` SURFACE hit
     — the marched point where the ray first lands inside the winning voxel's
     unit cube — while the analytic oracle predicts that voxel's CENTER. The L2
     gap is bounded by the cube's half-diagonal, `sqrt(3)/2 ~= 0.87`, and the
     bound is TIGHT here rather than pessimistic: the iso ray runs along
     (1,1,1), so it enters through the cell's near corner and the measured delta
     is exactly 0.87. The gate keeps ~8x of margin over the regression it
     exists to catch (a revert to the pre-#2548 iso-depth-0 latch lands ~8.5
     world units off on this geometry).

   **Residual: composite-depth quantization (accepted, measured).** The derive
   consumes what the composite reports, which is quantized per trixel at
   sub-voxel resolution. Against the probe's voxel lattice the readback lands
   within one iso-depth unit — measured on macOS/Metal at zoom 4:
   `background-center` 0.00 (exact), `center-depth` +0.5 iso (lateral-surface
   entry), `center-column` and `center-axis` +1.0 iso (both enter through a
   camera-facing cap). A 1-iso-unit bias displaces `F` by (1/3, 1/3, 1/3) world
   units, whose xy part leaves a residual orbit: `center-axis` measures 12 px at
   zoom 4 and 22 px at zoom 8, which is that model to within the read. This is
   ~12x better than the pre-#2547 focus in the same configuration (150 px at
   zoom 4) but is not exact; whether the cap-entry case can report the geometric
   surface rather than a neighbouring trixel's depth is **#2641**, and
   `--pivot-verify center-axis` is its repro. That issue also carries the
   cross-backend risk: the trixel→framebuffer parity shift applies to the depth
   read on GL but not on Metal, so the bias may differ per host (measured on
   macOS/Metal only).

   The `[pivot-focus-assert]` tolerance (0.6 world units) is set to admit
   exactly that one-iso-unit quantization and nothing more: a regression to the
   pre-#2547 iso-depth-0 focus is 3.46 world units off on `center-column` and
   12.1 on `center-depth`, and electing the far surface instead of the near one
   is 10.0 off on `center-depth`.
3. **Per-axis registration offset — FIXED (#2546).** With (1) compensated,
   every residual-yaw frame rendered the voxel scene a constant ≈1 iso px
   (per axis, zoom-scaled) off the cardinal frames. Root cause: the
   forward-scatter's SCREEN re-projection anchored on the per-axis STORE
   origin `perAxisBase` (= `trixelOriginOffsetZ1(canvasSize) + floor(cameraIso)`),
   whose `trixelOriginOffsetZ1` carries the trixel grid's `(-1,-1)` sub-pixel
   LATTICE alignment — a canvas-storage convention the `ij - perAxisBase`
   recovery needs, but not a screen offset. The scatter emits true face quads
   (no trixel-grid gather), so that lattice alignment rode into the on-screen
   placement while the cardinal gather's focus carried none. Fix: anchor the
   re-projection on the canvas geometric CENTER (`canvasSize/2`, exactly
   `perAxisBase + (1,1)` back from the storage origin) in
   `v_peraxis_scatter.glsl` / `metal/peraxis_scatter.metal`; recovery keeps
   the store anchor unchanged, so cardinal frames stay byte-identical (the
   scatter runs only at non-cardinal yaw). Depth (`yawedIsoDistanceCellAnchor`)
   and the store/RESOLVE/overflow paths are untouched — pure screen-XY
   registration.

4. **SDF-twin silhouette wobble — NOT A DEVIATION (#2645).** `focus-ctr-sdf`
   scores DRIFT 2.00 px at zoom 4 and 8 while its voxel twin holds 0.94/1.27
   with the same explicit focus. Both take an explicit `setRotationPivotFocus`,
   so the #2547 derive never runs for them and this is not a default-pivot
   deviation. Of the two candidate readings — the analytical solver's
   continuous silhouette re-forming per yaw (a comparison, not a 1.5 px gate)
   versus an SDF-side rotation-anchor delta #2545 did not cover — #2645 settled
   it on the first by zoom sweep: the deviation is flat at exactly 2.00 px over
   a 16x zoom range, which a world-space anchor delta cannot be. The twin is
   reported rather than gated; see §"Not a deviation: the SDF twin's flat
   2.00px floor (#2645)" below.

Fix chain and acceptance gates: epic #2544 (P1 #2545 → P2 #2546 → P3 #2547 →
P4 #2548, cursor-pivot true-depth latch + indicator). Each child flips its
pivot-verify block(s) to its own gate — PINNED for the blocks whose probe
rotates about a point on its own axis, `[pivot-focus-assert]` FOCUS-OK for the
default-pivot blocks whose silhouette legitimately orbits (see deviation 2).
This section shrinks as they land.

### Not a deviation: the SDF twin's flat 2.00px floor (#2645)

`focus-ctr`'s SDF twin (`--pivot-verify-sdf`) draws a DRIFT verdict from
`jitter_probe` at 2.00px while the voxel twin on the same explicit focus pins
at ~1px. This is **not** a pivot defect and no pivot fix can move it — it is
the SDF path's rasterization quantum, so the twin is **reported, not gated**
(`SDF_GATED` in `scripts/pivot-verify.py`) and the harness prints it as
`REPORT` rather than failing the run.

The discriminator is zoom. A rotation-anchor delta of Δ world units projects to
Δ·zoom screen px, so it must scale with zoom; a destination-grid quantization
floor must not. Measured on macOS/Metal against `origin/master` @ `13094837`, one
full-circle 9-yaw sweep per cell:

| zoom | voxel dev_x / dev_y | SDF dev_x / dev_y |
|---|---|---|
| 1 | 0.99 / 1.35 | **2.00** / 2.00 |
| 2 | 0.98 / 1.36 | **2.00** / 2.00 |
| 4 | 0.94 / 1.27 | **2.00** / 2.00 |
| 8 | 0.96 / 1.31 | **2.00** / 2.00 |
| 16 | 0.06 / 0.53 | **2.00** / 1.34 |

`dev_x` is **exactly 2.00px at every zoom over a 16x range** — flat, so the
anchor-delta reading is ruled out by measurement rather than by argument.

That 2.00px is not an arbitrary number: it is **one whole game-resolution
pixel**. `shape_debug` renders a 1280x720 game resolution
(`creations/demos/shape_debug/config.lua`) into a 2560x1440 HiDPI framebuffer
(confirmed from the captured PNG headers), so `outputScaleFactor == 2` and the
smallest step the destination grid can represent is 2.00 framebuffer px. The
raw centroids sit right at that quantum — at zoom 4 they take only two discrete
values per axis, exactly 2.00px apart (x ∈ {1277.50, 1279.50}: the frame-centre
pixel and the one game-pixel step next to it).

Because the quantum is one *game-resolution* pixel, the framebuffer figure is
host-dependent: on a 1x (non-HiDPI) host the same floor should read ~1.00px and
fall under the 1.5px threshold on its own. A Linux/GL re-measure that reports
~1.00px is therefore agreeing with this entry, not contradicting it — the
un-gating is keyed on the floor being a floor, not on the specific number.

The voxel twin has a lattice of its own to land on: its cells sit on exact
integer world positions, so its silhouette re-forms identically at each yaw and
it pins sub-pixel. The SDF twin is a *continuous* solved surface with no such
lattice, so as it rotates its silhouette edge crosses destination-pixel
boundaries and the centroid steps by the one-pixel quantum. That is the
sampling floor of rasterizing continuous geometry to a fixed grid: you cannot
pin a continuously-moving analytic silhouette better than the destination pixel.

This agrees with the independent #2469 measurement in
[`engine/render/CLAUDE.md`](../../engine/render/CLAUDE.md) §"Accepted sub-pixel
yaw-sweep centroid residual", which records the same 2.00px x-excursion for the
SDF cylinder at zoom 4/8 under the unrelated `--yaw-sweep` harness and already
treats the SDF twin as the *defect-free control* whose residual is "a floor the
probe itself carries". Two harnesses, one number.

Closing it would mean resolving the silhouette below one destination pixel —
supersampling / conservative rasterization on the SDF path, which is the same
principled root fix already deferred to epic **#1933** for the #1883 corner
drift and the #2469 centroid residual. It is not reachable by any change to the
pivot math, which is what this harness exists to gate. The twin keeps running
because the A/B against the voxel path is the useful signal; only its
contribution to the exit code is dropped.

## History

- #1352 / #1362 — first focus-pivot (panned-and-rotated correctness).
- #1921 / #1927 — explicit point-of-interest focus (`setRotationPivotFocus`).
- #1926 / #1942 — the `cameraYawPivotOffset` helper + the cursor pivot mode.
- #1944 — reverted #1942's "exact viewport-center default": the `viewCenter` is
  ~1 trixel off the canvas origin, so it shifted effective-offset world content
  relative to the **raw**-offset detached entities (canvas_stress rotation jitter).
  The revert also dropped the `cameraYawPivotOffset` wrapper, leaving the latent
  #1352 panned-swing bug.
- #1944 follow-up (this change) — restored #1942's exact viewport-center focus
  AND the drift-cancel wrapper, and completed the documented prerequisite: the
  detached entity-canvas composite now consumes `getEffectiveCameraIso()` for
  placement, so detached + GRID share the corrected pivot. Fixes the panned-scene
  rotation swing (shape_debug) without reintroducing the detached drift (#1944).
