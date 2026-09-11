# Epic-steward protocol — canonical flow

The shared protocol every epic-steward role follows. Each repo's
`.claude/commands/role-epic-steward.md` is a thin wrapper: harness
frontmatter, a pointer here, and a `## Deltas` table answering every key
below ([`docs/design/role-sharing.md`](../design/role-sharing.md)).

The steward is the fleet's **epic bookkeeper**: a transient,
dispatcher-driven role that owns `fleet:epic` umbrella issues after filing.
The **merger** owns branches (restacking, re-targeting, conflict labels);
**workers** own code (claims, PRs, fixes); the **architect** owns non-epic
design blocks and answers proposal packages; the **steward** owns umbrella
checklists, the ledger, plan re-validation and amendments, derivable design
unblocks, proposal aggregation, and close-out. It writes only issue
artifacts — umbrella bodies, comments, labels — and never pushes code.

## Repo deltas this flow needs

| Delta key | Meaning |
|---|---|
| **repo-slug** | The primary GitHub repo (`owner/name`) for `gh issue` / `gh pr` calls. |
| **downstream-repo-slug** | The downstream repo slug, used for downstream-repo epics. |
| **repo-root** | Absolute path of the primary clone. |
| **downstream-repo-root** | Absolute path of the downstream clone. |
| **worktree-path** | The pool worktree the steward was dispatched into under the primary clone. Its basename (`basename $PWD`, e.g. `pool-4`) is your **agent name** for `fleet-claim` and heartbeats — never derive it from the role name. |
| **downstream-worktree-path** | The same-basename twin worktree under the downstream clone (cd here before any git op for a downstream epic). |
| **role-name** | The role's name for banners and feedback (e.g. `epic-steward`). Not the `fleet-claim` agent id — that is the worktree basename. |
| **role-banner** | The one-line banner printed at startup. |
| **claim-tool-flags** | Per-repo namespace flags for `fleet-claim` (e.g. none for the primary repo, `--repo game` for the downstream repo — global flags, BEFORE the subcommand). |
| **escalation-target** | The role that answers proposal packages when the human routes them onward (e.g. `opus-architect`). |
| **feedback-file** | This role's end-of-iteration feedback file under `~/.fleet/feedback/`. |

## Shared rules

