# Voxel editor validation

## Validators

- `actions_workflow`, then `actions_restart` with the same isolated `IR_PREFS_DIR`, validate action search, invocation, conflict replacement, badges and keymap persistence. Run closes Actions before executing in the editor; the replay also checks modal tool-key isolation, camera reset, self-toggle and close/reopen. Actions coordinates resolve against the live GUI canvas.

- `material_tools` exercises closed-outline fill, open-outline rejection, Brush, sampled and selected Recolor, and undo/redo through the visible controls at 30°/67°. `shapes_workflow` checks all five primitives and dimension entry through Shapes, including stamp and drag placement.
- `--profile-frames N --profile-yaw DEGREES --profile-zoom ZOOM` times a fixed 219-cell voxel shadow scene without captures and exits. UI remains present; AO, construction grid and hover feedback are off. The standard profile report includes wall-frame and GPU-stage samples; `--no-stage-timing` supplies an instrumentation control. The editor enables continuous surface lighting by default; `IR_PERAXIS_SURFACE_LIGHTING=0` selects sampled receiving for comparisons, and `1` selects continuous receiving explicitly.
- `palette_workflow`, `array_workflow`, and `lod_workflow` exercise palette edit/add/GPL round trips, prospective array geometry through Apply/Undo, and actual LOD captures/onion skin/derive/Undo respectively. Use `--gui-layout-check` with each.
- `python3 scripts/gui-verify.py IRVoxelEditor` — blank-grid sculpt workflow; `-- --gui-layout-check` also fails layout findings. Add `--reference-scene` for the diagnostic furniture/legacy shot table.
- `python3 scripts/gui-verify.py IRVoxelEditor -- --gui-session <name>` runs a named scripted session; `sessions.hpp` owns the complete registry. Core authoring flows are `sculpt_workflow`, `hierarchy_workflow`, `animation_boundary`, `sculpt_single_save`, `local_canvas`, `document_workflow`, `workarea_focus` and `motion_preview`. Include `--gui-layout-check` to gate reachability/layout. `region_tools` selects Box through Shapes and Line through the toolbar and checks floor preview, exact commit, Undo and Escape at 30°/67°, including a floor obscured by a different editable part. `camera_gestures` checks camera chords, modifier-first release, interrupted strokes, fresh editing and undo. `click_orbit` drives right, Alt+left and Ctrl+middle orbit over solid surfaces and the empty floor at seven angles, including 120°/240°; it checks cursor-derived focus, visible markers, press/release continuity, reanchoring and unchanged authored voxels, with pan/zoom preludes. `place_below` validates inward placement. `face_pick` compares actual rendered faces with picking at yaw zero, and `text_input_command_capture` checks editor shortcuts under text focus.
- `python3 scripts/gui-verify.py IRVoxelEditor -- --module <dir>
  --module-session <name>` — one session registered by a creation module.
  `test/module` registers `test_parts`, `test_recipe_attach`, `test_class_paint`,
  `test_recipe_handle`, `test_recipe_handle_turned`, `test_radius`, `test_list_scroll`,
  `test_single`, `test_recipe_pages`, `test_click_after_focus`, and the failing `test_unreachable` / `test_click_under_panel`; the header contract
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

## Session authoring contracts

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

World-aimed left presses emitted by `Session::Builder` are checked against the editor UI gate on their consume tick. A swallowed press fails its segment and names the operation, target and GUI position. `test_click_under_panel` must fail that check; `test_click_after_focus` must pass after returning focus to the scene.
