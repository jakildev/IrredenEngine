# creations/editors/voxel_editor/ — the voxel editor

Inherits [`creations/CLAUDE.md`](../../CLAUDE.md) and the engine baseline.
Design log, findings and the authoring-session history:
[`docs/design/editor-authoring-friction.md`](../../../docs/design/editor-authoring-friction.md).

## Validators

- `python3 scripts/gui-verify.py IRVoxelEditor` — the standing GUI shot table.
- `python3 scripts/gui-verify.py IRVoxelEditor -- --gui-session <name>` — one
  scripted session: `face_pick` and `drag_probe` prove the picking contract
  against the full reference scene, `place_below` the Alt modifier,
  `fill_ghost` the fill preview drawn while a drag is held,
  `module_loaded` (with `--module test/module`) the creation-module seam,
  `component_attach` (same module) the COMPONENTS panel's manifest round trip,
  `component_field_page` the same on a field only the panel's pager reaches,
  `component_field_key` the same on a field whose name is not a Lua identifier,
  `composite_proof` (same module) authors the reference composite entity: nine
  parts from the module's recipes, a bake, LOD bands, an engine component and a
  yawed radial array,
  `parts_roundtrip` authors, moves, saves, clears, and reloads a two-part entity,
  `remove_part` removes and restores a part across hot and cold undo stacks and
  proves the saved manifest omits the removed record,
  `tier_scrub` proves per-part LOD bands and the tier scrubber,
  `part_mode_cycle` takes a Ctrl+P part through every render mode, the Ctrl+M
  twin, an array, and a save and reload,
  `corner_part_revox_refused` proves a part that is not centred on its
  translation is kept out of DETACHED REVOX,
  `parts_scroll` reaches every part of an arrayed scene through the PARTS list
  alone (reveal after the array, wheel, row click),
  `mode_preview` proves the deferred render-mode switch allocates and releases
  the selected part's private canvas while rotated and private-canvas parts stay
  outside the axis-aligned edit pick, `part_sizes` creates two parts of different extents and holds
  the mirror and loft tools to them, `part_gizmo_reach` drags a part's gizmo after a bake at its pivot,
  a camera turn, and on a turned copy, and `part_ring_turn` turns a part about each rotate ring's axis
  after a dimmed / transform-mode idle pair on the same gizmo texels.
- `python3 scripts/gui-verify.py IRVoxelEditor -- --module <dir>
  --module-session <name>` — one session registered by a creation module.
  `test/module` registers `test_parts`, `test_recipe_attach`, `test_radius`,
  `test_list_scroll`, `test_single`, and the deliberately failing
  `test_unreachable` fixture.
- `python3 scripts/render-verify.py --target IRVoxelEditor` — the editor's
  render reference set, discovered under this creation's `test/references/`.
- `--module test/module --module-session test_bake_radius_guard` is a second
  deliberately failing fixture: it exits 1 before any replay, on a recipe
  error that names `bakeSphere` and radius 2.095.
- `python3 scripts/author-entity.py <entity>` — replays an entity session twice
  and byte-compares the saved `.vxs` (rock, mushroom, bird at the default
  scene; `ant --scene-size 20 20 20`; `tree --scene-size 16 16 26`). A clean
  `git status assets/` afterwards means the committed asset is unchanged.
  Pass `--module <dir> --module-session <name>` to author from a module-owned
  session; `--asset-dir` may name an alternate output directory.
- `python3 scripts/author-entity.py --prefab composite_proof` — the same
  two-run gate over an entity scene (manifest, parts, sidecars), committed to
  `assets/voxel/entities/composite_proof/` with each `voxel_ref` rewritten to
  that staged path. The `--module` and `--part-names` it was authored with are
  read from that directory's `author.json`; a new prefab passes them once.
  `python3 scripts/render-verify.py --target IRShapeDebug` then spawns it for
  the `composite_proof_z*` shots.

## Picking contract

Every edit gesture picks through `IRVoxelEditor::pickEditable()`
(`editor_picking.hpp`). Never call `IRPrefab::Picking::castVoxelRay()` bare
from an edit path.

- **The nearest editable voxel under the cursor is what a click edits.** If a
  pixel of an editable voxel is on screen, clicking it edits that voxel.
- **The face is the one drawn under the cursor.** The pick casts the
  `SCREEN_PIXEL` ray, so a voxel's `-x`, `-y` and `-z` faces resolve
  separately at any zoom the face is visible at.
- **Reference furniture never takes a click.** Voxel-set furniture carries
  `C_EditorReference` and is passed through even when drawn in front; SDF
  shapes are left out of the edit pick wholesale (a shape hit has no face). A
  new reference voxel set gets the tag at creation, or it is editable.
- **Gizmo handles take clicks only in transform mode** (G; off at start, so
  joint placement and FK posing need it too). Outside it they draw dimmed and a
  click on one edits the scene beneath. In it a handle owns exactly the pixels
  it is drawn on (`cursorOnGizmoHandle()`, the GPU entity-id readback).
- A voxel's underside is never visible from the iso camera; Alt places on the
  far side of the picked face. That is a fact of the view, not of the pick.

The executor for "the face drawn under the cursor" is the
`*_pick_matches_render` assertion in `face_pick`: it compares the pick against
the main canvas's own face readback on every texel that shows the editable
set. An aim-based assertion alone cannot catch a pick that is offset from the
render, because the aim and the pick share one screen mapping.

## Authoring sessions