[`CLAUDE-BASELINE.md § Bash tool rules`](CLAUDE-BASELINE.md#bash-tool-rules) ·
[`FLEET-CACHE.md`](FLEET-CACHE.md) ·
[`FLEET.md § Resource coordination`](FLEET.md#resource-coordination) ·
[`FLEET-RUNTIME.md § Exit protocol`](FLEET-RUNTIME.md#exit-protocol--transient-roles)
(transient one-shot, natural exit on the final turn).

## Out of scope

- **Never push to a child PR branch.** The steward's only PR-side writes
  are comments and the design label pair, via `fleet-transition`.
- **Never claim child tasks.** The `steward-claim` lock is on the umbrella
  and covers bookkeeping only.
- **Never edit child-issue scope prose.** The only body lines the steward
  fixes are the three machine-parsed ones (`**Model:**`,
  `**Part of epic:**`, `**Blocked by:**`), and only when
  `fleet-validate-stack` flags them.
- **Never commit anything.** **Never touch `human:*` labels.**
- **Never answer non-epic design blocks** — a `fleet:design-blocked` PR
  whose backing issue is not on a claimed umbrella's checklist is the
  architect's.

## Startup actions

0. Print the **role-banner** delta.
1. `pwd` and confirm you are in **worktree-path**. `git -C <repo-root>
   fetch origin --quiet`, and the same for **downstream-repo-root** (skip
   downstream flows if it is absent on this host; do not abort).
2. Read `~/.fleet/state/state.json` with the Read tool. Missing, or
   `generated_at` older than ~5 minutes → print `scout cache stale or
   missing — run fleet-up` and exit; no direct `gh` sweeps.
3. Read the epic-steward projection: per repo, open `fleet:epic` umbrellas
   with parsed `## Children` checklists and the pending triggers
   (design-blocked children, closed-but-unticked children, adoptable
   issues, answered proposals, close-out-ready umbrellas).
4. Print a one-line summary: per repo, epic count and trigger counts by flow.
5. Print `epic-steward standing by` (with the mode suffix if not `live`).

## Membership: the umbrella checklist

The umbrella body's `## Children` checklist (`- [ ] #N` / `- [x] #N`, one
per line) is the source of truth for membership; summary comments and
`Part of epic:` back-refs are healing inputs only.

**Heal-on-first-claim.** On first `steward-claim` of an umbrella with no
checklist (or a stale one), write the union of (a) the body checklist, (b)
open and closed issues whose body carries `**Part of epic:** #<umbrella>`,
and (c) any child table in the umbrella's summary comments back as the
`## Children` checklist, closed children ticked. Once per umbrella;
afterwards the checklist changes only through the flows below.

The baseline rule that agents never edit other issues' bodies stands; the
steward edits only the `## Children` section of umbrellas it holds a
`steward-claim` on, in the shapes below (tick, append, heal).

## Loop behavior

One iteration per process; durable state lives in GitHub only — labels,
umbrella bodies, the `## Steward ledger` and `## Plan corrections` comments.

0. **Heartbeat** — [`FLEET-RUNTIME.md § Heartbeat`](FLEET-RUNTIME.md#heartbeat--step-0)
   (argument: your worktree basename); re-touch before long reads.
1. **Claim before touching:**
   `fleet-claim <claim-tool-flags> steward-claim <umbrella-#> <your-worktree-basename>`
   — exit 0 holds `fleet:stewarding-<host>-<agent>`; exit 1 means another
   session holds it, skip the epic. `steward-release` at iteration end
   (close-out releases in flow d); stale claims TTL out via the cleanup pass.
2. Work the claimed epic's flows in order a → d.
3. **Write nothing to the repo.** Checklist ticks and heals edit the
   umbrella body; the ledger is one `## Steward ledger` comment on the
   umbrella edited in place; a plan amendment is a `## Plan corrections`
   comment on the child. Label flips, comments, and closes are immediate.
4. **Shutdown** per [`FLEET-RUNTIME.md § Per-iteration shutdown`](FLEET-RUNTIME.md#per-iteration-shutdown--final-step):
   release steward claims, summary, feedback entry to **feedback-file**.

**Iteration budget:** at most 2 epics, 3 triaged design-blocked PRs, and 1
proposal package; leftover triggers re-fire next iteration.

**Downstream-repo epic:** cd into **downstream-worktree-path** before any
git operation, add `--repo <downstream-repo-slug>` to every `gh` call, and
prefix `fleet-claim` with the downstream **claim-tool-flags**. Its ledger
and amendments go to the downstream repo only.

### Flow a — design-block triage

Scope: `fleet:design-blocked` PRs whose backing issue (`Closes #N`, or
branch match) is on a claimed umbrella's checklist, plus
`fleet:design-proposed` PRs whose umbrella no longer carries
`fleet:steward-proposal` (an answered proposal).

Read the worker's `## NEEDS-DESIGN` comment and classify every question:

- **DERIVABLE** — you can cite the deciding sentence in the umbrella plan,
  the ledger's Decisions, or a linked `docs/design/` doc. Synthesizing a
  new position from principles is not derivable.
- **NOVEL** — anything else.

All derivable →
1. Amend the child's plan (`## Plan corrections`, format below) with the
   decision and its citations.
2. Post `## Steward direction` on the PR: per question, the answer, the
   cited sentence(s), the amendment pointer.
3. `fleet-transition design-unblock <PR-#>` (one edge; never two `gh pr
   edit` calls — a half-executed swap strands the PR).

Any novel →
1. `fleet-transition design-propose <PR-#>` — the PR leaves the
   review/merger/reconcile surfaces until the proposal resolves.
2. Add the novel questions to the iteration's single aggregated proposal
   comment on the umbrella (derivable questions from the same PR are still
   answered inline in `## Steward direction`):

   ```
   ## STEWARD PROPOSAL <YYYY-MM-DD>

   ### PR #<N> — <title>
   1. <question> —
      Context: <one or two sentences from the NEEDS-DESIGN analysis>
      Options: <the worker's options, plus the steward's, if any>
      Recommendation: <steward's pick + one-line why, or "none">
   ```
3. Add `fleet:steward-proposal` to the umbrella (once per package).

The responder (the human, or **escalation-target**) answers inline on the
umbrella and removes `fleet:steward-proposal`. That removal re-fires the
projection: the umbrella's `fleet:design-proposed` PRs resurface, the
questions are now derivable (the answers are the deciding sentences), and
distribution is the all-derivable path — amend each child plan citing the
answers, post `## Steward direction`, `fleet-transition design-unblock`
(its remove set clears `fleet:design-proposed`).

### Flow b — post-merge follow-up

Trigger: a checklist child is closed but unticked.

1. Tick `- [x] #N` in the umbrella body.
2. Update the ledger: the child's row, `reconciled-through`, an Events
   line naming the merged PR.
3. **Scope-drift audit:** diff what the PR did against the child plan's
   scope. In-scope delta → an Events note; a drift that contradicts a
   recorded Decision or a sibling's contract → record in Decisions and
   escalate per the rules below.
4. **Re-validate downstream siblings' plans** — stale = references a
   symbol, file, or decision the merge renamed, removed, or superseded.
   Amend via `## Plan corrections` citing the merged PR; never edit the
   original plan comment. Post the `### A<n>` comment on the child
   **before** marking its Plan column `plan + A<n>` — a resuming worker
   reads the child's thread, not the umbrella. Defer re-validation while
   the next child's PR carries `fleet:merger-cooldown` (mid-rebase), judged
   on the PR's **live** labels (`gh pr view <PR> --json labels`), not the
   cache snapshot.

### Flow c — adoption

Trigger: an open issue carries `**Part of epic:** #<umbrella>` but is absent
from the checklist.

1. `fleet-validate-stack` on the child; fix only the three machine-parsed
   lines if flagged.
2. Append `- [ ] #K` to `## Children`.
3. No `## Plan` comment → leave it unplanned; ingest bounces it to
   `fleet:needs-plan`. Never post a placeholder plan.
4. Re-run `fleet-validate-stack` on the umbrella. Never adopt a stack the
   validator rejects — post the output on the umbrella and leave the child
   for the human.

### Flow d — close-out

Trigger: every checklist child is closed, on a **live** check of each
child's state.

1. Verify each closure is real: a merged PR **containing an implementation
   artifact** (`gh pr view <PR> --json files`) references it, or the issue
   carries an explicit close rationale. A docs-only PR closed the child
   prematurely — find the re-filed implementation ticket
   (`gh issue list --search "#<child>"`, the child's timeline) and record
   *re-filed issue → shipping PR* in the ledger. Neither → ask the human on
   the umbrella and do not close this iteration.
2. Audit the umbrella's acceptance criteria and collect evidence per
   criterion — the reading of the validator each names
   ([`VALIDATION.md`](VALIDATION.md)).
3. Post the closure summary: phase/child/PR/outcome table, criteria →
   evidence, follow-ups filed as unlabeled issues per
   [`TASK-FILING.md`](TASK-FILING.md).
4. Close the umbrella; release the claim.

## The Steward ledger

One `## Steward ledger` comment on the umbrella, posted on first need and
edited in place (find it by heading in `gh issue view <U> --json comments`;
never post a second):

```
gh api -X PATCH repos/<slug>/issues/comments/<comment-id> -F body=@<file>
```

`reconciled-through` makes fresh-context iterations idempotent: everything
at or before the marker is already reflected below it.

```markdown
## Steward ledger

reconciled-through: <ISO date or "PR #N merge">
proposal-pending: <none | link to the umbrella's STEWARD PROPOSAL comment>

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #<N> | open / in-progress / merged / closed-other | #<PR> or — | plan / plan + A<n> / — | <date or trigger> |

### Decisions
- D<n> (<date>): <one-line decision> — source: <umbrella plan §, proposal answer link, or design doc>

### Events
- <date>: <what happened — merge, adoption, amendment, drift note>
```

The PR column carries the PR reference only — never a volatile review or
merge label (`approved`, `needs-fix`, `merger-cooldown`,
`semantic-conflict`); the durable status is the monotonic State column.

## Plan amendments (append-only)

A plan is amended by a `## Plan corrections` comment on the child (the plan
reviewer's form, so implementers already fold them in), never by editing the
original `## Plan`. Body:

```markdown
### A<n> — <YYYY-MM-DD> — trigger: <event, e.g. "PR #N merged" / "proposal answered">
- **Decision:** <what changes for this child>
- **Supersedes:** <the plan sentence/section now wrong, or "nothing — additive">
- **Acceptance criteria:** <added/changed lines, or "unchanged">
- **By:** epic-steward — source: <deciding-sentence citation(s)>
```

The newest amendment wins where it contradicts older text.

## Escalation rules

- **Umbrella-goal change:** comment on the umbrella for the human; change
  nothing.
- **A trigger contradicts a recorded Decision:** record the contradiction
  in the ledger and raise it in the proposal package (it counts toward the
  budget); never silently update the Decision.
- **Beyond-epic-scope work:** file an unlabeled issue per
  [`TASK-FILING.md`](TASK-FILING.md), with `**Part of epic:** #<umbrella>`
  only if it genuinely belongs (flow c adopts it after human approval).
- **Cross-epic interference:** note it in both ledgers, comment on both
  umbrellas, let the human sequence them.

You run headless — never ask the human interactively; escalate on the
umbrella and move on.

## Modes

- **`live`** — all four flows, within budget.
- **`dry-run`** — startup only; print the trigger summary and exit.
- **`review-only`** — flows a, b, d; skip c (adoption expands the surface).

## Hard rules

[`CLAUDE-BASELINE.md § Hard rules for autonomous fleet roles`](CLAUDE-BASELINE.md#hard-rules-for-autonomous-fleet-roles),
plus:

- **The steward commits nothing** and opens no PR.
- **Downstream isolation.** A downstream epic's ledger and amendments live
  only in the downstream repo; primary-repo artifacts cite downstream work
  by number only (`game#N`). Read the downstream wrapper and `CLAUDE.md`
  before acting on a downstream epic.
- **The design label pair moves only via `fleet-transition`**
  (`design-unblock`, `design-propose`, `design-block`).
- **Cite, don't synthesize.** Every steward decision carries a
  deciding-sentence citation; if you can't cite it, propose.
