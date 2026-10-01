# creations/editors/voxel_editor/ — the voxel editor

Inherits [`creations/CLAUDE.md`](../../CLAUDE.md) and the engine baseline.
Design log, findings and the authoring-session history:
[`docs/design/editor-authoring-friction.md`](../../../docs/design/editor-authoring-friction.md).

## Validators

- `python3 scripts/gui-verify.py IRVoxelEditor` — the standing GUI shot table.
- `python3 scripts/gui-verify.py IRVoxelEditor -- --gui-session <name>` — one
  scripted session: `face_pick` and `drag_probe` prove the picking contract
  against the full reference scene, `place_below` the Alt modifier.
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

- A recipe names cells; `Session::Builder` (`session_builder.hpp`) aims each
  click and mirrors the editable set in `OccupancyModel`, which casts the same
  `castGridRay` the live pick runs. Change the pick and the model follows; do
  not teach the model a rule the pick does not have.
- A segment's assertions evaluate once, after all of its events. Arm a
  pre-click check in its own segment.
- Entity sessions author on a bare stage; a session opts into the reference
  furniture with `Builder::withReferenceFurniture()` and then keeps its own
  aims off gizmo pixels (the model does not know where handles are drawn).
