# creations/editors/voxel_editor/ — the voxel editor

Inherits [`creations/CLAUDE.md`](../../CLAUDE.md) and the engine baseline.
Design log, findings and the authoring-session history:
[`docs/design/editor-authoring-friction.md`](../../../docs/design/editor-authoring-friction.md).

## Validation

[Editor validation](../../../docs/design/editor-validation.md) owns native workflows, module fixtures and session-authoring contracts.

## Palette and comparison contracts

Picker reads unlit voxel RGB. An unsaved sampled color is a transient foreground
selection (index -1); Add persists it as a swatch. Recolor changes occupied cells
only. Bucket fills the nearest visible enclosed four-connected empty region on
one local axis plane; an open boundary rejects the operation before mutation.
Bucket does not expand through symmetry. Shapes creates a new voxel part in the
active color with integer X/Y/Z extents, using closed cell/solid intersection.
Stamp stays active for repeated clicks against the document plane. In Drag mode,
set the footprint first, then the height. Each placed part owns one Undo.

Palettes use direct RGB: Edit/Add and `.gpl` Load/Save change swatches, never
existing voxel colors. Arrays preview selected-part copies without descendants;
SDF and detached proposals are labeled voxel approximations. Apply owns one Undo.
LOD comparison captures before GUI composition with a fixed view and common world
aperture. Native-size tiles require integer UI scaling. Copy To Tier preserves
voxel extent and records; it does not decimate or narrow the source LOD band.

## Picking contract

The editor defaults to the engine's spatial view; `--legacy-view` selects the
yaw-only compatibility path. Camera orientation, input units, platform controls,
and persistent configuration are defined in
[editor spatial navigation](../../../docs/design/editor-spatial-navigation.md).
Orbit uses `CursorPivot::OrbitAnchor`: ordinary right-drag and precise two-axis
scroll orbit the construction-plane center; Option/Alt+left and Ctrl+middle
resolve the editable surface or floor once at press. The gold marker identifies
that anchor. World up is -Z, elevation crosses the horizon to reveal undersides,
and pole clamps keep ordinary orbit upright; Option/Alt+Shift+left rolls explicitly.
Native pinch and wheel zoom anchor at each event's window position. Shift+swipe,
middle-drag, and Space+left pan. Camera ownership survives modifier release,
and phased scroll/pinch retain their initial UI or viewport ownership. OS scroll
momentum does not move the camera. RESET VIEW/Home (Command+0 or Fn+Left on Mac)
restores the centered upright view and configured zoom, default 4; 0 preserves
zoom. Continuous zoom retains discrete authored LOD tiers. `spatial_navigation`
is the spatial native session; `viewport_scroll` covers the legacy yaw flow.
macOS supports Command+N/O/S/Shift+S for documents and Command+Z/Shift+Z
for history. Focused text fields consume shortcuts. Control+Alt+Y toggles
rotational symmetry.

Surface edit gestures pick through `IRVoxelEditor::pickEditable()`
(`editor_picking.hpp`). Never call `IRPrefab::Picking::castVoxelRay()` bare
from an edit path.

- **The nearest editable voxel under the cursor is what a click edits.** If a
  pixel of an editable voxel is on screen, clicking it edits that voxel.
- **The face is the one drawn under the cursor.** The
  `SCREEN_PIXEL` ray follows the full spatial camera orientation and resolves
  each visible face separately, including undersides. In legacy view it follows
  yaw and keeps the cardinal-storage depth convention. Explicit spatial
  view-depth queries use the continuous camera basis.
- **Reference furniture never takes a click.** Voxel-set furniture carries
  `C_EditorReference` and is passed through even when drawn in front; SDF
  shapes are left out of the edit pick wholesale (a shape hit has no face). A
  new reference voxel set gets the tag at creation, or it is editable.
- **Gizmo handles take clicks only in transform mode** (G; off at start, so
  joint placement and FK posing need it too). Outside it they draw dimmed and a
  click on one edits the scene beneath. In it a handle owns exactly the pixels
  it is drawn on (`cursorOnGizmoHandle()`, the GPU entity-id readback).
- Spatial orbit can reveal a voxel's underside. I places on the far side of the
  picked face; the legacy fixed-elevation camera does not reveal undersides.

The cardinal executor for "the face drawn under the cursor" is `face_pick`'s
`*_pick_matches_render`, which compares the pick with the main canvas's face
readback on every editable texel at yaw zero. It does not validate continuous
orbit or cursor-to-window mapping. `screen_alignment` chooses raw mouse points
from displayed albedo/normal pixels at seven angles, including 30° and 67°;
`scripts/editor-screen-alignment.py` checks its PNGs for coverage and occlusion.

## Workspace controls

