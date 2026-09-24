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
   trixelOriginOffsetX1(canvasSize) - cameraIso` (derive: a world point lands at
   screen center when `pos3DtoPos2DIso(W) + cameraIso == canvasCenterIso`; the
   store origin `trixelOriginOffsetZ1` carries a `(-1,-1)` lattice alignment
   that is not a screen offset, and built on it the anchor sits one iso unit per
   axis off the framebuffer texel the readback samples) and
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

   **Latch policy — rotation-scoped: acquire at every rotation start, in the
   frame the source was drawn in (epic #2544 D16).**
   `RenderManager::updateDefaultRotationPivotFocus` runs once per frame from
   `beginFrame`, ahead of the RENDER pipeline, so every stage in a frame reads
   ONE focus. The decision and the acquisition arithmetic are
   `IRRender::DefaultPivotLatch`
   (`engine/render/include/irreden/render/default_pivot_latch.hpp`), lifted out
   of `RenderManager` so the whole policy is testable with no GPU.

   - **Acquire once per gesture, at any yaw.** The latch derives on the
     gesture-start edge only: `visualYaw` settled on the previous frame (per-frame
     absolute delta under `DefaultPivotLatch::kYawSettleDelta`) and moving on
     this one. A continuous rotation derives once however long it runs; a drag
     that pauses for a frame and resumes is a new gesture and acquires again at
     whatever yaw it paused — each such acquisition is displacement-free but
     costs one synchronous center-pixel readback, and the pivot can hop to a
     nearer surface mid-drag. Auto-screenshot needs no gesture plumbing: a pose
     snap is a gesture.
   - **Nothing else derives.** A still camera, a pan and a zoom derive nothing,
     so no view jump follows a pan — the pan-settle pop is gone because nothing
     re-latches after a pan, not because a rotation masks it.
   - **The source frame is the one the depth attachment was drawn with.** The
     main composite (`System<TRIXEL_TO_FRAMEBUFFER>`, once per frame) stamps its
     pose: yaw, raw camera, effective camera, canvas center, effective
     subdivisions. A derive in `beginFrame` reads the previous frame's
     attachment and that frame's stamp. The `beginFrame` observation is not that
     pose — `CAMERA_MOUSE_ROTATE` mutates yaw inside RENDER ahead of geometry, and
     auto-screenshot applies the next shot at the RENDER tail — so a drag
     acquires from its own first frame (already rotated one step about the old
     anchor) and a snapped shot acquires from the pre-gesture frame; each is
     displacement-free because each uses the pose its image was drawn with. The
     stamped subdivisions decode the depth: in `SubdivisionMode::FULL` the
     divisor follows zoom, and a shot that changes zoom and yaw together
     acquires from a frame at the old zoom. A frame drawn under a non-default
     pivot, and a creation with no main composite, leave no usable stamp; a
     gesture starting there holds.
   - **Recovery.** With `C` the canvas center, `E` the effective camera, `ψ` the
     yaw and `d` the decoded depth of the source frame, the point under the
     crosshair is `W = IRMath::isoPixelToPos3DYawed(C − E, d, ψ)` —
     `R_z(+ψ)·isoPixelToPos3D(C − E, d)`, the composite's depth being the yawed
     camera-space depth. A **cardinal** source (residual yaw 0 by
     `computeYawSplit`, stamped with the pose) first has
     `DefaultPivotLatch::kCardinalStoreLatticeDepth` (1.5) taken off `d`: the
     cardinal store keys each face on the lower-corner lattice `[p, p+1]` of
     view space rather than on the authored cube `[p − ½, p + ½]`, a `(½,½,½)`
     shift along the view axis that is invisible on screen and puts every
     cardinal key 1.5 yawed-depth units behind the surface the pixel shows.
     Taking it off lands `W` on the visible surface, to within one micro-face.
     The per-axis (non-cardinal) store keys without that shift, so its sample is
     used as-is. The analytic SDF store keys without it at a cardinal too, so
     the latch takes the lattice off only when a **voxel-store** fragment won
     the sampled pixel. Next to the depth readback, at cardinal sources only,
     `RenderManager` reads the main canvas's distance and entity-id textures
     in the 3×3 block (`C_TriangleCanvasTextures::readTexelBlock3x3`) around
     `defaultPivotCrosshairCanvasTexel` — the hover path's cursor→texel
     mapping evaluated at the canvas center with the source frame's effective
     camera and subdivisions — and takes the id of the texel whose stored key
     equals the sampled one (`defaultPivotSampledTexelInBlock`). The mapping
     is exact on Metal; at a fractional camera the OpenGL gather displays the
     row one further in +y, and the cardinal composite copies the canvas key
     texel for texel, so the key match finds the displayed texel on both. An
     SDF shape winner (`C_ShapeDescriptor`) latches its key as it stands. Both
     stores write the winner's id at the texel that holds its depth. The branch exists only for
     the voxel/SDF store disagreement (§"Known deviations" 2, #3742) and goes
     with it. The latch stores `isoDepth = W.x + W.y + W.z` and a view
     offset `o = C − cameraIso − pos3DtoPos2DIso(W)`; by construction the
     effective camera of the source pose is unchanged, so **acquisition never
     moves the view**. A source within `kYawSettleDelta` of yaw 0 latches `d`
     directly and leaves `o` untouched bit-for-bit: yaw-0 frames depend on `o`
     alone and are byte-compared by the reference suites, and the round trip
     through the recovery is not a float identity.
   - **Background holds.** A background or foreground-tier center sample keeps
     both the depth and the offset — the previous anchor, carried by any pan
     since — rather than jumping to the depth-0 point. Before the first
     acquisition the anchor is the depth-0 point under the viewport center with
     `o = 0`, exactly the pre-depth-aware default.

   **What is latched: an iso DEPTH and a view offset, not the point.**
   `getDefaultRotationPivotFocus` recomputes
   `isoPixelToPos3D(viewCenterIso − o, isoDepth)` from the *live* `cameraIso` on
   every call, and every `CAMERA_CENTER` branch of `getEffectiveCameraIso` —
   default and explicit focus — pivots about `cameraIso + o`. The default
   branch then gives `E = C − P_ψ(F)` for any `o`: the anchor sits at the canvas
   center. `o` is the residue of re-anchoring onto a different point of the same
   crosshair ray at non-zero yaw — two points on one ray project to one pixel,
   but a later yaw moves the scene about them differently, by
   `(I − R_z(θ))·(P′ − P)`. It has to live somewhere, and it lives in the camera
   term every consumer already reads. At yaw 0 the effective camera is
   `cameraIso + o`, so after a non-zero-yaw acquisition `getEffectiveCameraIso()
   != getCameraPosition2DIso()` at yaw 0 too, as it already was at non-zero yaw:
   world-anchored raw-camera readers (the debug-overlay projection, sprite
   anchoring) read the effective camera. Applying `o` to the explicit branch as
   well keeps an explicit focus set/clear at yaw 0 jump-free.

   The live point is required, not stylistic: `IRMath::cameraMoveRelativeToYaw`
   (the pan pre-compensation every pan system goes through) inverts
   `d effCam / d cameraIso`, which equals `P(R_z(−yaw)·Pinv(Δ))` only while the
   focus tracks the camera. `isoPixelToPos3D`'s depth parameter shifts along
   `(1,1,1)` — projecting to `(0,0)` — and `o` enters the camera and the focus
   expression as the same constant, so neither is visible to that derivative;
   between gestures the anchor is a world point transported by the pan,
   `F(c) = F0 + isoPixelToPos3D(c0 − c, 0)`. Latching a bare *world point*
   instead collapses the derivative to the identity and interactive pan at any
   non-zero yaw overshoots (at yaw 90°, a `(10,0)` drag moves content `(20,30)`).
   The guard is the headless unit test `test/render/camera_pan_pivot_test.cpp`.

   **Verification.** `test/render/default_pivot_latch_test.cpp` drives the latch
   frame by frame — pan, zoom, in-RENDER yaw mutation, background, the mode
   gate, the stamped divisor, the cardinal lattice and its subject branch, the
   crosshair texel — and asserts the effective camera across an acquisition at
   yaw 0, 22.5°, ±45°, 90° and 180°.
   `scripts/pivot-verify.py`'s sweep blocks assert per gesture: every shot is a
   pose snap, so each shot whose yaw changed acquires from the previous shot's
   settled frame; a non-gesture shot is scored against the previous focus
   carried by the pan, and the latch may move at most once per gesture. A
   gesture is graded against the first carved cell the source frame's crosshair
   ray enters (an exact ray/unit-cube test over the probe's own carve), by the
   store that frame took. Both bounds come from how the store keys a fragment
   and are never fitted to readings (epic #2544 D17, D18):

   - **Cardinal source** — the target is where the ray enters that cell. Once
     the lattice is off, what remains is where the keyed micro-face starts
     against where the ray enters it: at most one micro-face, `2/effSub` depth
     units, so the bound is `(2/effSub)·√3/3` world — 0.289 at zoom 4, 0.144 at
     zoom 8 (`√3/3` is the world length of one yawed depth unit along the ray,
     at every yaw).
   - **Per-axis source** — the per-axis store keys a fragment by its face
     origin's yawed depth, which sits about the cell rather than on the
     surface, so the target is the crosshair-ray point level with the cell's
     center. With `(c, s) = (cos ψ, sin ψ)`, a face origin's yawed depth lies at
     most `½·(|c − s| + |c + s| + 1)` units from its cell center's, and the
     store quantizes depth to `1/effSub`. The bound is their sum times `√3/3` —
     0.933 / 0.861 at 30° and 0.841 / 0.769 at 45°, zoom 4 / 8. Which cell won
     the pixel is not the crosshair ray's alone: a line has no footprint, and a
     ray that clips a cell corner for a few hundredths of a depth unit names a
     cell a ray one pixel over never enters. So the target set is the first
     cell entered by each of nine rays — the crosshair ray and its eight
     neighbours one game pixel (`1/zoom` iso units) away — and a gesture passes
     within the bound of any of them. The bound is not widened; the demo prints
     the cell each gesture graded against and whether it was the crosshair
     ray's own (`neighbour_cell=`).
   - **Grazing** — when those nine rays disagree on whether the probe is hit
     at all, the gesture's hold/acquire classification is undecidable: it is
     reported (`result=SKIP skip=grazing`), not graded, the harness prints each
     block's skip count, and a block whose every gesture is skipped fails.
   - **SDF subject** — the `center-column` SDF twin (`--pivot-verify-sdf`)
     grades its cardinal gestures against the same surface entry and
     micro-face bound, which reads the SDF side of the subject branch. Its
     per-axis gestures are reported, not graded (`skip=sdf-per-axis`): the
     per-axis bound is derived from the voxel store's face origins.

   The `acquire-continuity` block pans the probe under the crosshair at yaw 0,
   22.5° and 180° and scores the frames straddling the acquisition with
   `jitter_probe --stationary`.

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
2. **Cursor (`Ctrl+Shift+middle-drag`).** `System<CAMERA_MOUSE_ROTATE>` picks
   the surface under the cursor at drag start (`IRPrefab::Picking::castVoxelRay`)
   and sets it as an explicit focus via `IRRender::setRotationPivotFocus`. A
   click over background sets no focus: the drag runs on the screen-center
   default, whose latch acquires the surface under the crosshair on the drag's
   first yaw-delta frame. The drag reverts to the default on release.

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
   live composite-depth readback against a geometric ray/surface target over
   the probe's own carve constants — per gesture since the rotation-scoped
   latch (§"Latch policy", Verification) — and `pivot-verify.py` fails the pass
   on any FAIL, or on a sweep block that moved the camera pan/zoom mid-sweep
   (`view_held=0`), reported as a misconfigured block rather than a pivot
   regression. Their silhouette deviation is still measured and reported, just
   not gated.

   Two blocks join them, both new in #2547:

   - `background-center` — the center pixel reads BACKGROUND, so the derive must
     take its `d = 0` fallback (Phase 3 acceptance criterion 2, amendment A1).
     Its probe stays rotationally symmetric about the pinned column, so the
     whole-silhouette oracle remains exact for it: PINNED at 0.91/1.21 px
     (zoom 4) and 0.94/1.25 px (zoom 8).
   - `center-axis` — the probe's axis passes through the point where the yaw-0
     viewport-center ray enters its near cap: the surface the default pivot
     acquires. The ray runs along `(1,1,1)` and meets the cap's face plane half
     a voxel short of the cap cells' centers, so the axis is half-integer in xy
     (an even 10×10 grid keeps every voxel on the integer lattice). Every
     cardinal acquisition lands back on that point, so the silhouette rotates
     onto itself, and the block is centroid-gated as well as focus-asserted.

   A third joins them in #2548, for the CURSOR pivot rather than the default:

   - `cursor-latch` — the integer-axis form of that probe (axis through the
     center of the near-cap voxel the ray enters), and the focus comes from
     `IRPrefab::CursorPivot::resolveFocusWorld` (the real `castVoxelRay` path)
     with a synthetic cursor parked on the viewport-center anchor's screen
     pixel, latched once and held for the sweep. Pinned-point oracle like
     `center-axis` (which is itself centroid-gated at a zoom-scaled bound);
     cursor-latch's silhouette is reported, not gated (16 px at zoom 4 on the
     2x host, 9 px on a 1x one — see the destination-grid note below; the
     same residual model plus the half-voxel offset below). It runs on the
     GUI-test cycler rather than the plain auto-screenshot one, because it needs
     scripted cursor input and a per-frame hook: the shot cycler clears the
     pivot focus at every shot boundary and the latched point is only known at
     runtime, so it cannot ride the shot table.

     Its tolerance is a whole world unit, and the reason is geometric, not
     slack: the cursor latch reports a `castVoxelRay` SURFACE hit
     — the marched point where the ray first lands inside the winning voxel's
     unit cube — while the analytic oracle predicts that voxel's CENTER. The L2
     gap is bounded by the cube's half-diagonal, `sqrt(3)/2 ~= 0.87`, and the
     bound is TIGHT here rather than pessimistic: the iso ray runs along
     (1,1,1), so it enters through the cell's near corner and the measured delta
     is exactly 0.87. The gate keeps ~8x of margin over the regression it
     exists to catch (a revert to the pre-#2548 iso-depth-0 latch lands ~8.5
     world units off on this geometry).

   **Residual: the composite depth is a store KEY, not a metric surface depth
   (#2641; corrected by #3169).** The derive consumes the key the composite
   sorts by. #2641 measured the cap blocks at a zoom-invariant 0.5773514 world
   units and attributed it to face spread — `emitDeformedFace` stamping one
   anchor depth across a face, a dead-centre cap crossing being half its 2-unit
   spread. That attribution was wrong. The 0.577 was the default pivot's own
   anchor sitting one iso unit per axis off the framebuffer texel its readback
   samples (the `trixelOriginOffsetZ1` canvas center, §"Pivot modes" 1): on a
   flat camera-facing cap that is exactly one depth unit, `√3/3` world, at
   every zoom. With the anchor on the sampled texel the yaw-0 cap blocks read
   0.000. (Epic #2544: D12 stands as recorded; ledger F16 carries the
   correction.)

   What the key is, per store (macOS/Metal, pivot-verify blocks at zoom 4 and
   8, one micro-face = `2/effSub` depth units):

   | store | key at the crosshair | how the pivot uses it |
   |---|---|---|
   | voxel, cardinal (residual yaw 0) | surface entry + 1.5 (lower-corner lattice), within one micro-face | latch subtracts 1.5; graded at one micro-face |
   | voxel, per-axis | the winning face origin's yawed depth, quantized to `1/effSub` | used as-is; graded at the derived bound |
   | SDF, cardinal | surface entry − one quantum, no lattice | the latch reads the winner's id and does not subtract; graded at one micro-face by the `center-column` SDF twin |

   The voxel and SDF stores disagree with each other by 1.5 at a subdivided
   cardinal, which is a sort-order defect of its own, independent of the pivot:
   #3742 (**open**). Until it lands the latch branches on the winning subject
   (`RenderManager::crosshairWinnerIsVoxelStore`), one entity-id read per
   cardinal gesture start; the change that co-sorts the two stores deletes that
   branch and subtracts for every winner, and the SDF twin's cardinal gestures
   are the gate it reads.

   `center-axis` is centroid-gated at a bound AFFINE in zoom — `1.5 px/zoom +
   1.0 px` of game resolution, scaled by the run's own `outputScaleFactor`
   (`CENTROID_BOUND_GAME_PX` in `scripts/pivot-verify.py`). Affine rather than
   proportional because a deviation carries two terms: a world-space orbit,
   which scales with zoom, and the one-game-pixel destination-grid floor
   (§"Not a deviation" below), which cannot. The bound was calibrated on the
   pre-#3169 probe, whose integer axis sat 0.71 world units off the acquired
   surface point, and is kept unchanged. On the half-integer probe the block
   reads 2.00 / 2.00 framebuffer px at zoom 4 and 8 on the 2x host (1.00 game px,
   the floor alone) against 14 / 26. A regression to the pre-#2547 iso-depth-0
   focus is still an order of magnitude outside every focus bound: 3.46 world
   units off on `center-column`, 12.1 on `center-depth`.

   **Cross-backend: CONFIRMED (#2641 criterion 4), under the pre-#3169 anchor
   and latch.** Re-measured on Windows/OpenGL (mingw64, `windows-debug`)
   against the same sweeps. The
   GL-only row flip in `readbackCompositeDepth` (`resolution.y - 1 - y`,
   `engine/render/src/ir_render.cpp:116`) does **not** perturb the derive: every
   derived focus is byte-identical to the Metal value — `background-center`
   `(12,-12,0)` / 0.0, `center-column` `(14.333333,-9.666667,2.333333)` /
   0.5773497, `center-axis` `(18.333334,-5.666666,6.333334)` / 0.5773514, all 9
   shots, `view_held=1` throughout. `center-depth` agrees at zoom 4
   (0.28867403) and lands on a different sub-voxel step at zoom 8 (0.4330127) —
   still under tolerance, and expected for the off-centre crossing described
   above rather than a backend disagreement.

   The **centroid** deviations are scored on the captured FRAMEBUFFER but land
   on a GAME-resolution quantum, so the raw readings differ by the host's
   `outputScaleFactor`: macOS renders the 1280x720 game resolution into a
   2560x1440 HiDPI framebuffer (factor 2), Windows into a 1280x720 one
   (factor 1). Same reason the SDF destination-grid floor above reads 1.00 px
   here against 2.00 px there, confirming that entry's own prediction. This is
   why the `center-axis` centroid gate is stated in **game-resolution px** and
   scaled by the run's measured `outputScaleFactor` (`CENTROID_BOUND_GAME_PX` /
   `_output_scale_factor` in `scripts/pivot-verify.py`): the game-px figures are
   directly comparable across hosts, so one calibration covers both.

   `center-axis` dev_x on the pre-#3169 integer-axis probe (the bound's
   calibration), in game px, by zoom:

   | host | 1 | 2 | 4 | 8 | 16 |
   |---|---|---|---|---|---|
   | macOS/Metal — 2026-08-21 (#2758) | 2.00 | 3.00 | 6.00 | 11.00 | 22.00 |
   | Windows/GL — 2026-09-06, post-#1938 | 1.00 | 2.00 | 4.00 | 10.00 | 20.00 |

   The two hosts read identically while both rows were taken pre-#1938; the GL
   analytic-coverage port (`fbad3ac4`) then moved GL's silhouette and left it
   uniformly **below** Metal. The Metal row is unchanged by that port by
   construction — its Metal-side diff is comment-only — but has not been
   re-measured since. The `1.5 px/zoom + 1.0 px` bound is calibrated on the
   larger (Metal) row and clears every GL cell by 25-150%, so one calibration
   still covers both; "identical on both" no longer does. Whether a faithful
   parity port should have converged the two exactly is open, needs a Metal
   host to answer, and nothing gates on the difference.

   **The GL divergence in `focus-ctr` / `focus-off` / `background-center` is
   RESOLVED — #3008.** When this section was written those three read
   1.73 / 1.73 / 1.59 px at zoom 4 on the 1x host against a scale-adjusted
   ~0.47 px predicted from Metal, and grew with zoom — a real GL-side
   silhouette difference rather than a unit artefact, exceeding the harness's
   default 1.5 px bound. `fbad3ac4` (#1938, the GL port of the analytic scatter
   coverage) fixed it, landing the day after this section did. Bisected on
   Windows/OpenGL 2026-09-06 with `scripts/pivot-verify.py`, the `jitter_probe`
   binary and `shape_debug` held byte-identical across all three arms, so the
   engine render code is the only variable:

   | engine tree | `focus-ctr` z4/z8 | `focus-off` z4/z8 | `background-center` z4/z8 |
   |---|---|---|---|
   | `7d236f28` — neither fix | 1.73 / 3.58 | 1.73 / 3.58 | 1.59 / 3.24 |
   | `ab494b21` — #3011 only | 1.73 / 3.58 | 1.73 / 3.58 | 1.59 / 3.24 |
   | `f3e79a54` — master | **0.47 / 0.48** | **0.47 / 0.48** | **0.46 / 0.47** |

   All three now PIN at the default 1.5 px bound on a GL host, matching the
   ~0.47 px the 2x-host figures predict, and `python3 scripts/pivot-verify.py
   --zoom 4 --zoom 8` exits 0 there. The pre-fix deviation was proportional to
   zoom (x2.07 across z4 -> z8) — the signature of the pre-#1938 GL scatter's
   continuous `0.5*|n|` coverage margin, which scaled with the on-screen cell
   pitch, not of a pivot-anchor error. #3011's fixed 1 px parity shift is
   visibly NOT the cause: it moved only `center-axis` (6.00 -> 4.00 at zoom 4)
   and left these three untouched. None of it was ever a #2641 residual, and
   nothing in this section depends on it.
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
   therefore gated at that floor plus the standard budget (`SDF_BOUND_GAME_PX`,
   #2851) rather than at `--max-deviation`; see §"Not a deviation: the SDF
   twin's flat 2.00px floor (#2645)" below.

Fix chain and acceptance gates: epic #2544 (P1 #2545 → P2 #2546 → P3 #2547 →
P4 #2548, cursor-pivot true-depth latch + indicator). Each child flips its
pivot-verify block(s) to its own gate — PINNED for the blocks whose probe
rotates about a point on its own axis, `[pivot-focus-assert]` FOCUS-OK for the
default-pivot blocks whose silhouette legitimately orbits (see deviation 2).
This section shrinks as they land.

### Not a deviation: the SDF twin's flat 2.00px floor (#2645)

`focus-ctr`'s SDF twin (`--pivot-verify-sdf`) draws a DRIFT verdict from
`jitter_probe` at 2.00px against the default 1.5px threshold, while the voxel
twin on the same explicit focus pins at ~1px. This is **not** a pivot defect
and no pivot fix can move it — it is the SDF path's rasterization quantum. So
the twin is gated at **its own floor-aware bound** rather than at
`--max-deviation`: `SDF_BOUND_GAME_PX` in `scripts/pivot-verify.py`, one whole
game-resolution pixel of floor plus the same 1.5px budget every gated voxel
pass gets (#2851). The bound is stated in game px and scaled by the run's own
`outputScaleFactor`, so the 2x and 1x host readings below are the same figure.

Between #2648 and #2851 the twin was **ungated** instead — dropped from the
exit code entirely. That over-shot: it made the twin the only pass in the
harness that could not fail at any deviation, so nothing machine-checked the
SDF path's pivot convention. Bounding the floor meets #2648's objective (the
harness's red means an open pivot defect, not this quantum) without the blind
spot.

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
fall under the 1.5px threshold on its own. A Linux/GL or Windows/GL re-measure
that reports ~1.00px is therefore agreeing with this entry, not contradicting
it — the bound is keyed on the floor being a floor, not on the specific
framebuffer number, which is why `SDF_BOUND_GAME_PX` is stated in game px and
multiplied by the run's measured `outputScaleFactor`.

The voxel twin has a lattice of its own to land on: its cells sit on exact
integer world positions, so its silhouette re-forms identically at each yaw and
it pins sub-pixel. The SDF twin is a *continuous* solved surface with no such
lattice, so as it rotates its silhouette edge crosses destination-pixel
boundaries and the centroid steps by the one-pixel quantum. That is the
sampling floor of rasterizing continuous geometry to a fixed grid: you cannot
pin a continuously-moving analytic silhouette better than the destination pixel.

This agrees with the independent #2469 measurement in
[`tools/jitter_probe/README.md`](../../tools/jitter_probe/README.md) §"Pinned-probe
bars and residual floors", which records the same 2.00px x-excursion for the
SDF cylinder at zoom 4/8 under the unrelated `--yaw-sweep` harness and already
treats the SDF twin as the *defect-free control* whose residual is "a floor the
probe itself carries". Two harnesses, one number.

Closing it would mean resolving the silhouette below one destination pixel —
supersampling / conservative rasterization on the SDF path, which is the same
principled root fix already deferred to epic **#1933** for the #1883 corner
drift and the #2469 centroid residual. It is not reachable by any change to the
pivot math, which is what this harness exists to gate. So the twin keeps
running with the A/B against the voxel path as its diagnostic, and its gate is
raised past the floor rather than removed — the exit-code contribution stays,
which is what still catches an SDF-side pivot regression (#2851).

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
- #2547 / PR #2585 — the default pivot became depth-aware: the iso DEPTH under
  the crosshair is latched (the point stays live), re-derived while the camera
  is settled and pan or zoom moved.
- #2669 — contract amendment (architect ruling 2026-08-05, option 2): the latch
  also re-derives at **rotation start**. Implemented unrestricted, the edge
  walked the pivot at non-zero yaw (`pivot-verify.py` 16/16 -> 10/16, measured
  2026-09-10); the 2026-09-16 ruling (epic #2544 D12/D13) scoped it to
  rotations starting from **yaw 0** as the interim contract and made #3169 the
  carrier of the frame fix. The policy moved out of `RenderManager` into
  `IRRender::DefaultPivotLatch` so it is machine-gated headlessly
  (`test/render/default_pivot_latch_test.cpp`) — epic #2544 Finding F3. The
  pan-settle pop is unchanged.
- #3169 — the rotation-scoped latch (architect ruling D16, superseding D13 and
  D15's interim yaw-0 gate). Under the interim contract the depth was derived on
  the first still frame after every pan/zoom and on rotation starts from yaw 0
  only, and the focus `isoPixelToPos3D(viewCenterIso, depth)` had no yaw term:
  a depth read off a frame drawn at non-zero yaw pinned a point off the
  crosshair, so an unrestricted rotation-start derive walked the pivot into the
  background (`pivot-verify.py` 16/16 -> 10/16), and the pan-settle derive
  re-latched after every pan, shifting the view at non-zero yaw by up to `4h`
  iso at 180° (measured by `docs/design/detached-camera-placement.md`: 17.49 px
  at yaw 22.5°, zoom 16). Carrying the source yaw and effective camera into the
  recovery, with the residue kept as a view offset, made every rotation start
  sound at any yaw, and dropping the pan/zoom derive removed the pop. The
  sweep's sweep-wide constant-focus check became the per-gesture oracle.
- #3169, D17 / D18 — grading that oracle away from yaw 0. A zoom-invariant
  miss at cardinal sources traced to the canvas-center anchor sitting one iso
  unit off the sampled texel (the old 0.577 "face spread"), then to the
  cardinal store's 1.5 lattice. The latch now removes the lattice at cardinal
  sources and is graded against the ray's surface entry at one micro-face;
  per-axis sources are graded at a bound derived from the face-origin offset,
  and grazing gestures are reported, not graded. `center-axis` moved onto the
  acquired surface point. The SDF store keys without the lattice (#3742).
- #3169, D19 — the SDF store's missing lattice is answered by a subject branch:
  the latch reads the winning entity id next to the depth and subtracts only
  for a voxel-store winner, and an SDF `center-column` twin grades it. A
  per-axis reading of 1.051 against a 0.841 bound traced to the crosshair ray
  clipping a cell corner for 0.040 depth units; per-axis sources are now graded
  against the first cells of the pixel's nine footprint rays.
