# Plan: skills: file-epic seeds Children checklist + Steward ledger at filing time (P7)

- **Issue:** #1668
- **Model:** sonnet
- **Date:** 2026-06-10
- **Epic:** #1661 — see `.fleet/plans/issue-1661.md` for full context
- **Blocked by:** #1662

## Scope

New epics are born managed: the `file-epic` canonical flow writes the
umbrella body `## Children` checklist and seeds the `## Steward ledger`
section in the umbrella plan, so the steward's heal pass is only ever needed
for pre-protocol epics.

## Affected files

- `docs/agents/skills/file-epic.md` — the ONLY file. Both repos'
  `.claude/skills/file-epic/SKILL.md` wrappers inherit via runtime
  indirection; the ledger is shared fleet infrastructure, so no new delta
  keys (verify by diffing the wrappers' delta tables before/after — they
  must not change).

## Approach

- Step 2 (save the umbrella plan): append the initial `## Steward ledger`
  (schema verbatim from `docs/agents/epic-steward-protocol.md`:
  `reconciled-through:` = filing date, `proposal pending: none`,
  `### Children` rows seeded `open`, `### Decisions` empty or seeded from
  the architect plan's user decisions, `### Events` = "filed via
  file-epic").
- New step 3.5 (after the epic label, before child filing — or as one body
  edit after step 5; pick the single-edit form): write the `## Children`
  checklist into the umbrella body, `- [ ] #N — <short title>` per child.
  Row format stays plain: number immediately after the checkbox (the scout
  regex from #1664 takes the first `#N` on the line).
- Step 7 (summary comment): one added line pointing at the ledger section.
- Step 7.5: reference `--check-checklist` (#1667) as the post-filing drift
  assert.
- Anti-patterns: add the "filing without checklist/ledger" entry.

## Acceptance criteria

- The flow's prescribed checklist and ledger formats agree VERBATIM with the
  epic-steward protocol's schemas (heading text, table columns, row format).
- Neither repo's SKILL.md wrapper needs any change.
- A dry-run walk of the updated flow against epic #1661's actual filing
  reproduces what was done manually there (the worked example).

## Gotchas

- #1661 itself was filed pre-P7 with the checklist and ledger added
  manually — it is the worked example to validate against, not a format to
  copy blindly (its plan file carries an architect-plan preamble new epics
  won't have).
- Keep `Blocked by:` machine-parseability warnings intact — this file is
  load-bearing for queue ingestion; edit surgically.

## Verification

- Read-through against `docs/agents/epic-steward-protocol.md` schema
  sections — exact heading + column match.
- `git diff` shows zero changes outside `docs/agents/skills/file-epic.md`.