`workspace_controls` checks shortcuts, hints, live panel hit geometry, camera ownership, snapping and reset at 30°/67°. Run `workspace_restore` afterward to prove persistence in a fresh process. Both use `save_files/gui_layout_workflow.cfg`, separate from personal preferences. The neutral module session `test_dock_rails`, run with an isolated `IR_EDITOR_LAYOUT_PATH`, audits scrolled controls and requires the camera zoom to remain fixed.

- Backtick and F1 toggle the keyboard help menu. Help owns pointer and keyboard input until closed with either key or Escape. Focused text fields consume backtick.
- Hold Option/Alt to show bordered shortcut keycaps. Labels resolve from registered command bindings. The primary tools use 1 Brush, 2 Erase, 3 Recolor, 4 Bucket, 5 Color Picker, 6 Shapes and 7 Line. Controls without a shortcut remain unlabeled. Runtime user keymap assignment belongs to the shared command Actions system.
- Drag a panel title to float it or snap it into a side rail. Reset Layout restores default slots. Ordinary launches persist placement in `save_files/editor_layout.cfg` beside the executable, with `IR_EDITOR_LAYOUT_PATH` available for isolated preferences and opt-in layout validation in scripted sessions. Document files do not contain workspace layout.
- Panel hit testing and camera-start ownership use live widget positions. A moved panel blocks editing, orbit and zoom within its visible bounds. FOCUS accounts for side rails and floating panels.

## Authoring sessions

- Ctrl+P enters entity-scene mode and adds a voxel part at the NEW GRID SIZE
  panel's X/Y/Z dimensions. File > New uses them for the standalone canvas; Ctrl+Shift+P adds the
  BAKE panel's selected SDF primitive. Tab/Shift+Tab select parts. Ctrl+S and
  Ctrl+O write/read the prefab manifest plus one `.vxs` per voxel part (v3 for nested parents, v2 for flat scenes).
- Ordinary launch opens a blank 32×32×16 grid in Sculpt and fits it into visible free viewport space; FOCUS fits the selected part/local canvas without changing camera yaw. `workarea_focus` validates rotated-child and local-canvas framing around module panels; diagnostic reference scenes retain seeded furniture. ADD uses `pickEditablePlane` on an empty floor, a hover ghost, and a latched-plane continuous stroke. One stroke is one undo; Escape restores it. PAINT changes occupied cells only. BOX and LINE can start on the empty floor; floor-started regions stay on that plane, and surface-started regions can extend into empty space on their initial face plane. LINE follows the dominant axis. Shift+drag with Line selected locks X and Y and measures vertical movement in projected voxel heights. Right-drag or Alt+left-drag orbits the camera; Ctrl+middle-drag also orbits, and Space+left-drag pans. Camera gestures own the pointer through release, suppress sculpt previews, and cancel pending BOX/LINE commits. I+left places inward on the far side of a picked face. Grid lines sit on the bottom support plane and share voxel depth; the preview is a full translucent cell. F12 saves the composite view to `save_files/screenshots` beside the executable.
- Assemble exposes hierarchy/LOD/components/arrays; Generate exposes recipes/bake/module panels; Animate exposes single-asset frames/rig tools. Hidden widgets retain state and neither draw nor intercept input. ALL exposes the diagnostic layout.
- BAKE reads the radius the P1 slider holds, and a scripted drag leaves that
  within `kBakeParamSliderDragDrift` of the request. `bakeSphere` models the
  bake only when both ends of that range bake the same cells, and is a recipe
  error otherwise. The radii where the cells change depend on the parity of
  the part's extent (1.9 holds on an even extent, 2.14 on an odd one).
- A voxel part's extent is its `C_VoxelSetNew::size_`, saved as its `.vxs`
  bounds; the manifest carries no size. An edit path sizes from the set it
  edits, never from `g_editableSceneSize`. The loft tool is the exception: its
  masks are scene-sized, so it refuses a part of any other extent.
- New voxel parts use `EntityAnchor::CENTER`; an adopted single asset keeps its original anchor. NEW PART preserves that asset as the first part; multi-frame, layered, or rigged documents refuse conversion without mutation.
- A new part's translation is the middle of its box and the point it turns about. The
  manifest's per-part `anchor` carries that through save and load, and every
  rebuild of a part's set (array copy, Ctrl+M twin, load, undo of a removal)
  passes the source's anchor to `DenseVoxel::toComponent`. A world-to-cell
  conversion goes through `voxelSetCellOrigin`, never the translation alone.
- A part enters DETACHED REVOX only when `voxelSetIsCentered` holds (a loaded
  CORNER part usually fails it): `M`, the twin, the dropdown and `load` refuse.
- A selected part's gizmo is drawn on its pivot's pixels, in front of the part's
  own content (`EntityScene::partViewClearance`); other content can occlude it.
