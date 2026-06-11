# Plan: fleet: epic-steward canonical protocol, role-sharing doc, engine wrapper, labels + transitions (P1)

- **Issue:** #1662
- **Model:** opus
- **Date:** 2026-06-10
- **Epic:** #1661 — see `.fleet/plans/issue-1661.md` for full context
- **Blocked by:** (none)

## Scope

The all-docs phase: canonical role protocol, role-sharing design doc, engine
role wrapper, label catalog + state machine + reference updates, and the
hand-off edits in architect-protocol.md / FLEET.md. Docs-only PR — if any
change wants to touch a script other than the `fleet-labels` catalog array,
it is mis-scoped (that work is P2/P3).

## Affected files

- `docs/agents/epic-steward-protocol.md` (new — the centerpiece)
- `docs/design/role-sharing.md` (new)
- `.claude/commands/role-epic-steward.md` (new)
- `scripts/fleet/fleet-labels` — `LABELS=(...)` array (~line 99): two new
  entries; fix the stale `fleet:epic` description (~line 105) which credits
  auto-close to the retired queue-manager
- `docs/agents/fleet-state-machine.json` — two label nodes; new
  `design-propose` edge; `design-unblock` (~line 394) and `design-block`
  (~line 382) remove-sets gain `fleet:design-proposed`
- `docs/agents/fleet-labels-reference.md` — register both labels next to the
  design-blocked entry (~line 297); rewrite the `fleet:epic` bullet
  (lines 27–37)
- `docs/agents/architect-protocol.md` — steward-first paragraph in the
  design-blocked handling section
- `docs/agents/FLEET.md` — design-escalation flow gains the steward branch

## Approach

**Protocol doc structure** (full drafted skeleton in the planning record;
key sections):

1. Header: division of labor (merger = branches, workers = code, architect =
   non-epic blocks + proposals, steward = epic bookkeeping; steward writes
   only docs artifacts — plans, umbrella bodies/comments, labels).
2. `## Repo deltas this flow needs` table: repo-slug, downstream-repo-slug,
   repo-root / downstream-repo-root, worktree-path / downstream-worktree-path,
   role-name, role-banner, claim-tool-flags, plans-path,
   ledger-branch-prefix, escalation-target, feedback-file.
3. Out-of-scope list: never push to a child PR branch; never claim child
   tasks; never edit child scope prose (only the three machine-parsed lines
   when validate-stack flags them); never commit to master; never touch
   `human:*` labels; never answer non-epic design blocks.
4. Startup: banner → pwd-confirm + fetch both repos → state.json stale-cache
   guard → projection read → one-line summary → standing by.
5. Membership: umbrella body `## Children` checklist is the source of truth;
   heal-on-first-claim (union checklist + `Part of epic` search + summary-
   comment table). Umbrella-body editing is an explicit steward carve-out,
   claimed umbrellas only.
6. Loop: heartbeat → per-epic claim (`fleet-claim <flags> steward-claim
   <umbrella> <role-name>`; exit 1 → skip) → flows in order (a) design-block
   triage, (b) post-merge follow-up, (c) adoption, (d) close-out → batch all
   plan/ledger writes into ONE docs PR per repo per iteration → shutdown per
   FLEET-RUNTIME.md. Budget: ≤2 epics, ≤3 triaged PRs, ≤1 proposal package.
7. Flow a: classify each NEEDS-DESIGN question DERIVABLE (can cite the
   deciding sentence — synthesizing a new position is NOT derivable) vs
   NOVEL. All-derivable → plan amendment + `## Steward direction` PR comment
   + single atomic label swap (one `gh pr edit` with both --remove-label and
   --add-label, or `fleet-transition design-unblock`). Any-novel → swap to
   `fleet:design-proposed`, one aggregated `## STEWARD PROPOSAL <date>`
   comment on the umbrella, add `fleet:steward-proposal` to the umbrella.
   Responder answers inline and removes the label; the projection re-fires;
   formerly-novel questions are then derivable — no new machinery.
