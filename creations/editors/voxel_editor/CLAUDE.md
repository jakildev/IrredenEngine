# creations/editors/voxel_editor/ — the voxel editor

Inherits [`creations/CLAUDE.md`](../../CLAUDE.md) and the engine baseline.
Design log, findings and the authoring-session history:
[`docs/design/editor-authoring-friction.md`](../../../docs/design/editor-authoring-friction.md).

## Validators

- `--profile-frames N --profile-yaw DEGREES --profile-zoom ZOOM` times a fixed 219-cell voxel shadow scene without captures and exits. UI remains present; AO, construction grid and hover feedback are off. The standard profile report includes wall-frame and GPU-stage samples; `--no-stage-timing` supplies an instrumentation control. The editor enables continuous surface lighting by default; `IR_PERAXIS_SURFACE_LIGHTING=0` selects sampled receiving for comparisons, and `1` selects continuous receiving explicitly.
- `python3 scripts/gui-verify.py IRVoxelEditor` — blank-grid sculpt workflow; `-- --gui-layout-check` also fails layout findings. Add `--reference-scene` for the diagnostic furniture/legacy shot table.
- `python3 scripts/gui-verify.py IRVoxelEditor -- --gui-session <name>` runs a named scripted session; `sessions.hpp` owns the complete registry. Core authoring flows are `sculpt_workflow`, `hierarchy_workflow`, `animation_boundary`, `sculpt_single_save`, `local_canvas`, `document_workflow`, `workarea_focus` and `motion_preview`. Include `--gui-layout-check` to gate reachability/layout. `region_tools` selects Box/Line through the toolbar and checks floor preview, exact commit, Undo and Escape at 30°/67°, including a floor obscured by a different editable part. `camera_gestures` checks camera chords, modifier-first release, interrupted strokes, fresh editing and undo. `place_below` validates inward placement. `face_pick` compares actual rendered faces with picking at yaw zero, and `text_input_command_capture` checks editor shortcuts under text focus.
- `python3 scripts/gui-verify.py IRVoxelEditor -- --module <dir>
  --module-session <name>` — one session registered by a creation module.
  `test/module` registers `test_parts`, `test_recipe_attach`, `test_class_paint`,
  `test_recipe_handle`, `test_recipe_handle_turned`, `test_radius`, `test_list_scroll`,
  `test_single`, `test_recipe_pages`, and the failing `test_unreachable`; the header contract
  also covers `test_module_files`, `test_module_arg`, and `test_module_arg_absent`.
- `python3 scripts/render-verify.py --target IRVoxelEditor` — the editor's
  render reference set, discovered under this creation's `test/references/`.
- `--module test/module --module-session test_bake_radius_guard` must exit 1
  before any replay, on a recipe error naming `bakeSphere` and radius 2.095.
- `python3 scripts/author-entity.py <entity>` — replays an entity session twice
  and byte-compares the saved `.vxs` (rock, mushroom, bird at the default
  scene; `ant --scene-size 20 20 20`; `tree --scene-size 16 16 26`). A clean
  `git status assets/` afterwards means the committed asset is unchanged.
  Pass `--module <dir> --module-session <name>` to author from a module-owned
  session; prefab `author.json` records its module path relative to the prefab
  directory, while absolute `--asset-dir` output is referenced from `assets/`.
- `python3 scripts/author-entity.py --prefab composite_proof` — the same
  two-run gate over an entity scene (manifest, parts, sidecars), committed to
  `assets/voxel/entities/composite_proof/` with each `voxel_ref` rewritten to
  that path; its `--module` and `--part-names` are read from that directory's
  `author.json` (prefab-relative; a new prefab passes them once).
  `python3 scripts/render-verify.py --target IRShapeDebug` then spawns it for
  the `composite_proof_z*` shots.

## Picking contract

Surface edit gestures pick through `IRVoxelEditor::pickEditable()`
(`editor_picking.hpp`). Never call `IRPrefab::Picking::castVoxelRay()` bare
from an edit path.

- **The nearest editable voxel under the cursor is what a click edits.** If a
  pixel of an editable voxel is on screen, clicking it edits that voxel.
- **The face is the one drawn under the cursor.** The
  `SCREEN_PIXEL` ray follows the full camera yaw and resolves each visible
  face separately. At yaw zero these are `-x`, `-y`, and `-z`; orbit changes
  the visible X/Y face signs. The depth parameter stays in the cardinal
  storage frame used by drag anchors.
- **Reference furniture never takes a click.** Voxel-set furniture carries
  `C_EditorReference` and is passed through even when drawn in front; SDF
  shapes are left out of the edit pick wholesale (a shape hit has no face). A
  new reference voxel set gets the tag at creation, or it is editable.
- **Gizmo handles take clicks only in transform mode** (G; off at start, so
  joint placement and FK posing need it too). Outside it they draw dimmed and a
  click on one edits the scene beneath. In it a handle owns exactly the pixels
  it is drawn on (`cursorOnGizmoHandle()`, the GPU entity-id readback).
