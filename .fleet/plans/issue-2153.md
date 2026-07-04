# Plan: fleet — claim-verified verdict apply (`fleet-review-verdict`)

- **Issue:** #2153 (opus-reviewer recheck verdict misroutes to the wrong PR)
- **Model:** opus (fleet infra + protocol semantics, careful sequencing)
- **Date:** 2026-07-04
- **Approach sign-off:** architect (human-delegated) 2026-07-04 — `human:review-plan`
  cleared; plan-review SOUND (agent gate cleared) 2026-07-01.

## Problem

The reviewer's verdict (`fleet:needs-fix` / `fleet:approved` / …) has landed on
the **wrong PR** ≥ twice (PR #2141, #2138): an Opus recheck verdict for the
PR-fixing-#2032 was posted, with its `fleet:needs-fix` label, onto an unrelated
PR. The result is internally contradictory (approve body + needs-fix label) and
elects the wrong PR into the worker lane, burning a no-op iteration. The reviewer
already holds an atomic `fleet:reviewing-<host>-<agent>` claim on the PR it
reviewed — nothing currently prevents applying the verdict to a *different* PR.

## Verdict-apply surface audit (origin/master)

`git grep` for both `fleet-transition verdict-*` callers **and** every
`--add-label`/`--remove-label` of a verdict-namespace label. Four ADD-a-verdict
surfaces exist (all guarded by this plan); five sites are de-scoped (remove-only /
mechanical / archived — none is a verdict apply):

1. `docs/agents/REVIEWER-PROTOCOL.md` (non-gated) — canonical reviewer verdict
   apply (both reviewer role docs defer here for the main verdicts). This is the
   path that misfired. **Worker lands this.**
2. `.claude/commands/role-sonnet-reviewer.md:201` (**gated**) — raw
   `gh pr edit … --add-label "fleet:needs-opus-recheck"` (sonnet escalation =
   `verdict-needs-opus-recheck`). **Human-applied (Option A).**
3. `.claude/skills/review-pr/SKILL.md:165-174` (**gated**, dual-use) — raw
   `gh pr edit` for approve / approve-nits / needs-fix / blocker. **Human-applied**
   (the interactive human invocation omits `--agent` — the carve-out).
4. `docs/agents/FLEET-CROSS-HOST-SMOKE.md:226` (non-gated, **worker lands**) +
   `.claude/commands/role-smoke-worker.md` step 5c (**gated**, human-applied) —
   smoke *failure* path. Same reviewing claim, same `verdict-needs-fix` class,
   same cross-number misroute vector (`platform-catchup` batch-swaps labels).

De-scoped (non-verdict): role-merger de-review remove-only; request-re-review
author reset; FLEET-FEEDBACK-HANDLING author amend; `fleet-claim:3621`
`__rebase_pause_chain` (mechanical `fleet:blocker` overload on a deterministic
chain target — no reviewing claim, guarding it would break rebase-downstream);
archived audit-roles.md.

## Approach — one PR

**A. New wrapper `scripts/fleet/fleet-review-verdict` (non-gated).**
Interface mirrors `fleet-transition`'s arg order:
`fleet-review-verdict <verdict-transition> <pr-number> [--agent <name>] [--repo <slug>] [--dry-run]`
- Validate `<verdict-transition>` ∈ {verdict-approve, verdict-approve-nits,
  verdict-needs-fix, verdict-blocker, verdict-needs-opus-recheck}; reject anything
  else ("not a verdict transition — use fleet-transition"). Guards the review-verdict
  lane only.
- **Claim check (the guard) — enforced only when `--agent <name>` is passed:**
  `host=$(fleet-claim host)` (reuse the canonical host key; do not re-derive the
  uname map), `label="fleet:reviewing-${host}-${name}"`, read live labels via
  `gh pr view <pr> [--repo] --json labels`. If absent → misroute diagnostic to
  stderr + exit non-zero (4). This is the seam.
- **Dual-use carve-out:** `--agent` omitted → skip the claim check, apply directly.
  The fleet reviewer/smoke loops always pass `--agent`; a human running review-pr
  interactively holds no claim and omits it. (Absence-of-`--agent`, NOT a
  `--force` flag — a flag an agent could add would defeat the guard.)
- On pass/carve-out: delegate to `fleet-transition <verdict-transition> <pr>
  [--repo] [--dry-run]`, propagate its exit code. `fleet-transition` stays the sole
  state-machine mechanism (idempotency + verify inherited).

**B. Register on PATH (non-gated):** `scripts/fleet/install.sh` — add
`FLEET_REVIEW_VERDICT_SRC`/`_DEST` next to the `FLEET_TRANSITION_*` pair, the
chmod-loop entry, AND a dedicated `ln -sf` block (mirror the full FLEET_TRANSITION
pattern — the loop alone only chmods, it does not symlink).

**C. Rewire the two non-gated apply surfaces (worker lands):**
- `docs/agents/REVIEWER-PROTOCOL.md` verdict lines →
  `fleet-review-verdict verdict-X <N> --agent <your-review-agent>`. Guards the
  **primary** misroute path with **zero gated edits**.
- `docs/agents/FLEET-CROSS-HOST-SMOKE.md:226` →
  `fleet-review-verdict verdict-needs-fix <N> --agent <smoke-worktree-name> --repo …`.
  (`verdict-needs-fix` removes a superset of `{approved,has-nits}` and adds
  `needs-fix` — behaviorally equivalent to the raw split.)

**D. Hermetic test:** `scripts/fleet/tests/test_fleet_review_verdict.sh`, patterned
on `test_fleet_transition.sh`. Stub `gh`/`fleet-transition`/`fleet-claim` on PATH.
Cases: `--agent` + claim present → delegates, exit 0; `--agent` + claim absent →
refuses non-zero, no label change; `--agent` omitted → applies (carve-out);
non-verdict transition name → rejected; `--dry-run`/`--repo` threaded through.

**E. Gated deltas — documented in the PR body for human application** (items 2, 3,
4-gated): the three one-line rewirings of `role-sonnet-reviewer.md:201`,
`review-pr/SKILL.md:165-174`, `role-smoke-worker.md` step 5c. A queued worker
cannot push gated self-config (deterministic commit gate); the PR body carries a
copy-pasteable checklist.

## Acceptance criteria

- `fleet-review-verdict verdict-needs-fix <PR> --agent <foreign-name>` on a PR NOT
  holding `fleet:reviewing-<host>-<foreign-name>` exits non-zero, changes no labels.
- Claim present → identical label delta to `fleet-transition verdict-needs-fix`,
  idempotent on re-run.
- `--agent` omitted → applies (carve-out); non-`verdict-*` name → rejected;
  `--repo`/`--dry-run` thread through; game path works with `--repo jakildev/irreden`.
- `test_fleet_review_verdict.sh` passes hermetically; `install.sh` symlinks
  `~/bin/fleet-review-verdict`.
- REVIEWER-PROTOCOL.md + FLEET-CROSS-HOST-SMOKE.md contain no remaining raw verdict
  `gh pr edit`; primary verdict path guarded with zero gated edits.
- PR body lists the three exact gated one-line edits for the human.

## Gotchas

- **"Verdict THEN release" ordering is load-bearing** — the guard's precondition is
  the reviewing claim being present, so the verdict apply must run before
  `fleet-claim review-release`. Do not reorder.
- **Reuse `fleet-claim host`, don't re-derive** the uname→host map (drift risk vs
  the claim-label token).
- **Do not over-guard the mechanical lane** — `fleet-claim`'s `__rebase_pause_chain`
  `fleet:blocker` stays on raw `gh pr edit`.

## Out of scope (follow-ups)

- Worker-side pre-claim heuristic (drop a misfired `fleet:needs-fix` when the
  triggering review body's scope doesn't match the PR area).
- Normalizing the `fleet:blocker` overload (item 8) into a distinct chain-paused
  signal.
</content>
</invoke>
