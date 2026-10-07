# Fog-of-war reveal model

**Status:** Normative design contract. The implementation is split across
#3675–#3677 and #3679–#3680, with the reveal-surface ceiling and treatment in
#4055; the mapping table names the child that makes each part live.
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
| **FIELD** | Per sample against the reveal field, including height terms. An unrevealed sample is painted with fog color. | Terrain and terrain-scale static geometry: the surface on which fog is drawn. |
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

### A FIELD sample is painted, not removed

An unrevealed FIELD sample remains in the geometry and is painted with the
fog color. Removing it would expose geometry behind it and change the world's
silhouette. One sanctioned exception remains: the compact and stage-1
keep-ring culls may drop samples only when their column is unexplored and
outside every source's keep radius, never when a sample could be revealed or
drawn as explored. A dropped region exposes what is behind it instead of the
unexplored color, so it looks identical only while that color matches the
backdrop. In particular, the world and per-axis raster routes do not remove
FIELD voxels because of the fog height ceiling.

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
| FIELD paint | `FOG_TO_TRIXEL` samples the field and paints toward the per-canvas unexplored color: per pixel on the world canvas, and per occupied face sample plus overflow entry on the per-axis rotation route. Both routes use the z-free nearest-column keep ring; only the sanctioned unexplored/outside-keep-radius exception above is removed. | #3675 (P2), #3710 (per-axis) |
| Voxel BODY apply route | Hidden voxel bodies are removed through the pool active mask. Shown pixels decode the BODY factor in `FOG_TO_TRIXEL`, skip grid, height, rim, and cut-cap evaluation, and apply one uniform result. | #3676 (P3) |
| Shape BODY apply route | `SHAPE_FLAG_FOG_HIDDEN` suppresses a hidden shape before raster work, independently of the author's `SHAPE_FLAG_VISIBLE`. Shown pixels carry and use the uniform BODY factor. | #3677 (P4) |
| Detached-canvas BODY apply route | The canvas owner carries `fogHidden_` and `fogRevealFactor_`. `ENTITY_CANVAS_TO_FRAMEBUFFER` suppresses a hidden canvas or applies the uniform factor to the whole composite; private-pool voxels carry the BODY exemption. | #3680 (P6) |
| EXEMPT apply routes | For a voxel set, explicit EXEMPT classification stamps factor 255 without `C_FogRevealed`. Shape and detached-canvas EXEMPT bypasses require explicit raster-route realization; exclusion from BODY adoption alone is insufficient. | #3676 (P3) for voxel sets; #3696 for shapes and detached canvases |
| Reveal surface | A per-source ceiling (`setVisionCircleCeiling`) multiplies with `losVisibility` into one surface factor per source before the maximum over sources. A canvas-owned style (`setRevealSurfaceTreatment`) marks a partial winning cut on FIELD samples in `FOG_TO_TRIXEL` and the overflow lane. [Reveal-surface treatment](#reveal-surface-treatment) is the contract. | #4055 |
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
ceil_to_10us(1.15 * M)`. On the unchanged `IRPerfGrid --fog-los-unexplored`
reference fixture `M` is +67 µs, so the gate is control + 80 µs (#3878). A
fixture that changes the marched pixels, the sources or the blocks crossed
measures its own `M`.

### Unpainted-route FIELD deviations

A world-placed detached canvas has no `FOG_TO_TRIXEL` paint pass. When its
owner is explicitly FIELD, the detached route therefore retains the z-aware
geometric clip as a documented deviation. An untagged detached canvas is a
BODY instead: the verdict is evaluated at the world-space owner, its private
pool is exempt from that clip, and the result is applied at composite time
(#3680).

This exception does not create a fourth subject class. It is the closest
available application of the FIELD verdict on a route that cannot paint. A
source's ceiling enters this clip and nothing else from the
[reveal-surface treatment](#reveal-surface-treatment) does.

## Reveal-surface treatment

**Landing child:** #4055 (landed). This section is the contract the render
path implements.

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

### Treatment

With the treatment disabled, a partial contribution simply stands. The sample
blends toward the unexplored colour by its reveal, and a fully cut sample is
the flat unexplored colour. The treatment adds a dissolve and a cap tone to
the partial band.

The style belongs to the canvas: one `dissolveDensity` and one `capTone`,
each in [0, 1], enabled and cleared explicitly. The ceiling and fade stay per
source. Because there is one style per canvas, two overlapping sources, or a
ceiling cut and a line-of-sight cut, cannot disagree about how one sample
looks.

A source's contribution styles a FIELD sample only when both conditions hold:

1. The contribution is partial: `0 < surfaceVisibility < 1`.
2. The contribution wins. It raises the sample's reveal above the grid term
   and above every other source's contribution.

A fully visible competing source or a VISIBLE grid cell therefore suppresses
the treatment, because the partial contribution no longer decides the sample.
The ceiling and line-of-sight factors are never styled separately. Their
product is the one factor the treatment reads, so a sample carries at most
one treatment. Among contributions that tie, the one with the larger band
weight (see [Cap tone](#cap-tone)) styles the sample.

A hard ceiling on a source with a hard or absent line-of-sight gate has no
partial band, so the treatment marks nothing there. A creation that wants a
marked cut authors a `fadeHeight` or a line-of-sight softness.

### Dissolve

The dissolve is a colour decision and never removes geometry. Each world
voxel has one fixed hash value in [0, 1]. A fragment of the styled
contribution is retained when

```
hash <= mix(1, surfaceVisibility, dissolveDensity)
```

and rejected otherwise. At density 0 every fragment is retained. At density 1
a fragment is retained only where its voxel's hash is at most
`surfaceVisibility`, so the band thins from solid at its fully visible edge
to empty at its fully cut edge.

The source of a rejected fragment contributes nothing to that sample. The
sample falls back to the maximum of the grid term and the remaining sources,
which is the unexplored colour when nothing else reveals it. Its geometry is
still drawn.

The hash is integer arithmetic over the sample's integer world voxel, built
from fixed-width xor and multiply steps and written identically in GLSL and
Metal. Nothing else enters it: no frame counter, no camera or yaw term, and
no floating-point transcendental. A voxel makes the same decision on every
frame, at every camera yaw, on every painted route and on both backends.

### Cap tone

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

### Routes and subject classes

| Subject and route | Ceiling and line of sight | Treatment |
|---|---|---|
| FIELD on a painted route: the world canvas at cardinal and smooth yaw, the per-axis cells, and the overflow entries | Evaluated per sample inside the paint's per-source contribution. The geometry stays, and a sample above the ceiling is painted the unexplored colour. | Applied when enabled. |
| FIELD on an [unpainted route](#unpainted-route-field-deviations): a world-placed detached canvas tagged FIELD | The ceiling multiplies each source inside the route's existing z-aware clip. The clip removes a voxel only where its result is zero, so a soft band is kept whole and the cut falls at `ceilingHeight + fadeHeight`. | None. The route synthesizes no dissolve and no tint. |
| BODY, on every route | Evaluated once at the ground anchor by the same oracle, and applied as one factor to every pixel. A body anchored under the ceiling renders whole even where its geometry rises above it. A body anchored above a hard ceiling is hidden whole, and one anchored in a soft band fades whole. | None. No BODY pixel hashes, dissolves, or takes a cap. |
| EXEMPT | Not evaluated. | None. |

The ceiling does not enter the z-free keep-ring culls. They still drop a
sample only when its column is unexplored and outside every source's keep
radius.

### Parameters and API

| Parameter | Owner | Default | Rule |
|---|---|---|---|
| `ceilingHeight` | Analytic source | Disabled | World units above `observerZ`. Any negative value disables the ceiling. |
| `fadeHeight` | Analytic source | 0 | World units, never negative. 0 is the hard plane. |
| `dissolveDensity` | Canvas | 0 | In [0, 1]. |
| `capTone` | Canvas | The radial rim cap's tone | In [0, 1]. |
| Treatment enabled | Canvas | Off | Setting the treatment enables it; clearing it disables it. |

| C++ (`C_CanvasFogOfWar` and `IRPrefab::Fog`) | Lua (`IRFog`) | Effect |
|---|---|---|
| `setVisionCircleCeiling` | `setVisionCeiling` | Sets `ceilingHeight` and `fadeHeight` on one analytic slot. |
| `visionCircleCeiling` | `getVisionCeiling` | Reads the stored pair for one slot. |
| `setRevealSurfaceTreatment` | `setRevealSurfaceTreatment` | Enables the canvas style with a `dissolveDensity` and a `capTone`. |
| `revealSurfaceTreatment` | `getRevealSurfaceTreatment` | Reads the stored style and whether it is enabled. |
| `clearRevealSurfaceTreatment` | `clearRevealSurfaceTreatment` | Disables the treatment. Every source keeps its ceiling. |

The slot argument must name a registered analytic slot, which is
`addVisionCircle`'s return value. Every slot starts with its ceiling
disabled, both when it is added and after the sources are cleared, so a
creation that re-adds its sources each frame re-authors the ceiling with
them, as it does for line of sight.

The Lua boundary raises an error naming the called function, and changes no
state, for an unregistered slot, a non-finite value, a negative `fadeHeight`,
or a `dissolveDensity` or `capTone` outside [0, 1]. A negative
`ceilingHeight` is not an error; it disables the ceiling. The getters return
the values as stored.

### Defaults are byte-identical

Every source starts with its ceiling disabled and every canvas starts with
the treatment disabled. In that state nothing in this section changes a
byte: the FIELD paint, the radial rim cap, every BODY verdict, the keep-ring
culls and the paint pass's source skips all behave as they did before, and
the new GPU parameters are appended after the existing observer payload so
that no existing field moves.

Authoring a ceiling while the treatment stays disabled changes only the
reveal of the samples that ceiling cuts. The radial rim cap keeps its rule
and its tone.

### Rejected alternatives

| Alternative | Why it is rejected |
|---|---|
| Encoding the ceiling as a large `zCostUp` | It couples the ceiling to radial distance. Only an effectively infinite cost gives a level cut, and any softer cost slopes the hiding height across the disc, so a fade of a fixed height cannot be authored. |
| One global ceiling multiplied into the result after the maximum | It would cut the grid term and every other source. A ceiling belongs to one source and is measured from that source's `observerZ`. |
| A dissolve or cap style per source | Overlapping sources would disagree about how one sample looks. |
| Removing geometry above the ceiling on a painted FIELD route | It breaks [A FIELD sample is painted, not removed](#a-field-sample-is-painted-not-removed): the hole exposes what is behind it and changes the world's silhouette. |
| Dissolving, tinting or clipping BODY or EXEMPT pixels | It breaks [A BODY is never sliced](#a-body-is-never-sliced). The anchor verdict owns a body's visibility, and EXEMPT is outside fog. |
| A separate treatment parameter or boundary representation for line of sight | The exact march's softness already supplies the factor. A second one would allow two overlapping treatments on one sample. |

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
last-seen pose and renders that snapshot while the live body is hidden. The
subject-class epic establishes the seam but implements only HIDE; GHOST is
tracked by #3686.

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
