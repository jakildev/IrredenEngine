# Fog-of-war reveal model

**Status:** Normative design contract. The implementation is split across
#3675–#3677 and #3679–#3680, with the reveal-surface ceiling and treatment in
#4055, the column-top reveal in #4304, and the geometric ceiling cut in
#4305; the mapping table names the child that makes each part live.
Until a child lands, its row describes the target behavior rather than the
current render path.

Fog answers one question for every rendered subject: is this part of the
world field, one discrete body, or outside fog? The answer determines where
the reveal verdict is evaluated and whether fog may vary across the rendered
geometry. Screen-composited sprites bypass the trixel fog pipeline and are
therefore implicitly EXEMPT.

## Subject classes

| Class | Reveal verdict | Intended for |
|---|---|---|
| **FIELD** | Per sample against the reveal field, including height terms. Voxel matter above the ceiling is absent; retained matter and analytic shapes receive fog paint. | Terrain and terrain-scale static geometry: the surface on which fog is drawn. |
| **BODY** | Once per entity at its ground anchor. Every rendered sample uses the resulting `C_FogRevealed::revealFactor_`. | Every discrete entity: creatures, units, items, props, and placed objects. |
| **EXEMPT** | Always visible; the fog field is not applied. | Screen-composited sprites, cursors, markers, overlays, and anything else the creation explicitly nominates. |

BODY is the fallback. An untagged renderable on a fogged world canvas is
adopted as a BODY; FIELD and EXEMPT are explicit classifications.

## Invariants

### A BODY is never sliced

A BODY shows, hides, or fades as one unit. The engine evaluates the reveal
field once at the body's ground anchor, including the height cost and the
source ceiling at that anchor, then applies the one factor to every pixel. No
BODY pixel performs a second field lookup, height clip, or rim fade. A body on
higher ground may therefore remain hidden until its anchor is revealed, but
once revealed its entire geometry is visible at the same factor.

### FIELD matter is absent above the ceiling

**Landing child:** #4305. This is the target contract; geometric selection
and animation are not implemented by the docs-first change.

A FIELD voxel below the ceiling is painted; above the ceiling it is absent.
The partial ceiling band is a geometric dissolve: rejected voxels are absent,
and retained voxels expose cap faces where their neighbours were removed.
Only the ceiling term removes matter. A line-of-sight cut remains painted,
so an occluder's shadow does not detach the revealed part of a wall from its
base. BODY and EXEMPT keep their existing contracts. Analytic FIELD shapes
keep their painted ceiling and dissolve until a separate shape-cut design
provides a capped surface.

Selection composes the admitted sources conservatively with the column-top
verdict. A column revealed through an unceilinged source at its top remains
whole even when a source at its foot has a ceiling. VISIBLE grid cells keep
their matter, and columns reached by no analytic source keep theirs too.

The silhouette trade-off is intentional: a cut column ends at its cap and
reveals the ground behind it where that ground is revealed. Painting a tall
column to its full height would disclose the height the ceiling hides.
Geometry selection, rather than a pixel discard, must expose the cut faces;
otherwise the authored neighbour mask suppresses the top and leaves a hollow
column.

The separate compact and stage-1 keep-ring culls still drop only unexplored
columns outside every source's keep radius. Their radial policy is unchanged.

## Engine mapping

The issue in the last column owns the transition from the current path to
this contract.