- Ctrl+P enters entity-scene mode and adds a voxel part at the PART SIZE
  panel's W / H / D (initially `--scene-size`); Ctrl+Shift+P adds the
  BAKE panel's selected SDF primitive. Tab/Shift+Tab select parts. Ctrl+S and
  Ctrl+O write/read the v2 prefab manifest plus one `.vxs` per voxel part.
- A voxel part starts clear (the single-set editor keeps its seeded ground
  plane), so a click has nothing to pick until a first write that needs no
  pick: BAKE, or a module recipe APPLY. Sessions author that write with
  `Builder::bakeSphere` (panel geometry in `bake_panel.hpp`).
- BAKE reads the radius the P1 slider holds, and a scripted drag leaves that
  within `kBakeParamSliderDragDrift` of the request. `bakeSphere` models the
  bake only when both ends of that range bake the same cells, and is a recipe
  error otherwise. The radii where the cells change depend on the parity of
  the part's extent (1.9 holds on an even extent, 2.14 on an odd one).
- A voxel part's extent is its `C_VoxelSetNew::size_`, saved as its `.vxs`
  bounds; the manifest carries no size. An edit path sizes from the set it
  edits, never from `g_editableSceneSize`. The loft tool is the exception: its
  masks are scene-sized, so it refuses a part of any other extent.
- Every voxel part the editor creates is `EntityAnchor::CENTER`: its
  translation is the middle of its box and the point it turns about. The
  manifest's per-part `anchor` carries that through save and load, and every
  rebuild of a part's set (array copy, Ctrl+M twin, load, undo of a removal)
  passes the source's anchor to `DenseVoxel::toComponent`. A world-to-cell
  conversion goes through `voxelSetCellOrigin`, never the translation alone.
- A part enters DETACHED REVOX only when `voxelSetIsCentered` holds, which a
  loaded manifest with no `anchor` (CORNER) usually fails: `M` and the twin
  skip the mode, the dropdown refuses it, and `load` rejects a manifest that
  asks for it.
- A selected part's gizmo is drawn on its pivot's pixels, in front of the part's
  own content (`EntityScene::partViewClearance`); other content can occlude it.
- `EntityScene` owns one root and its ordered part records. Only the selected
  voxel part receives paint; other voxel parts carry `C_EditorReference`.
- A part's LOD band (`EditorPart::lodMax_` fine .. `lodMin_` coarse) is
  mirrored onto its own `C_VoxelSetNew` / `C_ShapeDescriptor`, so the engine's
  LOD gate previews which parts exist at a tier. Parts stay resident; the
  editor never runs `PREFAB_LOD_PARTS`. The LOD panel's TIER slider and `,` /
  `.` pin `C_LodTierOverride` on the root and every part; FOLLOW ZOOM removes
  it. Neither touches the camera zoom.
- Radial and linear arrays create independently editable parts. Radial
  RADIUS/X and COPY YAW apply whole units and degrees; linear X stays
  continuous. One array is one undo record; rotational symmetry replays a
  stroke across the radial copy group through the same stroke buffer. The
  selected source stays outside the copy group: radial copies start at the
  radius, and linear copies start one step out.
- Delete and the PARTS-panel REMOVE button remove the selected part as one undo
  record. Ctrl+Z recreates its authored payload at the same ordered index and
  rewrites older undo references to the fresh entity id. Removing a rotational
  group member preserves the group's identity; symmetry remains inactive until
  the group again matches its rotational order.

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
  `releasePartGizmo` end it in a later one. `expectFillGhost` reads the texels the ghost owns, and a ghost inside
  the voxels an erase drag would carve owns none.
- `Session::Builder` is also the Lua authoring ABI. Adding or changing a public
  builder operation requires the matching `IREditor.registerSession` binding,
  conversion coverage, and binding-matrix test in the same change.
- Module sessions use `applyRecipe(name, values)`, `attachComponent(name)`,
  `setComponentField(component, field, text)`, and
  `setComponentOverrides(component, text)` instead of RECIPES or COMPONENTS
  panel coordinates. `applyRecipe` accepts only INT params with whole targets
  and at least four slider trixels per unit; other recipes keep using the raw
  GUI gestures without occupancy-model updates.
- Each of those names its list row. The Builder wheels a row below the
  RECIPES or COMPONENTS view into it with the live list's least movement, and
  shadows both lists' scroll offsets with `C_WidgetList`'s own clamp;
  `wheelGui` over either list moves its shadow too.

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
- A recipe is a pure function of `(values, size)`. APPLY writes its cells
  through `applyEditRaw` and one `commitStroke`, so one Ctrl+Z undoes it.
- A module may call `IRSystem.registerSystem`, but the editor places no module
  system in a pipeline yet.
- COMPONENTS attaches a module component (fields edited by type, listed by
  name and paged five to a page) or a C++
  one with a prefab factory (a Lua table literal) to the selected part or, with
  ROOT, the root. Its records (`component_records.hpp`) hold no sol objects;
  each apply goes through the factory, and Ctrl+S writes them as `components`.
  A saved scene naming a module component loads only with that module.
  The editor binds `C_AutoSpin` as its engine row; it runs no auto-spin
  system, so the component is data there.
- A list's visible-row count is a panel fact, not module ABI: a module
  registers any number of recipes and components, and a session names a row,
  never its position.
- Editor source names no module content: the module sessions read their
  expectations from `<dir>/session_expect.lua`. `test/module/` is the neutral
  fixture; `test/module_cpp_collision/` must exit 2.