- A voxel's underside is never visible from the iso camera; I places on the
  far side of the picked face. That is a fact of the view, not of the pick.

The cardinal executor for "the face drawn under the cursor" is `face_pick`'s
`*_pick_matches_render`, which compares the pick with the main canvas's face
readback on every editable texel at yaw zero. It does not validate continuous
orbit or cursor-to-window mapping. `screen_alignment` chooses raw mouse points
from displayed albedo/normal pixels at seven angles, including 30° and 67°;
`scripts/editor-screen-alignment.py` checks its PNGs for coverage and occlusion.

## Authoring sessions

- Ctrl+P enters entity-scene mode and adds a voxel part at the PART SIZE
  panel's W / H / D (initially `--scene-size`); Ctrl+Shift+P adds the
  BAKE panel's selected SDF primitive. Tab/Shift+Tab select parts. Ctrl+S and
  Ctrl+O write/read the prefab manifest plus one `.vxs` per voxel part (v3 for nested parents, v2 for flat scenes).
- Ordinary launch opens a blank grid in Sculpt and fits it into visible free viewport space; FOCUS fits the selected part/local canvas without changing camera yaw. `workarea_focus` validates rotated-child and local-canvas framing around module panels; diagnostic reference scenes retain seeded furniture. ADD uses `pickEditablePlane` on an empty floor, a hover ghost, and a latched-plane continuous stroke. One stroke is one undo; Escape restores it. PAINT changes occupied cells only. BOX and LINE can start on the empty floor; floor-started regions stay on that plane, and surface-started regions can extend into empty space on their initial face plane. LINE follows the dominant axis. Right-drag or Alt+left-drag orbits the camera; Ctrl+middle-drag also orbits, and Space+left-drag pans. Camera gestures own the pointer through release, suppress sculpt previews, and cancel pending BOX/LINE commits. I+left places inward on the far side of a picked face. Grid lines sit on the bottom support plane and share voxel depth; the preview is a full translucent cell. F12 saves the composite view to `save_files/screenshots` beside the executable.
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
- A recipe names cells; `Session::Builder` (`session_builder.hpp`) aims each
  click and mirrors the editable set in `OccupancyModel`, which casts the same
  `castGridRay` the live pick runs. Change the pick and the model follows; do
  not teach the model a rule the pick does not have.
- A segment's assertions evaluate once, after all of its events. Arm a
  pre-click check in its own segment.
- Entity sessions author on a bare stage. A session enters transform mode
  (`toggleTransformMode`) only around its gizmo drags and leaves it before
  painting: the model does not know where handles are drawn.
- A new session is four edits: `Session::Id`, `idFromName`, `Session::build`,
  and the `--gui-session` enum list in `main()`.
- A button left down at a segment's end stays down into the next: `holdBox` /
  `holdPartGizmo` end a segment with the drag held, and `releaseBox` /
  `releasePartGizmo` end it in a later one. `expectFillGhost` checks preview visibility and bounds; `screen_alignment`
  separately proves the final pixel footprint, alpha and solid occlusion.
- `Session::Builder` is also the Lua authoring ABI. Adding or changing a public
  builder operation requires the matching `IREditor.registerSession` binding,
  conversion coverage, and binding-matrix test in the same change.
- Module sessions use `applyRecipe(name, values)`, `attachComponent(name)`,
  `setComponentField(component, field, text)`, and
  `setComponentOverrides(component, text)` instead of RECIPES or COMPONENTS
  panel coordinates. `applyRecipe` admits whole INT targets and exact stepped FLOAT (`step`) lattice targets at four slider trixels per unit or step (`test/module`'s `test_recipe_float_step`); other recipes keep using raw GUI gestures without occupancy-model updates.
  Stepped FLOAT lattice point `k` is the float32 fused result of `k * step + min`, followed by the range clamp.
  Binary-fraction minima and steps are exact; decimal literals or Lua arithmetic may differ, so copy the error's full-precision `nearest` value when an exact non-binary target is required.
- Recipe-bound parts use `paintClass(cell, class)` and `expectPartCellClass(part, cell, class | nil, name)` for persistent class masks.
- Each of those names its list row. The Builder wheels a row below the
  RECIPES or COMPONENTS view into it with the live list's least movement, and
  shadows both lists' scroll offsets with `C_WidgetList`'s own clamp;
  `wheelGui` over either list moves its shadow too.
- `dragRecipeHandle(handle, target)` drags the selected part's recipe handle
  to a set-local cell whose world coordinate sum is `at`'s (the drag's screen
  plane) at the baseline yaw, in transform mode and never on a turned GRID part,
  and models the re-apply. After an undo, redo, clear or reload it and `paintClass`
  need a fresh APPLY. `expectRecipeValue`, `expectPartMatchesRecipe` and
  `expectRecipeHandleTexels` read the live binding; the gizmo census skips handles.

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