8. Flow b: tick `- [x] #N`, ledger update, scope-drift audit on the merged
   PR, re-validate downstream plans (stale = references a symbol/file/
   decision the merge renamed/removed/superseded), amend stale plans.
   Belt-and-suspenders skip: defer re-validation while the next child's PR
   carries `fleet:merger-cooldown`/`fleet:stacked-rebase`.
9. Flow c: validate structured fields via validate-stack, append `- [ ] #K`,
   plan stub ("## Plan status: STUB — needs planning before claim") if
   missing, re-run validate-stack; never adopt a stack the validator rejects.
10. Flow d: gate = every checklist child closed (live check). Verify each
    closed via merged PR or explicit rationale (neither → comment asking the
    human, do NOT close). Audit umbrella closing criteria with evidence.
    Closure summary comment (phase/child/PR/outcome table + criteria →
    evidence + follow-ups). Close umbrella, release claim.
11. `## Steward ledger` schema (reconciled-through / proposal pending /
    Children table / Decisions / Events) and append-only `## Amendments`
    format (`### A<n> — <date> — trigger: <event>` + Decision / Supersedes /
    Acceptance criteria / By) — formalizes the existing "Architect RE-plan
    v3" pattern in `.fleet/plans/issue-1457.md`.
12. Escalation rules: umbrella-goal changes, contradicting recorded
    decisions, beyond-epic-scope work (file an unlabeled issue), cross-epic
    interference. Modes: live / dry-run / review-only (no adoption).

**role-sharing.md**: mirror `docs/design/skill-sharing.md` section-for-
section — problem, decision (canonical `docs/agents/<role>-protocol.md`
declaring the delta table; thin wrappers answering every key), shared-vs-
delta line, why runtime indirection, wrapper template, baseline delta-key
set, **new-role checklist** (engine wrapper in same PR + downstream-wrapper
follow-up issue filed on the game repo before merge), reference
implementations (architect retrofit + epic-steward born canonical), future
(lint auto-filing; protocol versioning).

**Engine wrapper**: frontmatter (name/description) + pointer to the protocol
+ Deltas table (engine values: `jakildev/IrredenEngine` / game cache key,
worktree `~/src/IrredenEngine/.claude/worktrees/epic-steward`,
escalation-target `opus-architect`, ledger-branch-prefix
`claude/epic-steward-`, feedback `~/.fleet/feedback/epic-steward.md`) +
downstream-isolation section (read the game wrapper before acting on a game
epic; reference downstream work as `game#N` only).

**Hand-off edits**: architect-protocol gains "steward-first for epic
children" (engage only on `fleet:steward-proposal` or human direction;
check the umbrella for a steward claim before manually unblocking an
epic-child PR). FLEET.md design-escalation flow gains the steward branch.

## Acceptance criteria

- `fleet-labels --check` passes (catalog ↔ JSON ↔ reference in sync — they
  must change in the same commit).
- `fleet-transition design-propose <PR>` resolves against the JSON.
- Wrapper answers every key the protocol's delta table declares.
- Downstream-wrapper follow-up issue filed on the game repo (per the
  checklist this PR introduces), body naming only the protocol path and the
  delta keys to answer.

## Gotchas

- Reuse `design-unblock` as the proposal-accept edge — delta semantics make
  the extra `fleet:design-proposed` remove a no-op on plain unblocks. No
  separate accept edge.
- Protocol phrasing: "downstream repo", never game feature names.
- The two-label split is deliberate: `fleet:design-proposed` (PR) gives the
  projection a clean off-edge + reconcile exclusion; `fleet:steward-proposal`
  (umbrella) is the human queue whose REMOVAL re-fires distribution. Don't
  collapse them into one.

## Verification

- `scripts/fleet/fleet-labels --check` (offline check) passes.
- `python3 -c` smoke: load `fleet-state-machine.json`, assert the
  `design-propose` edge and both label nodes exist.
- Grep the wrapper's Deltas keys against the protocol's table — every bold
  key answered (manual until #1667 lands the lint).