| Concern | Engine representation and route | Landing child |
|---|---|---|
| Classification | `C_FogField` and `C_FogExempt` are explicit marker components. `C_FogRevealed` holds BODY verdict state. `FogSubjectClass`, `IRPrefab::Fog::setSubjectClass`, `subjectClass`, and `revealSystems` are the public C++ surface. | #3676 (P3), extended to shapes by #3677 (P4) and detached canvases by #3680 (P6) |
| Default adoption | `FOG_SUBJECT_ADOPT` classifies an untagged voxel set on the active fog canvas as BODY within one frame. Shape and detached-canvas variants classify their respective owner entities. | #3676 (P3), #3677 (P4), #3680 (P6) |
| Shared verdict | `IRPrefab::Fog::evalReveal` takes the maximum of a grid-VISIBLE cell (1.0, regardless of channels) and the matching-circle term; an EXPLORED cell contributes no BODY reveal. BODY evaluators apply the result with `C_FogRevealSettings` hysteresis and write `C_FogRevealed::revealFactor_`. | #3676 (P3), reused by #3677 (P4), #3679 (P5), and #3680 (P6) |
| Entity-id carrier | High-word bit 28 says the pixel is BODY-classed; bits 27:20 carry its quantized 8-bit reveal factor. All entity-id readers strip these carrier bits before decoding the entity. | #3676 (P3), with the shape fold in #3677 (P4) |
| Voxel carrier source | `C_Voxel::reserved_` bit 3 carries the BODY class and bits 11:4 carry the factor. Stage 2 folds them into entity-id bit 28 and bits 27:20. | #3676 (P3) |
| Shape carrier source | `C_ShapeDescriptor` uses `SHAPE_FLAG_FOG_BODY` for the class and reserves GPU descriptor `flags` bits 23:16 for the factor before the shape raster folds both into the entity id. | #3677 (P4) |
| FIELD paint | `FOG_TO_TRIXEL` samples the field and paints retained matter toward the per-canvas unexplored color: per pixel on the world canvas, and per occupied face sample plus overflow entry on the per-axis rotation route. The radial keep ring remains z-free. | #3675 (P2), #3710 (per-axis) |
| FIELD ceiling selection | A shared ceiling-presence and retained-set definition selects voxels and exposes neighbour cut faces on every voxel raster route. The sun bake uses the phase-zero set. | #4305 (pending) |
| Voxel BODY apply route | Hidden voxel bodies are removed through the pool active mask. Shown pixels decode the BODY factor in `FOG_TO_TRIXEL`, skip grid, height, rim, and cut-cap evaluation, and apply one uniform result. | #3676 (P3) |
| Shape BODY apply route | `SHAPE_FLAG_FOG_HIDDEN` suppresses a hidden shape before raster work, independently of the author's `SHAPE_FLAG_VISIBLE`. Shown pixels carry and use the uniform BODY factor. | #3677 (P4) |
| Detached-canvas BODY apply route | The canvas owner carries `fogHidden_` and `fogRevealFactor_`. `ENTITY_CANVAS_TO_FRAMEBUFFER` suppresses a hidden canvas or applies the uniform factor to the whole composite; private-pool voxels carry the BODY exemption. A fog-governed BODY voxel set matches `shown_` while it is on the active fog canvas and keeps `visible_` open in any other pool, where the owning canvas carries the hide. The reveal evaluators repair both gates on the first UPDATE tick after a pool move. | #3680 (P6) |
| EXEMPT apply routes | For a voxel set, explicit EXEMPT classification stamps factor 255 without `C_FogRevealed`. Shape and detached-canvas EXEMPT bypasses require explicit raster-route realization; exclusion from BODY adoption alone is insufficient. | #3676 (P3) for voxel sets; #3696 for shapes and detached canvases |
| Reveal surface | A per-source ceiling (`setVisionCircleCeiling`) multiplies with `losVisibility` into one surface factor per source before the maximum over sources. A canvas-owned style (`setRevealSurfaceTreatment`) marks a partial winning cut on FIELD samples in `FOG_TO_TRIXEL` and the overflow lane. [Reveal-surface treatment](#reveal-surface-treatment) is the contract. | #4055 |
| Column top | A FIELD sample on a vertical face takes the larger of its own reveal and the reveal at the top plane of its own column (`fogColumnTopState` in the paint, `IRPrefab::Fog::evalRevealSurface` with a column point on the CPU). [Column top](#column-top) is the contract. | #4304 |
| Lua FIELD surface read | `IRFog.evalRevealSurface(x, y, z [, channels])` returns `state, band` of the position-only FIELD verdict (`IRPrefab::Fog::evalActiveRevealSurface`, the term-off arm). A point exactly on a negative-side face plane lies in the matter's half-cell and reads its column top's verdict, so a caller reading the arm a face sample compares against steps the point `kFogLosFaceBias` outward first. | #4329 |
| Lua FIELD face read | `IRFog.evalRevealFace(x, y, z, face [, channels])` returns `state, band` of the verdict with the column-top term for a sample on face `face` (`IRFog.Face.X_NEG` through `Z_POS`) whose point on the face plane is `(x, y, z)`. `IRPrefab::Fog::evalActiveRevealFace` steps that point outward for the sample's own march target and inward for its column point (`losColumnPoint`). | #4329 |
| Creation seams | Override and source/BODY channel data live with `C_FogRevealed` and feed every BODY evaluator; the FIELD paint admits a source only where its mask intersects the sampled cell's mask (the world field's per-cell channels), on both source tiers. | #3679 (P5), per-cell masks #3687 |

Only FIELD matter and shapes with `C_LightBlocker::blocksLOS_` occlude the
reveal field; BODY matter does not. BODY pixels skip the per-pixel LOS gate
because their anchor verdict owns visibility (#3662).

The gate is exact at every sample. `FOG_LOS_BUILD` rasterizes the occluders
into one column field on a half-cell lattice, each column holding the top
plane of the box the raster draws its voxel or shape as, and every FIELD
pixel marches the segment from a source's eye to its own recovered world
position through that lattice, on every paint route (the cardinal canvas, the
smooth-yaw canvas, the per-axis canvases and the overflow lane). A column
blocks when the segment's lowest point over its footprint sits at or below
the column's top plane, so a wall's shadow ends on the straight line the
wall's edge projects to, a wall top seen from below and a face seen from
behind hide without any per-face rule, and the same march evaluated at an
entity's ground anchor is the BODY verdict. The march is hierarchical:
`FOG_LOS_BUILD` also stacks a pyramid of the field (per block, its highest
top plane), and the walk steps over any block whose highest top cannot change
the verdict, so open ground costs a few block tests instead of a cell per
half-cell crossed, and a pixel and an entity reach the flat march's verdict
at a fraction of its cost. This is the sun shadow's
receiver model applied to a point eye: an exact geometric test against the
drawn geometry at each pixel's continuous position, with a normal bias that
keeps a face out of its own column, instead of a per-cell verdict
interpolated across the lattice. A source's optional softness grades the
verdict over the segment's clearance above the occluder it passes, which
fades the far edge of a plateau's shadow; the flank of a shadow, where the
segment leaves an occluder's footprint, stays a crisp line. The model is
stated in `component_canvas_fog_of_war.hpp`; `IRPrefab::Fog::losVisibility`
is the CPU oracle, and `ir_fog_los.{glsl,metal}` carry the same march.
Both evaluators march only where the march can change their result. The CPU
oracle marches gated sources strongest first, and only while one could still
raise the maximum. The paint pass stops evaluating sources once a sample is
fully revealed, and skips a gated source that can neither raise the reveal
nor move a rim distance the colour reads, so ground the grid already reveals
costs the paint pass no march.

### Shared hard routes (CPU)

A hard gate's route depends only on the eye and the target's exact XY: the
cells crossed, the segment parameters where it enters and leaves each, and the
target column's top plane. Only the target's height varies between bodies
stacked on one XY, and within each slope regime (rise above the eye's height,
tested at a column's exit; or not, tested at its entry) every cell's test is
monotone in that rise. `IRPrefab::Fog::buildLosHardRoute` therefore walks a
route once and keeps, per regime, a band: a rise below it is visible, one at
or above it hidden, and one inside it marches. Each band edge is proven by
evaluating the march's own expression (`losSegmentClearance`, one inline
helper, so a fused multiply-add rounds alike on both sides), never by
rearranging it, so every verdict the summary gives is the march's bit for bit
on every backend's compiler. The walk is the march's own pyramid walk, entering
only blocks that could block at a regime's extreme rise.

Every continuous BODY evaluator owns an `IRPrefab::Fog::LosHardRouteCache` and
binds it to its tick's publication in `beginTick`. All workers of the
`PARALLEL_FOR` fan-out share it: the first lookup of a (source, exact XY)
claims a slot and builds the route, a lookup that finds it still building
waits, and every other lookup reads it. A per-worker memo would build each
route once per worker, because the Z-major row order spreads one stack over
every worker's ranges. Slots are stamped with the tick's generation, so a new
tick forgets every route without touching them, and four consecutive X cells
share a cache line, the order a worker reads them in. Soft and ungated sources,
an unpublished field, a lane whose probe runs out and a rise inside a band all
take `losVisibility`. Only `begin` allocates, sizing each lane from the last
tick's demand. `C_FogRevealSettings::staggerPeriod_` stays the knob for
populations past what this sharing absorbs.

The per-pixel FIELD march has no such sharing: each pixel's exact XY is its
own. Its cost scales with the blocks a segment crosses, not with a texel read,
so its budget is priced against the march it runs. For an identical fixture
and a same-session matched merge base, let `M` be the base's `fogToTrixel`
delta between line of sight on and off; the gate is `LOS-on <= control +
max(10 µs, ceil_to_10us(1.15 * M))` (#3878). A fixture whose `M` rounds to
zero runs no FIELD march the stage's timing resolves, so the floor is the
whole allowance, and a pass proves that the stage did not grow, not that the
change's march cost is bounded. A fixture that changes the marched pixels,
the sources or the blocks crossed measures its own `M`. Any future criterion
for a GPU stage row follows the general noise-aware per-stage rule in
[gpu-stage-timing-cost-model.md](gpu-stage-timing-cost-model.md#state-the-noise-bound).

### Unpainted-route FIELD deviations

A world-placed detached canvas has no `FOG_TO_TRIXEL` paint pass. When its
owner is explicitly FIELD, the detached route therefore retains the z-aware
geometric clip as a documented deviation. An untagged detached canvas is a
BODY instead: the verdict is evaluated at the world-space owner, its private
pool is exempt from that clip, and the result is applied at composite time
(#3680).

This exception does not create a fourth subject class. It is the closest
available application of the FIELD verdict on a route that cannot paint.
The geometric ceiling treatment also applies here: its retained set combines
with the route's height-cost clip. The route has no fog paint for a cap tint.

`fogColumnRevealZ` supplies only the per-source height-cost radial curve
(`zCostUp`, `zCostDown`, `freeBand`); it does not multiply by the ceiling.
The drop is `heightClip(col, z) <= 0 || !Retained(vWorld, phase)`, so a
static ceiling veto cannot discard a voxel that the animated cut retains.
The height-cost maximum and the admitted ceiling maximum are separate: a
cost-excluded source with its ceiling disabled can keep the ceiling term
open where a different source supplies the height-cost reveal. This accepted
over-retention keeps the ceiling decision shared with other raster routes
and the bake.

## Reveal-surface treatment

**Landing children:** #4055 (painted treatment, landed), #4305 (geometric
voxel ceiling and phase, pending). The geometric clauses below describe the
target behavior until #4305 lands.

A source stops revealing a sample for one of three reasons: the sample is out
of XY range, it is above the source's ceiling, or an occluder blocks the
source's line of sight to it. The last two cut through matter that is
otherwise in range, and together they form the source's *reveal surface*.
This section defines the ceiling, how the two cuts combine into one factor
per source, and the opt-in treatment that marks a partial cut on FIELD matter
so that it reads differently from unexplored ground beyond the range.

### Ceiling

Each analytic source carries a `ceilingHeight` and a `fadeHeight`, both in
world units measured upward from the source's `observerZ`. Iso +Z points
down, so a higher sample has a smaller Z, and a sample's height above the
observer is

```
dzUp = max(observerZ - sampleZ, 0)
```

`sampleZ` is the position the additive height terms already read: the
recovered world position of a FIELD sample, or the ground anchor of a BODY. A
sample at or below the observer has `dzUp = 0`, so no enabled ceiling hides
it.

| Authoring | `ceilingVisibility` |
|---|---|
| `ceilingHeight < 0` (the default) | 1 everywhere. The ceiling is disabled. |
| `ceilingHeight >= 0` and `fadeHeight == 0` | 1 where `dzUp <= ceilingHeight`, otherwise 0. The plane itself is visible and the first sample above it is hidden. |
| `ceilingHeight >= 0` and `fadeHeight > 0` | `1 - smoothstep(ceilingHeight, ceilingHeight + fadeHeight, dzUp)`: 1 up to and including the plane, 0 at and beyond `ceilingHeight + fadeHeight`. |

`ceilingVisibility` depends on `dzUp` alone. A column at the disc's centre
and one at its rim hide at the same height above the observer, and a fade of
`H` is fully hidden at exactly `ceilingHeight + H` in both.

The additive height terms (`zCostUp`, `zCostDown`, `freeBand`) cannot express
that shape, because they add to the radial distance. A source with a
positive `zCostUp`, radius `radius` and edge `edge` fully hides a sample at
XY distance `d` once it is `freeBand + (radius + edge - d) / zCostUp` above
the observer, a height that differs between the centre and the rim for every
finite cost. Those terms are unchanged and remain available; a source may
author them together with a ceiling.

A ceiling needs an analytic slot. A field-tier source, one added past
`kMaxFogVisionCircles`, has no ceiling, as it has no height terms and no line
of sight.

### Composition

For each analytic source whose channel mask admits the sample:

```
shapeReveal       = the radial curve with its additive height terms
surfaceVisibility = ceilingVisibility * losVisibility
contribution      = shapeReveal * surfaceVisibility
```

`losVisibility` is the exact march's factor, softness included, for a source
gated by line of sight, and 1 for an ungated source. The reveal of a sample
is the maximum of the world-field grid term and every source's contribution.

Both factors scale their own source before the maximum is taken. A source cut
off by its ceiling or by an occluder therefore never lowers what another
source or the grid reveals, and each ceiling is measured from its own
source's `observerZ`. The grid term is an independent input to the maximum:
no ceiling and no line-of-sight factor scales it.

The CPU oracle (`IRPrefab::Fog::evalReveal`) and the GLSL and Metal FIELD
paint evaluate this one definition.

### Sun-shadow caster ceiling

The sun-shadow predicate uses the same retained set as voxel selection with
phase forced to zero. A statically absent or dissolved FIELD voxel casts
nothing. BODY casters keep their whole-body rule. No enabled source ceiling
makes the predicate an identity path.

With treatment disabled, a soft ceiling retains every voxel centre strictly
below `ceilingHeight + fadeHeight`; a hard ceiling includes the plane itself.
With treatment enabled, the density and voxel hash determine the static band
casters. The midpoint of the fade is not a separate shadow plane.

Voxel-face, box and analytic-shape bakes evaluate the static predicate
directly. Analytic FIELD shapes retain their painted raster rule, but their
caster surface samples use the same phase-zero voxel key. Legacy canvas and
world-placed resolves can only cast samples that survived the frame's raster;
their result is that raster intersected with the static retained set. The
per-axis legacy resolve inherits the raster cut. Consequently nonzero phase
can affect these raster-derived shadow routes even though the predicate reads
phase zero; reference captures pin phase zero.

The retained predicate is a pure function of the observers, the world field's
cell at the voxel's XY (state and channel mask), and the integer world voxel.
It has no route or camera input. The cell is an explicit argument to the
shared presence, retention and shadow helpers; the shadow include owns no
texture binding. BODY, disabled-driver and no-source early returns precede
the caller's cell sample, so the no-ceiling path reads no fog texture.

The caster cell comes from the main world canvas's `C_CanvasFogOfWar` window,
with the origin published by `gatherFogWindow` before the shadow passes.
`bindFogObservers` binds both the observer block and that window before each
bake dispatch; the shape caster pass binds the same inputs through its bake
preparation. Each kernel declares a free image unit and matching Metal texture
index, and the next dispatch rebinds any shared slot it owns. Window addressing
uses one `fogWindowTexel` definition per dialect in `ir_iso_common`.

Outside the published window the cell is `(kFogStateUnexplored,
kFogChannelDefault)`, matching the paint's fallback and the CPU's default
`FogWindowTexel{}`. Thus a caster beyond the covered radius cannot observe a
VISIBLE cell stored outside the window; the window coverage and shadow sweep
bounds must contain every sample whose grid override matters.

The all-visible placeholder is bound only to complete the argument table when
there is no main fog component; the driver disables the ceiling predicate
before any sample in that case. The world-placed detached compact does not
apply the ceiling envelope through its placeholder. Its stage-1 retained
test uses the real main window through `cutSectionFog`, and the world-placed
depth resolve binds that same window. A placeholder never supplies a VISIBLE
override to ceiling selection or casting.

### Ceiling presence and voxel selection

The shared helpers `fogCeilingPresence(v, cell)` /
`fogCeilingRetained(v, cell, phase)` and their CPU twins
`IRPrefab::Fog::detail::ceilingPresence` / `ceilingRetained` take the observers,
the sampled world-field cell and the integer world voxel, independently of
the camera. Callers supply their cell through their own texture binding.

An analytic source is admitted when its channel mask intersects the cell's
mask and its disc reaches the nearest point of the voxel's closed unit cell.
Admission uses `max(sourceSoftness, kFogCeilingAdmitAa)`, with
`kFogCeilingAdmitAa = 0.75`. The widest paint AA floor is `sqrt(2)/2` world
units; subdivision only reduces it. The fixed admission bound therefore
contains every source that can contribute to the column-top verdict without
requiring a column-field read in selection or the bake.

Presence `C(v)` is 1 for a VISIBLE grid cell or an empty admitted set;
otherwise it is the maximum ceiling visibility at the voxel centre over the
admitted sources. EXPLORED memory, height costs and line of sight do not
enter this maximum. At phase zero, presence at a voxel at or below its column
top is at least that top's reveal contribution.

With the treatment enabled, `C'(v, phase)` evaluates each admitted ceiling
after a height perturbation of at most `kFogCeilingFlickerSpan = 2` world
units. A fixed-width hash of the voxel selects a triangle-wave rate; the wave
uses `fract` and `abs`, has range [-1, 1], and is exactly zero at phase zero.
An explicit phase-zero branch reads unperturbed presence. Painted state and
band weight always read unperturbed ceiling terms.

```
Retained(v, phase) = C'(v, phase) > 0
                    && (C'(v, phase) >= 1
                        || hash01(v) <= mix(1, C'(v, phase), density))
```

Density is zero while the treatment is disabled. Selection treats a rejected
neighbour as empty on every face axis, including the top face. BODY voxels
bypass both the ceiling drop and the neighbour cut test.

The compact drops only when the conservative envelope `Cenv(v)` is zero.
It uses the same admitted set, VISIBLE-cell override and empty-set rule as
`C`, but evaluates each admitted ceiling at `dzUp - span`, with
`span = kFogCeilingFlickerSpan` while treatment is enabled and zero otherwise.
This test is hash-free and phase-free; the compact's observer block extends
through the treatment lane. With treatment disabled, `Cenv == C` exactly.

Because the wave is at least -1 and ceiling visibility is non-increasing in
height, `C'(v, phase) <= Cenv(v)` for every phase. Thus every voxel the
compact drops fails `Retained` at every phase, and every possible survivor
reaches stage 1. Stage 1 and face selection own the final retained decision;
a dropped neighbour cannot suppress an exposed cap. Negative perturbed
heights need no clamp: they are below any enabled ceiling and read as 1.

The compact applies this ceiling test only on the world fog canvas
(`imageSize(canvasFogOfWar).x > 1`). World-placed revoxelization keeps its
placeholder because compact uses un-offset model columns; stage 1 alone
cuts that route at its world voxel. The envelope can over-admit at most
`span` world units above the static cut while a ceiling and treatment are
enabled; stage 1 removes the excess before emitting geometry.

Whether any source has a ceiling is a per-frame fact, and the kernels learn
it from the CPU rather than scanning the sources per voxel or per face.
`VOXEL_TO_TRIXEL_STAGE_1` evaluates `anyCeilingEnabled` once per canvas: the
compact is two compile-time specializations of one body
(`c_voxel_visibility_compact`, no ceiling code, and
`c_voxel_visibility_compact_ceiling`, selected by `IR_FOG_CEILING_PASS`), and
the ceiling kernel is dispatched only when the canvas's own fog window has a
ceiling-enabled source live; stage 1 and stage 2 read the same verdict as the
`fogCeilingEnabled` lane of their frame data, computed over the observers
they select against (the main window for a world-placed detached canvas).
A canvas with no ceiling therefore runs master's compact kernel, and a FIELD
voxel on such a frame pays one uniform read in face selection.

In split mode, every voxel and axis whose final face-selection test emits a
cut or cap at any phase must reach that axis's region, including fully
interior authored voxels. Region admission reads the lower envelope
`Cmin(q)`, which evaluates each admitted ceiling at `dzUp + span` with the
same admission, cell overrides and span as `Cenv`. Ceiling monotonicity and
the wave's bounds give `Cmin(q) <= C'(q, phase) <= Cenv(q)`. A rejected
neighbour has `C' < 1`, so `Cmin < 1` is necessary for possible rejection;
the upper envelope cannot answer that question.

While a ceiling-enabled source is live on the world fog canvas, a surviving
FIELD voxel joins axis region `a` when an authored face on that axis is
exposed, or any of `Cmin(v)`, `Cmin(v + n_a)` and `Cmin(v - n_a)` is below 1.
Face selection supplies the exact retained verdict after this conservative
append. The own-voxel term is redundant for correctness but remains part of
the predicate. BODY voxels keep their exposed-face append. With treatment
disabled, `span = 0` and `Cmin == C`; the same `< 1` test harmlessly
over-admits the whole-retained soft band. With no ceiling-enabled source,
every ceiling reads 1 and the append is unchanged.

Every neighbour term reads that neighbour's column cell and nearest-cell
admission, using the caller's binding and the UNEXPLORED/default-channel
off-window rule. Z neighbours share the owner's column and admitted set;
X/Y neighbours require four extra admissions per surviving FIELD voxel.
The same neighbour-cell rule applies to face selection, cap paint and the
static shadow neighbour test.

### Treatment

With the treatment disabled, the ceiling band remains whole and matter above
it is absent. Retained samples blend toward the unexplored colour by their
reveal. The treatment adds geometric ceiling dissolve and animation, painted
line-of-sight dissolve, and a cap tone.

**Height-cost authoring.** Voxel ceiling selection reads the ceiling alone:
`fogCeilingRetained` through `fogCeilingPresence`
(`engine/render/src/shaders/ir_iso_common.glsl::fogCeilingPresence`, lines
1252–1288) admits a source by its XY disc and applies
`fogCeilingVisibility`; `visionCircleHeights` supplies only `observerZ` to
that selection. FIELD paint instead includes the additive height cost before
the radial curve (`engine/render/src/shaders/ir_fog_common.glsl::fogRevealSample`,
lines 211–225). If that height-cost radial curve reaches zero inside the
ceiling fade band, retained band matter has zero contribution: an unrevealed
grid cell paints unexplored with no band tone. Author a cut with the ceiling
alone. A source that also keeps a height cost must leave the band alone by
placing its free band at or beyond
`ceilingHeight + fadeHeight`.

The style belongs to the canvas: one `dissolveDensity` and one `capTone`,
each in [0, 1], plus a finite creation-supplied phase. The ceiling and fade
stay per source. Setting or clearing the treatment controls whether density
and phase participate in selection.

A source's contribution styles the painted band only when both conditions hold:

1. The contribution is partial: `0 < surfaceVisibility < 1`.
2. The contribution wins. It raises the sample's reveal above the grid term
   and above every other source's contribution.

A fully visible competing source or a VISIBLE grid cell therefore suppresses
the treatment, because the partial contribution no longer decides the sample.
Their product supplies the painted band weight. Among contributions that
tie, the one with the larger band weight (see [Cap tone](#cap-tone)) styles
the sample. Geometric selection uses ceiling presence separately, so line
of sight cannot remove matter.

A hard ceiling has no static partial band, but still exposes a geometric
cap. A nonzero phase can move its geometric boundary within the flicker span.

### Dissolve

Ceiling dissolve is the voxel-selection decision above. Each world voxel
has one fixed hash value in [0, 1]. A retained voxel's pixels do not repeat
the ceiling rejection using their recovered face positions. Their painted
dissolve reads the line-of-sight factor alone:

```
hash <= mix(1, losVisibility, dissolveDensity)
```

Analytic FIELD pixels keep the product `surfaceVisibility` in this test,
because their ceiling remains painted. At density zero the painted dissolve
rejects nothing; at density one it compares directly with the selected
factor. A line-of-sight-only band is byte-identical to its painted rule.

The source of a rejected fragment contributes nothing to that sample. The
sample falls back to the maximum of the grid term and the remaining sources,
which is the unexplored colour when nothing else reveals it. Its geometry is
still drawn.

The hash is integer arithmetic over the sample's integer world voxel, built
from fixed-width xor and multiply steps and written identically in GLSL and
Metal. Nothing else enters it: no frame counter, no camera or yaw term, and
no floating-point transcendental. A voxel makes the same decision on every
frame at a fixed phase, at every camera yaw and on both backends.

### Cap tone

A geometric cut face carries the existing entity-id bit 29. A FIELD voxel
pixel with that bit takes full cap-tone weight when its outward neighbour
fails `Retained` at the frame's phase. Its owning voxel is recovered from
`roundHalfUp(facePoint - 0.5 * outwardNormal)` using the route's face-plane
point, not the raw cardinal pixel recovery. This test precedes the paint's
fully-revealed early return. The stored tone applies with treatment on or
off, defaulting to `kFogCutTone`.

Overflow cut provenance uses **word 0, bit 31**, with x in bits 15:0 and y
in 30:16. `PerAxisCanvasStore::allocate` asserts a canvas height below 32768
cells. `ir_iso_common.{glsl,metal}` owns `kOverflowCutFaceBit`,
`packOverflowEntryCell`, `overflowEntryCell` and `overflowEntryIsCutFace`;
the append packs the selection verdict, and every consumer masks the cell
through the shared helper. Words 1 and 2 and the three-word stride are
unchanged, preserving the full signed 17-bit depth and BODY class byte.
The sort orders `(masked cell, encoded distance, colour, cut flag)`: the
flag breaks only otherwise identical ties, so the key covers every bit and
strict compare-exchange remains deterministic. The all-ones sentinel sorts
last because no real cell reaches x=65535. Only the fog overflow pass reads
the flag for cap tone; scatter and lighting preserve their cell recovery.

Rejected carriers: a fourth word increases bandwidth and shared-memory sort
cost on every rotating frame; word 2 has no free bit without narrowing the
signed depth domain; the class byte has no free value without requantizing
BODY; dropping provenance would require the paint to recover authored face
exposure it cannot read, or make the store own the cap paint rule.

A retained fragment of the styled contribution is blended toward its source
colour multiplied by `capTone`, on every face axis. The weight of that blend,
the *band weight*, is zero where `surfaceVisibility` is 0 or 1 and peaks at
the midpoint of the band, so the cap meets the untreated colour at both
edges. This document fixes those three points. The implementation child
fixes the curve between them and pins it across GLSL and Metal.

The blend is the helper the radial rim cap uses, so a ceiling cut, a soft
line-of-sight edge and a disc rim share one cap language. The radial rim cap
is unchanged in where it applies: hard discs only, unexplored grid cells
only, vertical faces only, and within two cells past the rim.

The radial rim cap's tone is also unchanged: it blends with the fixed 0.85
constant whether or not the treatment is enabled. The stored `capTone`
tones geometric ceiling caps and, while enabled, the painted partial band;
hard-disc rims retain 0.85.

### Column top

**Landing child:** #4304.

A source that reveals a ground surface reveals the matter under it. Without
this rule a wall can carry an unrevealed band that conveys nothing: a source
standing on a plateau reveals its top but not its walls (the march to a wall
passes under the top plane), and a source at the foot reveals the walls only
up to its ceiling. The observer sees the top and the base, and the band
between them stays unexplored.

A FIELD sample on a vertical face therefore takes the larger of its own
reveal and the reveal at the top plane of the column it belongs to:

```
own   = the composition, treatment included, evaluated at the sample
p     = facePoint - kFogLosFaceBias * n
topZ  = the column field's top plane at p's half-cell
T     = (p.x, p.y, topZ)
top   = max over admitted sources of
        shapeReveal(T) * ceilingVisibility(T) * losVisibility(T)
state = fires ? max(own.state, top) : own.state
```

`facePoint` is the sample's point on its face plane and `n` the face's
outward normal. `p`, the *column point*, therefore lies just inside the
matter the face bounds, in that matter's own column, where the sample's own
march target lies just outside it. `T` is the point of that column's top
plane over `p`, with no further bias. It is the point `losVisibility`
evaluates for a body standing there, so a wall is revealed through its column
top exactly where a BODY standing on that top would be. A source is admitted
by the channel mask of the sample's own grid cell.

The term fires only when all three conditions hold:

| Condition | Reason |
|---|---|
| The column field is published this frame: some live source is gated by line of sight (`losSourceMask != 0` in the paint, `FogLosColumnField::published()` on the CPU). | The field is the only per-column top plane the paint can read, and `FOG_LOS_BUILD` builds it only on such a frame. |
| The face is vertical. | A top face is its own ground surface. A top face under an overhang keeps its own verdict. |
| The column has a top plane above the sample by more than `kFogLosClearanceTolerance`. | A sample at its column's top has no surface above it to inherit from. |

Three properties follow from the march's own arithmetic:

- **A source below a top does not reveal a wall through it.** Its segment to
  `T` enters the column's edge cell under the top plane, by `kFogLosFaceBias`
  times the segment's slope, and is blocked once that exceeds
  `kFogLosClearanceTolerance`: an eye below the top by more than `0.05`
  times its perpendicular distance to the face. An eye nearer the top's
  height than that grazes the plane and reveals the wall, which is the
  verdict a body standing on that edge takes.
- **The column top is the highest occluder in the half-cell**, a FIELD voxel
  or a `blocksLOS_` shape. A FIELD shape without `blocksLOS_` is not in the
  column field and keeps its own verdict.
- **A column point that misses its own column leaves the sample unchanged.**
  A shape stamp edge that snaps inside the authored face reads the
  neighbouring column's lower top plane, and the third condition fails.

**Treatment.** The term raises the state and nothing else. A sample it raises
above its own state carries no styled band: no dissolve key is drawn for it
and no cap tones it, whatever the surface factor at `T`. When the term does
not exceed the own state, the own band stands. The rim distance and the grid
state stay the sample's own, so the radial rim lift and cut cap read what
they read without the term.

**Cost.** The term runs after the sample's own sources, and only when the
three conditions hold and the sample is below full reveal. A source is
evaluated at `T` only when its radial curve times its ceiling there exceeds
the current state; a gated source then marches to `T` and an ungated one
contributes without a march. These skips are exact, because the term feeds
the state alone.

Every painted route builds the column point from its face point and the
face the pass already decodes, stepping `kFogLosFaceBias` into the face:

| Route | Face point | In-plane coordinate |
|---|---|---|
| Cardinal voxel canvas | The recovered position with its face-axis coordinate snapped onto the micro lattice. | Snapped onto the lattice line through the drawn cell's view-frame lower corner, then stepped `kFogLosFaceBias` into the cell. |
| Per-axis cells and overflow entries | The cell's own position, which lies exactly on its face plane. | As recovered. |
| Smooth-yaw canvas and analytic (shape) pixels | The recovered or authored surface point. | As recovered. |

The cardinal raster recovers a pixel on the plane through its voxel's corner
that faces the camera, not on the voxel's face, so a wall pixel's in-plane
horizontal coordinate lands up to a third of a micro cell either side of
that corner. Left as recovered, the pixels along one vertical edge of every
wall would read the neighbouring column and keep a one-pixel unrevealed
line. The drawn cell extends from the corner toward the cardinal rotation of
view `(+1, +1)`, the direction `losVoxelBox` stamps it in.

BODY and EXEMPT pixels, and the unpainted detached-canvas FIELD clip, never
take the term.

**Byte identity.** A scene with no gated source publishes no column field, so
the term never fires and nothing changes by a byte. With a gated source, a
sample changes only where a source reveals the top of its own column above
it.

### Routes and subject classes

| Subject and route | Ceiling and line of sight | Treatment |
|---|---|---|
| FIELD voxels on a painted route: the world canvas at cardinal and smooth yaw, the per-axis cells, and the overflow entries | Ceiling selection removes absent voxels and exposes cap faces. Retained samples keep per-source ceiling and line-of-sight paint. | Geometric ceiling dissolve and phase; painted line-of-sight dissolve; geometric cap tone and painted band tone. |
| Analytic FIELD shapes | Ceiling and line of sight remain painted per sample. | Painted product-factor dissolve and band tone. No geometric cut. |
| FIELD on an [unpainted route](#unpainted-route-field-deviations): a world-placed detached canvas tagged FIELD | The height-cost radial clip excludes the ceiling factor; the shared retained set supplies the sole ceiling decision at the world voxel. | Geometric ceiling dissolve and phase, with exposed faces but no fog-pass tint. |
| BODY, on every route | Evaluated once at the ground anchor by the same oracle, and applied as one factor to every pixel. A body anchored under the ceiling renders whole even where its geometry rises above it. A body anchored above a hard ceiling is hidden whole, and one anchored in a soft band fades whole. | None. No BODY pixel hashes, dissolves, or takes a cap. |
| EXEMPT | Not evaluated. | None. |

The ceiling does not enter the z-free keep-ring culls. They still drop a
sample only when its column is unexplored and outside every source's keep
radius.

### Parameters and API

| Parameter | Owner | Default | Rule |
|---|---|---|---|
| `ceilingHeight` | Analytic source | Disabled | World units above `observerZ`. Any negative value disables the ceiling; see [height-cost authoring](#treatment). |
| `fadeHeight` | Analytic source | 0 | World units, never negative. 0 is the hard plane; see [height-cost authoring](#treatment). |
| `dissolveDensity` | Canvas | 0 | In [0, 1]. |
| `capTone` | Canvas | The radial rim cap's tone | In [0, 1]. |
| Phase | Canvas | 0 | Finite float supplied by the creation. Zero pins the static retained set; read only while treatment is enabled. |
| Treatment enabled | Canvas | Off | Setting the treatment enables it; clearing it disables it. |

| C++ (`C_CanvasFogOfWar` and `IRPrefab::Fog`) | Lua (`IRFog`) | Effect |
|---|---|---|
| `setVisionCircleCeiling` | `setVisionCeiling` | Sets `ceilingHeight` and `fadeHeight` on one analytic slot. |
| `visionCircleCeiling` | `getVisionCeiling` | Reads the stored pair for one slot. |
| `setRevealSurfaceTreatment` | `setRevealSurfaceTreatment` | Enables the canvas style with a `dissolveDensity` and a `capTone`. |
| `revealSurfaceTreatment` | `getRevealSurfaceTreatment` | Reads the stored style and whether it is enabled. |
| `clearRevealSurfaceTreatment` | `clearRevealSurfaceTreatment` | Disables the treatment. Every source keeps its ceiling. |
| `setRevealSurfacePhase` | `setRevealSurfacePhase` | Stores the finite phase without changing the treatment enable flag. |
| `revealSurfacePhase` | `getRevealSurfacePhase` | Reads the stored phase separately; the treatment getter keeps its three values. |

The slot argument must name a registered analytic slot, which is
`addVisionCircle`'s return value. Every slot starts with its ceiling
disabled, both when it is added and after the sources are cleared, so a
creation that re-adds its sources each frame re-authors the ceiling with
them, as it does for line of sight.

The phase occupies `revealSurfaceTreatment.w`; no observer-block layout
change is needed. Component phase access follows both the member and the
static-on-observers forms of the existing treatment API.

The Lua boundary raises an error naming the called function, and changes no
state, for an unregistered slot, a non-finite value, a negative `fadeHeight`,
or a `dissolveDensity` or `capTone` outside [0, 1]. A negative
`ceilingHeight` is not an error; it disables the ceiling. The getters return
the values as stored. The phase setter rejects non-numbers and non-finite
input without mutation.

### Defaults are byte-identical

Every source starts with its ceiling disabled and every canvas starts with
the treatment disabled. In that state nothing in this section changes a
byte: the FIELD paint, the radial rim cap, every BODY verdict, the keep-ring
culls and the paint pass's source skips all behave as they did before, and
the new GPU parameters are appended after the existing observer payload so
that no existing field moves.

Authoring a ceiling while the treatment stays disabled also removes voxel
matter at zero presence and exposes caps. The radial rim cap keeps its rule
and its tone.

### Rejected alternatives

| Alternative | Why it is rejected |
|---|---|
| Encoding the ceiling as a large `zCostUp` | It couples the ceiling to radial distance. Only an effectively infinite cost gives a level cut, and any softer cost slopes the hiding height across the disc, so a fade of a fixed height cannot be authored. |
| One global ceiling multiplied into the result after the maximum | It would cut the grid term and every other source. A ceiling belongs to one source and is measured from that source's `observerZ`. |
| A dissolve or cap style per source | Overlapping sources would disagree about how one sample looks. |
| Discarding ceiling pixels only in the fog paint | Neighbour masks never expose the cap below the removed matter, leaving a hollow column. The cut belongs in geometry selection. |
| Dissolving, tinting or clipping BODY or EXEMPT pixels | It breaks [A BODY is never sliced](#a-body-is-never-sliced). The anchor verdict owns a body's visibility, and EXEMPT is outside fog. |
| A separate treatment parameter or boundary representation for line of sight | The exact march's softness already supplies the factor. A second one would allow two overlapping treatments on one sample. |
| Applying the [column top](#column-top) to every sample by a height test alone | A top-face pixel whose recovered XY falls a float error inside a taller neighbouring column would light a one-pixel line along the foot of a shadowed wall, and a floor under a roofed column would reveal through the roof. |
| Reading the column top from a second per-column summary of the voxel pool | It is a new per-frame upload against a full Metal buffer table, and a second occluder set that can disagree with the one the march walks. |
| Building the column field for scenes with no gated source | A full column raster and a 2 MiB upload per frame for scenes that run no march. |
| Answering the column-top term from the sample's own route | The two targets end in different half-cells, the hard-route cache is a CPU structure keyed on exact XY and owned by the BODY evaluators, and the paint shares no route between pixels. |
| Giving a wall the dissolve decision of the top voxel above it | The top face keys its hash on its recovered position and the wall would key on the top plane; on a half-integer plane those round to different voxels, so the wall would dissolve against a different pattern than the surface above it. |
| A runtime switch for the column top | It adds public API and a uniform read for a test. The position-only oracle is the term-off arm. |

## Creation-facing seams

The engine defines how each seam composes with the reveal verdict; creations
assign gameplay meaning and select policy. The BODY oracle and
`setEntityGoverned` are exposed by #3676; #3679 binds override and channel
fields on `C_FogRevealed` and adds channel arguments to the IRFog vision and
verdict services.

### Per-body override

`FogOverride` composes after the field verdict:

- `NONE` preserves the evaluated factor and hysteresis.
- `FORCE_HIDDEN` produces factor 0 and hides the BODY in the same tick.
- `FORCE_REVEALED` produces factor 1 and shows the BODY in the same tick.

This is the seam for invisibility, disguise, detection marks, and forced
visibility. #3679 lands the component state and evaluator behavior.

### Channels

Sources and subjects carry bitmasks. A source can reveal a BODY only when the
two masks intersect; the engine assigns no gameplay meaning to creation-owned
bits. The default channel is bit 0 (`kFogChannelDefault = 1`). FIELD cells
carry their own masks in the world field (default bit 0): an analytic or
field-tier source reveals a FIELD cell only where the two masks intersect
(world field D8, D14). The grid term stays channel-blind for BODY subjects:
an admitted VISIBLE cell reveals a BODY whatever mask the BODY carries, and
an EXPLORED cell reveals no BODY.

### Reveal surface

A source's ceiling and the canvas's cut treatment are creation-owned. The
engine ships both disabled and assigns no look: a creation chooses each
source's `ceilingHeight` and `fadeHeight` through `setVisionCircleCeiling`
(`IRFog.setVisionCeiling`) and the canvas's dissolve density and cap tone
through `setRevealSurfaceTreatment`. The parameters, their validation and the
composition they feed are defined in
[Reveal-surface treatment](#reveal-surface-treatment).

### Hidden-body policy

`HIDE` is the default: a hidden BODY contributes no pixels. `GHOST` retains a
last-seen pose and renders that snapshot at explored-grey while the live body
is hidden. The pose is captured only while the body is shown under GHOST; a
never-shown body cannot create an origin ghost. Rigid and skinned voxel sets,
SDF shapes, and detached canvases freeze placement while continuing to show
authored content edits. Re-showing refreshes the snapshot. Seeing the remembered
spot, forcing hidden, selecting HIDE, or reclassifying to FIELD/EXEMPT discards
it. `C_FogRevealed` remains the 12-byte hot BODY verdict; the runtime-only
`C_FogGhost` component exists exactly when the BODY selects GHOST and owns the
remembered pose. The policy, ghost component, and all route mirrors are omitted
from saves, so a loaded BODY resumes as HIDE. Each route partitions its HIDE
evaluator (which excludes `C_FogGhost` and never touches ghost state) from a
GHOST evaluator that requires the component. C++ selects through
`IRPrefab::Fog::setHiddenPolicy`; Lua uses `IRFog.setHiddenPolicy` with
`IRComponent.FogHiddenPolicy.GHOST`. The corresponding `hiddenPolicy` getters
report the structural selection; the component has no writable policy field.

### Explored-state policy

The field distinguishes unexplored, explored, and currently visible cells.
Persistent policy keeps explored memory indefinitely. Decay policy returns an
explored cell whose mask intersects the policy mask to unexplored exactly
when the creation's simulation clock passes the cell's last exploration by
the configured duration. The policy, the clock, the per-cell masks and the
`exploreRadius` authoring service are the world field's D14; the C++ surface
is `IRPrefab::Fog::setExploredPolicy` / `setExploredTimeMs` /
`setCellChannels` / `exploreRadius`, the Lua surface the matching `IRFog`
entries.

## Creation migration

Tag terrain, floors, cliffs, and other terrain-tier geometry as FIELD when
creating them, using `IRPrefab::Fog::setSubjectClass` or the corresponding
marker component. A creation must not depend on terrain being inferred from
shape, size, or placement.

During the transition, `setEntityRevealGoverned(entity, true)` remains the
synchronous BODY opt-in and `setEntityRevealGoverned(entity, false)` becomes
an explicit FIELD tag. `SHAPE_FLAG_FOG_WHOLE_BODY_EXEMPT` remains as the
compatibility alias for `SHAPE_FLAG_FOG_BODY` when the shape route lands.

Leave ordinary discrete entities untagged when BODY is correct. On a fogged
world canvas the adoption systems classify such an entity as BODY within one
frame, evaluate its ground-anchor verdict, and attach the BODY state needed by
its raster path. Explicit BODY classification is still useful when a creation
wants the intent visible at construction or needs to configure BODY seams.

Tag cursors, selection markers, and overlays EXEMPT when they must ignore fog.
The engine's cursor-pivot indicator and gizmo handles require those explicit
tags when shape adoption lands (#3696). Screen-locked detached canvases remain
outside fog adoption; world-placed detached canvases follow the BODY default,
retain the documented clip when tagged FIELD, and gain an EXEMPT bypass in
#3696.

Sprites remain EXEMPT because their screen-composite route bypasses fog. A
world subject that must follow BODY or FIELD policy needs a fog-capable trixel
raster path rather than the sprite bypass.

During migration, audit every terrain-tier fixture before relying on the BODY
default. An omitted FIELD tag intentionally changes behavior: the geometry is
treated as one discrete body and can never be sliced or painted sample by
sample.
