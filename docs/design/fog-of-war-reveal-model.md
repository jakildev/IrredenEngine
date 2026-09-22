# Fog-of-war reveal model

**Status:** Normative design contract. The implementation is split across
#3675–#3677 and #3679–#3680; the mapping table names the child that makes each
part live.
Until a child lands, its row describes the target behavior rather than the
current render path.

Fog answers one question for every rendered subject: is this part of the
world field, one discrete body, or outside fog? The answer determines where
the reveal verdict is evaluated and whether fog may vary across the rendered
geometry.

## Subject classes

| Class | Reveal verdict | Default for |
|---|---|---|
| **FIELD** | Per sample against the reveal field, including height terms. An unrevealed sample is painted with fog color. | Terrain and terrain-scale static geometry: the surface on which fog is drawn. |
| **BODY** | Once per entity at its ground anchor. Every rendered sample uses the resulting `C_FogRevealed::revealFactor_`. | Every discrete entity: creatures, units, items, props, and placed objects. |
| **EXEMPT** | Always visible; the fog field is not applied. | Cursors, markers, overlays, and anything else the creation explicitly nominates. |

BODY is the fallback. An untagged renderable on a fogged world canvas is
adopted as a BODY; FIELD and EXEMPT are explicit classifications.

## Invariants

### A BODY is never sliced

A BODY shows, hides, or fades as one unit. The engine evaluates the reveal
field once at the body's ground anchor, including the height cost at that
anchor, then applies the one factor to every pixel. No BODY pixel performs a
second field lookup, height clip, or rim fade. A body on higher ground may
therefore remain hidden until its anchor is revealed, but once revealed its
entire geometry is visible at the same factor.

### A FIELD sample is painted, not removed

An unrevealed FIELD sample remains in the geometry and is painted with the
fog color. Removing it would expose geometry behind it and change the world's
silhouette. Geometry culling may remain where it is provably indistinguishable
from painting, but it must keep a superset of samples the paint pass can
reveal. In particular, the world and per-axis raster routes do not remove
FIELD voxels because of the fog height ceiling.

## Engine mapping

The issue in the last column owns the transition from the current path to
this contract.

| Concern | Engine representation and route | Landing child |
|---|---|---|
| Classification | `C_FogField` and `C_FogExempt` are explicit marker components. `C_FogRevealed` holds BODY verdict state. `FogSubjectClass`, `IRPrefab::Fog::setSubjectClass`, `subjectClass`, and `revealSystems` are the public C++ surface. | #3676 (P3), extended to shapes by #3677 (P4) and detached canvases by #3680 (P6) |
| Default adoption | `FOG_SUBJECT_ADOPT` classifies an untagged voxel set on the active fog canvas as BODY within one frame. Shape and detached-canvas variants classify their respective owner entities. | #3676 (P3), #3677 (P4), #3680 (P6) |
| Shared verdict | `IRPrefab::Fog::evalReveal` is the grid-aware CPU oracle. BODY evaluators apply its result with `C_FogRevealSettings` hysteresis and write `C_FogRevealed::revealFactor_`. | #3676 (P3), reused by #3677 (P4), #3679 (P5), and #3680 (P6) |
| Entity-id carrier | High-word bit 28 says the pixel is BODY-classed; bits 27:20 carry its quantized 8-bit reveal factor. All entity-id readers strip these carrier bits before decoding the entity. | #3676 (P3), with the shape fold in #3677 (P4) |
| Voxel carrier source | `C_Voxel::reserved_` bit 3 carries the BODY class and bits 11:4 carry the factor. Stage 2 folds them into entity-id bit 28 and bits 27:20. | #3676 (P3) |
| Shape carrier source | `C_ShapeDescriptor` uses `SHAPE_FLAG_FOG_BODY` for the class and reserves GPU descriptor `flags` bits 23:16 for the factor before the shape raster folds both into the entity id. | #3677 (P4) |
| FIELD paint | `FOG_TO_TRIXEL` samples the field per pixel and paints toward the per-canvas unexplored color. World and per-axis stage-1 height drops are removed; z-free compact and keep-ring culls remain as conservative performance paths. | #3675 (P2) |
| Voxel BODY apply route | Hidden voxel bodies are removed through the pool active mask. Shown pixels decode the BODY factor in `FOG_TO_TRIXEL`, skip grid, height, rim, and cut-cap evaluation, and apply one uniform result. | #3676 (P3) |
| Shape BODY apply route | `SHAPE_FLAG_FOG_HIDDEN` suppresses a hidden shape before raster work, independently of the author's `SHAPE_FLAG_VISIBLE`. Shown pixels carry and use the uniform BODY factor. | #3677 (P4) |
| Detached-canvas BODY apply route | The canvas owner carries `fogHidden_` and `fogRevealFactor_`. `ENTITY_CANVAS_TO_FRAMEBUFFER` suppresses a hidden canvas or applies the uniform factor to the whole composite; private-pool voxels carry the BODY exemption. | #3680 (P6) |
| Creation seams | Override and source/BODY channel data live with `C_FogRevealed` and feed every BODY evaluator; the FIELD loop accepts only sources on the implicit default cell channel. | #3679 (P5) |

### Detached FIELD deviation

A world-placed detached canvas has no `FOG_TO_TRIXEL` paint pass. When its
owner is explicitly FIELD, the detached route therefore retains the z-aware
geometric clip as a documented deviation. An untagged detached canvas is a
BODY instead: the verdict is evaluated at the world-space owner, its private
pool is exempt from that clip, and the result is applied at composite time
(#3680).

This exception does not create a fourth subject class. It is the closest
available application of the FIELD verdict on a route that cannot paint.

## Creation-facing seams

The engine defines how each seam composes with the reveal verdict; creations
assign gameplay meaning and select policy. The engine-owned Lua surface in
#3664 is the creation boundary for the supported controls.

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
bits. The default channel is bit 0 (`kFogChannelDefault = 1`). FIELD cells use
that implicit default until the world field stores per-cell masks. #3679 lands
source/BODY masks; per-cell masks are tracked with explored decay in #3687.

### Hidden-body policy

`HIDE` is the default: a hidden BODY contributes no pixels. `GHOST` retains a
last-seen pose and renders that snapshot while the live body is hidden. The
subject-class epic establishes the seam but implements only HIDE; GHOST is
tracked by #3686.

### Explored-state policy

The field distinguishes unexplored, explored, and currently visible cells.
Persistent policy keeps explored memory indefinitely. Decay policy returns it
to unexplored according to creation-owned timing. The subject-class epic uses
persistent explored state; decay and per-cell channels are tracked by #3687.

## Creation migration

Tag terrain, floors, cliffs, and other terrain-tier geometry as FIELD when
creating them, using `IRPrefab::Fog::setSubjectClass` or the corresponding
marker component. A creation must not depend on terrain being inferred from
shape, size, or placement.

Leave ordinary discrete entities untagged when BODY is correct. On a fogged
world canvas the adoption systems classify such an entity as BODY within one
frame, evaluate its ground-anchor verdict, and attach the BODY state needed by
its raster path. Explicit BODY classification is still useful when a creation
wants the intent visible at construction or needs to configure BODY seams.

Tag cursors, selection markers, and overlays EXEMPT when they must ignore fog.
Screen-locked detached canvases remain outside fog adoption; world-placed
detached canvases follow the BODY default unless explicitly tagged FIELD or
EXEMPT.

During migration, audit every terrain-tier fixture before relying on the BODY
default. An omitted FIELD tag intentionally changes behavior: the geometry is
treated as one discrete body and can never be sliced or painted sample by
sample.
