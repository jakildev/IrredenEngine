# creations/editors/voxel_editor/ — the voxel editor

Inherits [`creations/CLAUDE.md`](../../CLAUDE.md) and the engine baseline.
Design log, findings and the authoring-session history:
[`docs/design/editor-authoring-friction.md`](../../../docs/design/editor-authoring-friction.md).

## Validators

- `python3 scripts/gui-verify.py IRVoxelEditor` — the standing GUI shot table.
- `python3 scripts/gui-verify.py IRVoxelEditor -- --gui-session <name>` — one
  scripted session: `face_pick` and `drag_probe` prove the picking contract
  against the full reference scene, `place_below` the Alt modifier,
  `module_loaded` (with `--module test/module`) the creation-module seam,
  `component_attach` (same module) the COMPONENTS panel's manifest round trip,
  `component_field_page` the same on a field only the panel's pager reaches,
  `component_field_key` the same on a field whose name is not a Lua identifier,
  `parts_roundtrip` authors, moves, saves, clears, and reloads a two-part entity,
  `remove_part` removes and restores a part across hot and cold undo stacks and
  proves the saved manifest omits the removed record,
  `tier_scrub` proves per-part LOD bands and the tier scrubber, and
  `mode_preview` proves the deferred render-mode switch allocates and releases
  the selected part's private canvas while rotated and private-canvas parts stay
  outside the axis-aligned edit pick.
- `python3 scripts/render-verify.py --target IRVoxelEditor` — the editor's
  render reference set, discovered under this creation's `test/references/`.
- `python3 scripts/author-entity.py <entity>` — replays an entity session twice
  and byte-compares the saved `.vxs` (rock, mushroom, bird at the default
  scene; `ant --scene-size 20 20 20`; `tree --scene-size 16 16 26`). A clean
  `git status assets/` afterwards means the committed asset is unchanged.

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
- **A gizmo handle owns exactly the pixels it is drawn on**
  (`cursorOnGizmoHandle()`, the GPU entity-id readback). A click beside a
  handle reaches the scene.
- A voxel's underside is never visible from the iso camera; Alt places on the
  far side of the picked face. That is a fact of the view, not of the pick.

The executor for "the face drawn under the cursor" is the
`*_pick_matches_render` assertion in `face_pick`: it compares the pick against
the main canvas's own face readback on every texel that shows the editable
set. An aim-based assertion alone cannot catch a pick that is offset from the
render, because the aim and the pick share one screen mapping.

## Authoring sessions

- Ctrl+P enters entity-scene mode and adds a voxel part; Ctrl+Shift+P adds the
  BAKE panel's selected SDF primitive. Tab/Shift+Tab select parts. Ctrl+S and
  Ctrl+O write/read the v2 prefab manifest plus one `.vxs` per voxel part.
- `EntityScene` owns one root and its ordered part records. Only the selected
  voxel part receives paint; other voxel parts carry `C_EditorReference`.
- A part's LOD band (`EditorPart::lodMax_` fine .. `lodMin_` coarse) is
  mirrored onto its own `C_VoxelSetNew` / `C_ShapeDescriptor`, so the engine's
  LOD gate previews which parts exist at a tier. Parts stay resident; the
  editor never runs `PREFAB_LOD_PARTS`. The LOD panel's TIER slider and `,` /
  `.` pin `C_LodTierOverride` on the root and every part; FOLLOW ZOOM removes
  it. Neither touches the camera zoom.
- Radial and linear arrays create independently editable parts. One array is
  one undo record; rotational symmetry replays a stroke across the radial
  copy group through the same stroke buffer. The selected source stays at the
  pivot outside the copy group.
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
- Entity sessions author on a bare stage; a session opts into the reference
  furniture with `Builder::withReferenceFurniture()` and then keeps its own
  aims off gizmo pixels (the model does not know where handles are drawn).
- A new session is four edits: `Session::Id`, `idFromName`, `Session::build`,
  and the `--gui-session` enum list in `main()`.

## Creation modules (`--module <dir>`)

- `ModuleHost` (`editor_lua_host.hpp`, whose header comment is the contract)
  runs `<dir>/init.lua` in the world's Lua VM in EVAL mode. A module registers
  components (`IRComponent.register`), recipes and panels (`IREditor.*`). A
  missing `init.lua`, a Lua error, or a component name already registered (a
  C++ one included) exits 2.
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
- Editor source names no module content: the module sessions read their
  expectations from `<dir>/session_expect.lua`. `test/module/` is the neutral
  fixture; `test/module_cpp_collision/` must exit 2.
