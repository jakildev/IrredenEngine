# Epic #213 — Entity creation mode (steward record)

`#213` was filed **pre-protocol** (2026-04-19): its execution plan lives in
`docs/design/entity-editor-epic.md` plus the umbrella body (Status / Phase
children / Original acceptance / Architectural decisions locked / Engine vs.
game split), and no `.fleet/plans/issue-213.md` was ever committed. This file
closes that gap — it carries the **steward ledger only**; the design doc and
the umbrella body remain the authoritative plan.

## Why the projection never surfaced this epic

#213's eleven children were enumerated as a **prose markdown table** under
`## Phase children` (`| 0 | Foundation … | #603 | — |`), not as a
`- [ ] #N` checklist. `fleet-state-scout::_parse_epic_checklist` returns `[]`
for that shape, so the epic-steward projection reported `#213 [0/0]` and
**emitted no trigger of any kind** — not `rollup`, not `normalize`, not
`closeout` — for the 3.3 months between filing and this heal. Two children
closed in that window (#603, #605) with no rollup.

This is the same blind spot that hid a pre-protocol umbrella in the downstream
repo until 2026-07-28 (downstream #192). The umbrella now carries a
machine-parsed `## Children` checklist; the prose table is retained beneath it
as the human-readable phase/dependency view.

Membership is the eleven phase sub-epics only; per-phase implementation tickets
are grandchildren. See **D1** below for the rule and its citation.

## Steward ledger

reconciled-through: 2026-07-28 (first steward claim — heal + membership audit; close-out NOT READY — 9 of 11 children open, only Phase 0 and Phase 2 done)
proposal-pending: none

### Children

| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #603 | closed — Phase 0 Foundation | — (sub-epic; shipped via #619–#628) | umbrella body + design doc | 2026-07-28 — closure audited, see F1 |
| #604 | open — **in progress** (Phase 1, Static voxel authoring) | — (sub-epic; #761–#766 in flight) | umbrella body + design doc | 2026-07-28 — adopted |
| #605 | closed — Phase 2 Hierarchies & skeletal voxels | — (sub-epic; shipped via #1602–#1612) | `.fleet/plans/issue-605.md` | 2026-07-28 — closure audited, see F2 |
| #606 | open — not started (Phase 3, Animation timeline) | — | umbrella body + design doc | 2026-07-28 — adopted |
| #607 | open — not started (Phase 4, IK + chain solvers) | — | umbrella body + design doc | 2026-07-28 — adopted |
| #608 | open — not started (Phase 5, Bind-points + component attachment) | — | umbrella body + design doc | 2026-07-28 — adopted |
| #609 | open — not started (Phase 6, Procedural / variant authoring) | — | umbrella body + design doc | 2026-07-28 — adopted |
| #610 | open — not started (Phase 7, Particles / lights / audio bind-points) | — | umbrella body + design doc | 2026-07-28 — adopted |
| #611 | open — not started (Phase 8, Multi-window / polish) | — | umbrella body + design doc | 2026-07-28 — adopted |
| #612 | open — not started (Phase 9, Lua editor scripting) | — | umbrella body + design doc | 2026-07-28 — adopted |
| #613 | open — not started (Phase 10, Voxel rep / perf review) | — | umbrella body + design doc | 2026-07-28 — adopted |

Build order (umbrella body): 0 → 1 → 2 → 3 → 4 → 5, then 6–10 fan out from
Phase 5 independently.

### Decisions

- D1 (2026-07-28): #213's checklist membership is the **eleven phase sub-epics
  #603–#613**; per-phase implementation tickets are **grandchildren** — they
  stay off this checklist and are tracked on the phase sub-epic that owns them
  — source: umbrella body §Phase children table + "each filed as
  its own `fleet:epic`-labeled child issue" / "Implementation tickets per phase
  will be filed when the phase begins, not all up-front". Derivable, recorded
  rather than proposed.
- D2 (2026-07-28): close-out of #213 gates on all eleven sub-epics closing, and
  each sub-epic's own close-out gates on its implementation tickets — the
  umbrella's "Original acceptance" list is **Phase 1 (#604) scope**, not #213's
  own criteria — source: umbrella body §Original acceptance ("preserved as
  Phase 1 scope") + §Phase children row 1 ("delivers original #213 scope").

### Findings

- **F1 — #603 (Phase 0) closed COMPLETED 2026-05-18 with no close-out
  rationale comment.** Audited 2026-07-28: **closure is real.** All ten Phase-0
  tickets (#619 epic doc + #620–#628 implementation) are closed COMPLETED, and
  the three the phase's own acceptance names — F-0.8 editor exe scaffold
  (#622), F-0.4 3D editor camera (#625), F-0.9 voxel mouse picking (#628) —
  closed 2026-05-12/2026-05-14, before the epic's own close. F-0.5 gizmo
  primitives (#627) closed 2026-05-19, one day *after* #603, but is not named
  in the Phase-0 acceptance sentence. Ticked on that basis.
- **F2 — #605 (Phase 2) closure is real and self-documenting.** Its close
  carries an explicit architect close-out comment (2026-07-04, human-delegated
  triage) citing all eleven re-planned children #1602–#1612 merged by
  2026-06-11. No audit gap.
- **F3 — plan-file coverage is uneven but not a gap.** Only Phase 2 has a
  committed `.fleet/plans/issue-605.md`; the other phases plan at phase start
  via their implementation tickets, per the umbrella's own declaration. No
  stubs filed — a stub per D1 would be filed on an implementation ticket, not
  on a phase sub-epic that is not yet started.
- **F4 — `fleet-validate-stack` rejects all eleven children (22 errors, 10
  warnings): every one is missing a standalone `**Model:**` and
  `**Part of epic:** #213` line.** Child bodies were **not** edited, and the
  residual is the human's / architect's call. Reasoning:
  - `**Part of epic:** #213` and `**Blocked by:** #<predecessor>` *are*
    derivable (the umbrella's `## Phase children` table declares both, and
    engine sub-epics #1884 and #1272 set the precedent of a `fleet:epic` child
    carrying the back-ref), so they fall inside the steward's sanctioned
    three-line carve-out.
  - `**Model:**` is **not** derivable. Neither the umbrella body nor
    `docs/design/entity-editor-epic.md` assigns a model class per phase — the
    `[opus]` / `[sonnet]` tags in the Phase-0/Phase-1 comments are on
    *implementation tickets*, not phases. Choosing one would be synthesis, not
    citation.
  - Since the stack cannot be made to pass without that human input, fixing
    only two of the three lines would leave the validator FAILED anyway. Per
    flow c step 4 ("never adopt a stack the validator rejects — post the
    validator output and leave it for the human"), the output is posted on the
    umbrella instead.
  - **The heal is unaffected.** Membership's source of truth is the umbrella
    checklist, not the back-refs (protocol §Membership), and the eleven
    children were already declared by the umbrella's own table — the heal
    changed that declaration's *encoding*, it did not adopt new members.
  - **Category caveat for whoever rules:** `fleet-validate-stack` encodes
    *task*-child expectations. #213 is an epic-of-epics, and a `**Model:**`
    line on a phase sub-epic may simply not be meaningful — the implementation
    tickets under each phase carry their own. "Exempt sub-epics from the Model
    check" is a legitimate resolution alongside "assign a model per phase".

### Events

- 2026-04-19: filed pre-protocol as a static voxel painter; scope expanded
  during planning into the eleven-phase entity creation mode. No `## Children`
  checklist and no `.fleet/plans` entry — invisible to the steward projection
  from this point forward.
- 2026-05-18: **#603 (Phase 0) closed COMPLETED** — foundation shipped (trixel
  UI primitives, layout, input routing, editor camera, gizmos, voxel picking,
  editor exe scaffold, per-voxel metadata, JSON sidecar). No rollup fired.
- 2026-07-04: **#605 (Phase 2) closed COMPLETED** via architect-run close-out —
  bind-pose + skin matrices, joint authoring, per-voxel skinning, `.rig`
  save/load, FK pose editing, skeletal demo entities, render-verify. No rollup
  fired.
- 2026-07-28: **first steward claim — heal.** Built the `## Children` checklist
  from the umbrella's own `## Phase children` table (the protocol's "child
  table" input), ticked #603 and #605 after auditing both closures (F1/F2),
  adopted the nine open phases, and opened this ledger. **No scope prose
  edited** — the heal added a `## Children` section and left the phase table,
  acceptance list, and locked architectural decisions untouched.
