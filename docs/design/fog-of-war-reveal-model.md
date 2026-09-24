# Fog-of-war reveal model

**Status:** Normative design contract. The implementation is split across
#3675–#3677 and #3679–#3680; the mapping table names the child that makes each
part live.
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
field once at the body's ground anchor, including the height cost at that
anchor, then applies the one factor to every pixel. No BODY pixel performs a
second field lookup, height clip, or rim fade. A body on higher ground may
therefore remain hidden until its anchor is revealed, but once revealed its
entire geometry is visible at the same factor.

### A FIELD sample is painted, not removed

An unrevealed FIELD sample remains in the geometry and is painted with the
fog color. Removing it would expose geometry behind it and change the world's
silhouette. One sanctioned exception remains: the compact and stage-1
keep-ring culls may drop samples only when their column is unexplored and
outside every source's keep radius, never when a sample could be revealed or
drawn as explored. A dropped region exposes what is behind it instead of the
unexplored color, so it looks identical only while that color matches the
backdrop. In particular, the world raster route does not remove FIELD voxels
because of the fog height ceiling.

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
| FIELD paint | `FOG_TO_TRIXEL` samples the field per pixel and paints toward the per-canvas unexplored color. The world stage-1 height drop is removed; the z-free compact and keep-ring culls retain only the sanctioned unexplored/outside-keep-radius exception above. The per-axis rotation route keeps its height drop (below). | #3675 (P2) |
| Voxel BODY apply route | Hidden voxel bodies are removed through the pool active mask. Shown pixels decode the BODY factor in `FOG_TO_TRIXEL`, skip grid, height, rim, and cut-cap evaluation, and apply one uniform result. | #3676 (P3) |
| Shape BODY apply route | `SHAPE_FLAG_FOG_HIDDEN` suppresses a hidden shape before raster work, independently of the author's `SHAPE_FLAG_VISIBLE`. Shown pixels carry and use the uniform BODY factor. | #3677 (P4) |
| Detached-canvas BODY apply route | The canvas owner carries `fogHidden_` and `fogRevealFactor_`. `ENTITY_CANVAS_TO_FRAMEBUFFER` suppresses a hidden canvas or applies the uniform factor to the whole composite; private-pool voxels carry the BODY exemption. | #3680 (P6) |
| EXEMPT apply routes | For a voxel set, explicit EXEMPT classification stamps factor 255 without `C_FogRevealed`. Shape and detached-canvas EXEMPT bypasses require explicit raster-route realization; exclusion from BODY adoption alone is insufficient. | #3676 (P3) for voxel sets; #3696 for shapes and detached canvases |
| Creation seams | Override and source/BODY channel data live with `C_FogRevealed` and feed every BODY evaluator; the FIELD loop accepts only sources on the implicit default cell channel. | #3679 (P5) |

Only FIELD matter and shapes with `C_LightBlocker::blocksLOS_` occlude the
reveal field; BODY matter does not. BODY pixels skip the per-pixel LOS gate
because their anchor verdict owns visibility (#3662).

### Unpainted-route FIELD deviations

A world-placed detached canvas has no `FOG_TO_TRIXEL` paint pass. When its
owner is explicitly FIELD, the detached route therefore retains the z-aware
geometric clip as a documented deviation. An untagged detached canvas is a
BODY instead: the verdict is evaluated at the world-space owner, its private
pool is exempt from that clip, and the result is applied at composite time
(#3680).

The per-axis rotation route has no paint pass either: at a non-cardinal yaw
the world voxels raster into the X/Y/Z per-axis textures, which the
forward-scatter composite draws straight into the framebuffer, and
`FOG_TO_TRIXEL` paints only the main canvas. Its stage-1 drop therefore keeps
the z-aware clip, so a height-hidden FIELD voxel is removed while the camera
is off-cardinal instead of rendering lit. `fog_edge_zcost_ceiling_paint_yaw9`
gates it. A per-axis paint pass would retire this deviation (#3710).

These exceptions do not create a fourth subject class. They are the closest
available application of the FIELD verdict on a route that cannot paint.

## Creation-facing seams

The engine defines how each seam composes with the reveal verdict; creations
assign gameplay meaning and select policy. #3679 binds override and channel
fields on `C_FogRevealed`; #3664's named IRFog subject-model and channel
integration follow-up owns service-level setters after those fields land.

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