- `EntityScene` owns a root and stable part records with optional parent ids. Attachment preserves world pose and rejects cycles; removing a parent detaches children preserving pose. Each part retains its local voxel canvas. Only the selected part receives paint. LOCAL EDIT uses an identity GRID proxy for rotated/scaled/detached parts; edits and history target the original authored records, and DONE EDIT restores assembly. Local canvas supports sculpt/color/symmetry/history/save; layers, class painting, loft and joint authoring require leaving that view.
- A part's LOD band (`lodMax_` fine .. `lodMin_` coarse) is mirrored onto its
  set / shape so the engine's LOD gate previews it; parts stay resident (no
  `PREFAB_LOD_PARTS`). TIER and `,` / `.` pin `C_LodTierOverride` on the root
  and every part, FOLLOW ZOOM removes it; neither touches the camera zoom.
- Radial and linear arrays create independently editable parts. Module sessions
  set radial RADIUS/X and COPY YAW through `applyRadialArray`; linear X stays
  continuous. One array is one undo record; rotational symmetry replays a
  stroke across the radial copy group through the same stroke buffer. The
  selected source stays outside the copy group: radial copies start at the
  radius, and linear copies start one step out.
- Part transform drags, LOD-band edits, and attachment each make one undo record; Escape cancels an active transform/LOD gesture. Shift+A/D add/duplicate single-asset frames; multipart frame controls are disabled.
- Ordinary documents expose named Save / Save As / Open / New and Save / Discard / Cancel for unsaved changes, including native-window close. Failed loads retain the current document; failed saves never clear its modified state. Single-asset saves stage frames, layer metadata and optional rig as one publication; composite saves refuse unsupported frame, rig or layer metadata. `document_workflow` exercises these decisions through the UI.
- PREVIEW isolates a rendering copy of the assembly and samples `C_AutoSpin` from immutable rest poses. Play/seek/step/reset never mutate authored parts; Save serializes the originals. Preview intentionally does not run module systems or arbitrary component factories. `motion_preview` exercises detached-child motion, save/reopen and input isolation.
- Composite saves stage canonical files and roll back publication failures. Recovery directories survive failed rollback; this is not crash/power-loss atomic.
- Delete and PARTS REMOVE remove the selected part as one undo record; undo
  restores its payload at the same index, redo removes it again. Records name
  stable part ids, never entity ids. A removed rotational group member keeps the
  group's identity; symmetry stays off until the group matches its order again.
Session authoring contracts and executable checks: [editor validation](../../../docs/design/editor-validation.md).

## Creation modules (`--module <dir>`)

- `ModuleHost` (`editor_lua_host.hpp`, whose header comment is the contract)
  runs `<dir>/init.lua` in the world's Lua VM in EVAL mode. A module registers
  components (`IRComponent.register`), recipes, panels, and authoring sessions
  (`IREditor.registerSession(name, function(session) ... end)`). A
  missing `init.lua`, a Lua error, or a component name already registered (a
  C++ one included) exits 2.
- `--module-session <name>` requires `--module`, is mutually exclusive with a
  non-`none` `--gui-session`, and builds the registered function exactly once
  against the live scene size and origin. Its session handle is valid only
  during that function call; retaining it for a later panel callback is an
  error.
- A recipe is a pure function of `(values, size, mask)`. APPLY writes its cells
  as one undo record and binds the part to it (`EditorPart::recipeBinding_`).
  Reapplying the same recipe replaces unchanged generated cells and preserves
  manual colours, erasures, metadata and additions; handles use the same rule.
  Persistent class masks participate in both old and new evaluations. An erased
  coordinate has no tombstone after it leaves the generated footprint, so a later
  expansion can fill it again. Switching recipe identity retains additive behavior.
  The session occupancy model preserves erasures but cannot model colour-only
  overrides; assert their live result without subsequent model-dependent aims.
- `opts.handles` (header contract): in transform mode the bound part's handles
  draw in front of its content and behind its gizmo, turned with it (none on a
  turned GRID part). A drag frame replaces the old values' cells with the new;
  release commits one undo record restoring the binding and its sliders.
- A module may call `IRSystem.registerSystem`, but the editor places no module
  system in a pipeline yet.
- COMPONENTS attaches a module component (fields edited by type, listed by
  name and paged five to a page) or a C++
  one with a prefab factory (a Lua table literal) to the selected part or, with
  ROOT, the root. Its records (`component_records.hpp`) hold no sol objects;
  each apply goes through the factory, and Ctrl+S writes them as `components`.
  A saved scene naming a module component loads only with that module.
  The editor binds `C_AutoSpin` as its engine row; authored entities keep their rest pose while PREVIEW samples isolated copies.
- A list's visible-row count is a panel fact, not module ABI: a module
  registers any number of recipes and components, and a session names a row,
  never its position.
- Editor source names no module content: the module sessions read their
  expectations from `<dir>/session_expect.lua`. `test/module/` is the neutral
  fixture; `test/module_cpp_collision/` must exit 2.
