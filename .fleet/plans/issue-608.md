# Epic #608 — Entity editor Phase 5: Bind-points & component attachment

**Type:** `fleet:epic` umbrella ledger (no task plan — the phase's work is
carried by its children; this file exists to hold the ledger).
**Umbrella:** #213 (CLOSED/COMPLETED 2026-07-04)

## Steward ledger

reconciled-through: 2026-09-10 (first steward claim — heal-on-first-claim)
proposal-pending: https://github.com/jakildev/IrredenEngine/issues/608#issuecomment-5619958711

### Children

| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #669 | merged | #686 | — | 2026-09-10 |
| #671 | merged | #703 | — | 2026-09-10 |
| #698 | merged | #992 | — | 2026-09-10 |
| #700 | merged | #720 | — | 2026-09-10 |
| #701 | merged | #718 | — | 2026-09-10 |
| #721 | merged | #729 | — | 2026-09-10 |

Position: **6 of 6 closed — and the epic is NOT close-out-ready.** See F3:
two of the six sub-tasks in the umbrella body own acceptance that no child
issue has ever owned, so an all-ticked checklist here is not evidence of a
finished phase. Do not read this table as a close-out trigger.

### Decisions

- D1 (2026-09-10): the epic's membership is the six issues above. All six
  declare it as `- Parent epic: #608`, a spelling no discovery path parses
  (F1) — source: each child's body, read directly, corroborated by the
  2026-07-04 scope note on #608 which names #669/#671/#700/#701 by number.

### Findings

- **F1 (2026-09-10) — third membership spelling.** All six children declare
  `- Parent epic: #608`; `fleet-validate-stack 608 --state all` returns
  **`0 child issue(s)`** against a #1881 control returning 4. The epic
  showed `[0/0]` for 122 days. Same root cause as #604's F1; appended to
  **#3141**, not filed fresh.

- **F2 (2026-09-10) — the declared blocker was abandoned, not satisfied.**
  #608's body carries `## Blocked by — #607 (Phase 4 — IK + chain solvers)`.
  **#607 is CLOSED/NOT_PLANNED.** The blocking edge is therefore void while
  the prerequisite it named was never delivered. Context: of the nine
  entity-editor phase epics, **#606, #607, #609 and #611 are all
  CLOSED/NOT_PLANNED**, #605 is CLOSED/COMPLETED, the grand umbrella #213 is
  CLOSED/COMPLETED, and #604/#608/#610/#612/#613 remain open. Neither
  remaining sub-task (5.2, 5.4) has an IK dependency, so nothing here is
  blocked in substance — but the line as written is wrong and is the kind of
  line that gets copied onto the next ticket.

- **F3 (2026-09-10) — two sub-tasks own acceptance that no child owns.**
  Measured against `origin/master`, per sub-task:

  | Sub-task | State | Evidence |
  |---|---|---|
  | 5.1 `C_BindPoints` component | **shipped** (#700) | `engine/prefabs/irreden/voxel/components/component_bind_points.hpp` |
  | 5.2 bind-point gizmos (place + name a marker) | **partial** | primitive exists — `IRPrefab::Gizmo::createBindPointMarker` (`engine/prefabs/irreden/render/gizmo.hpp:312`), instantiated once at `creations/editors/voxel_editor/main.cpp:3330`. No place-and-name authoring workflow; `rig_scene_io.hpp:11` states bind points are "a separate concept outside the joint-authoring tool" |
  | 5.3 `entity:bindPoint("name")` Lua | **shipped** (#700) | `IREntity.bindPoint(entity, name)` in `engine/script/src/lua_script.cpp`, per `component_bind_points_lua.hpp:4` |
  | 5.4 component palette UI | **absent** | the editor's only palette is the 16-swatch **colour** palette from F-1.1 (`main.cpp:148-154`, `kPaletteCount = 16`). No component-registry UI anywhere: `getComponentTypeByName` has exactly two consumers on master — `lua_script.cpp:868` and `world_snapshot.cpp:201` — neither a UI |
  | 5.5 prefab Lua format | **shipped** (#671) | `engine/script/src/lua_bindings_prefab.cpp`, `prefab_api.cpp` |
  | 5.6 `Prefab.spawn` | **shipped** (#701/#721) | `prefab_api.cpp` routes a prefab's `voxel_ref` to ECS components |

  The umbrella's acceptance is a **conjunction**: "Spawn a prefab from Lua at
  a world position; `entity:bindPoint("<named_anchor>")` returns the correct
  world-space transform. **The component palette serializes attached
  components into the prefab Lua and round-trips.**" The first two clauses
  are discharged by #700/#671/#701/#721. The third cannot be discharged by
  anything, because 5.4 does not exist and no open issue owns it (searched
  `component palette` and `bind-point` across open issues — the only hits are
  the phase epics themselves).

- **F4 (2026-09-10) — #214 is closed COMPLETED while its acceptance was
  delegated, not delivered.** The 2026-07-04 adoption note on #608 records
  that #214 (prefab browser) "was closed as folded into this phase", with
  sub-task 5.4 absorbing its read-only browse/inspect acceptance. But **#214's
  state reason is COMPLETED**, its timeline shows a hand close with
  `commit_id: null` and no closing PR, and its criteria 1–5 (enumerate
  registered component types with name + header path; enumerate system types;
  substring filter; per-component field/type inspector; per-system template
  inspector) are measured **absent** on master by the same evidence as 5.4
  above. Its criterion 6 (dear-imgui) is correctly dead per #213's
  trixel-GUI ruling. The residual slice the note itself calls "an optional
  follow-on within the editor" — system-type enumeration against the
  `SystemName` enum — is likewise unowned. Net: a COMPLETED issue is the
  only record of five undelivered criteria that now depend on a sub-task with
  no ticket.

- **F5 (2026-09-10) — #608 gates another open epic.** #610 (Phase 7 —
  particles/lights/audio bind-points) carries a 2026-09-10 sweep note:
  "Stays parked pending Phase 5 (#608)." So #608 is not a leaf that can be
  closed on its own merits — whatever happens to it has to leave #610's gate
  pointing at something real.

### Events

- 2026-05-10: epic filed, `## Blocked by — #607`.
- 2026-05-13 → 2026-05-20: all six children merged (the backend/runtime half
  of the phase, delivered early via the asset + prefab tracks).
- 2026-07-04 (triage): #214 closed and folded into 5.4; scope note posted
  recording that the remaining scope is "the editor authoring surface only:
  5.2 and 5.4".
- 2026-07-04 → 2026-09-10: untouched for 68 days. No trigger of any kind
  fired, because the checklist was empty (F1).
- 2026-09-10: first steward claim. `## Children` healed to the six members
  above (all ticked). Findings F1–F5 recorded. **STEWARD PROPOSAL** raised on
  the umbrella and `fleet:steward-proposal` applied — the phase's remaining
  scope is an umbrella-goal question (build the editor-authoring half, or
  retire it as its four sibling phases were retired), which the steward does
  not decide.
