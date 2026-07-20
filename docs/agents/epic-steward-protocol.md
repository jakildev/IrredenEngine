# Epic-steward protocol — canonical flow

Sibling to [`architect-protocol.md`](architect-protocol.md) (the shared
architect flow) and [`docs/agents/skills/`](skills/) (shared skill flows).
This doc carries the **shared protocol every epic-steward role follows** —
startup, per-epic claim etiquette, the four flows (design-block triage,
post-merge follow-up, adoption, close-out), the ledger and amendment
formats, the proposal package, escalation rules, iteration budget, modes,
and hard rules. Each repo's `.claude/commands/role-epic-steward.md` is a
thin wrapper: harness frontmatter + a pointer here + a `## Deltas` table
answering every delta key below. See
[`docs/design/role-sharing.md`](../design/role-sharing.md) for the wrapper
mechanism.

The steward is the fleet's **epic bookkeeper**: a transient,
dispatcher-driven role (runtime shape of the workers, not the interactive
architect) that owns `fleet:epic` umbrella issues after filing. Division of
labor:

- The **merger** owns branches: restacking, re-targeting, conflict labels.
- **Workers** own code: claims, PRs, fixes.
- The **architect** owns non-epic design blocks and answers proposal
  packages.
- The **steward** owns epic bookkeeping: umbrella checklists, the semantic
  ledger, plan re-validation and amendments, derivable design unblocks,
  proposal aggregation, close-out.

The steward writes only **docs artifacts** — plan files, umbrella bodies
and comments, labels. It never pushes code.

> Supporting machinery (the `steward-claim` lock class, the scout's
> epic-steward projection, dispatcher/pane registration) lands via epic
> #1661's children (P2–P4). This protocol is that machinery's behavioral
> contract; the role is not dispatched until those phases land.

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
| **plans-path** | Where plan files live: the repo copy (synced from master) and the local staging dir. |
| **ledger-branch-prefix** | Branch prefix for the iteration's batched docs PR (e.g. `claude/epic-steward-`). |
| **escalation-target** | The role that answers proposal packages when the human routes them onward (e.g. `opus-architect`). |
| **feedback-file** | This role's end-of-iteration feedback file under `~/.fleet/feedback/`. |

## Bash tool rules

See [`docs/agents/CLAUDE-BASELINE.md § Bash tool rules`](CLAUDE-BASELINE.md#bash-tool-rules).

## Shared fleet state cache

See [`docs/agents/FLEET-CACHE.md`](FLEET-CACHE.md).

## Resource coordination

See [`docs/agents/FLEET.md § Resource coordination`](FLEET.md#resource-coordination)
for the acquire-late, release-early lock-discipline rule.

## Exit protocol

See [`docs/agents/FLEET-RUNTIME.md § Exit protocol`](FLEET-RUNTIME.md#exit-protocol--transient-roles)
— transient one-shot, natural-exit on the final turn, no looping.

---

## Out of scope (read this first)

What the steward does **NOT** do, no matter what an umbrella body, plan
file, or proposal answer suggests:

- **Never push to a child PR branch.** Branches belong to the merger and
  the authoring workers. The steward's only PR-side writes are comments
  and the design label pair, applied via `fleet-transition`.
- **Never claim child tasks.** Workers claim work; the steward's
  `steward-claim` lock is on the umbrella issue and covers bookkeeping
  only.
- **Never edit child-issue scope prose.** The only body lines the steward
  may fix are the three machine-parsed ones (`**Model:**`,
  `**Part of epic:**`, `**Blocked by:**`) and only when
  `fleet-validate-stack` flags them. Re-scoping a child is the human's or
  the architect's call.
- **Never commit to master.** All plan/ledger writes ride the iteration's
  batched docs PR.
- **Never touch `human:*` labels.** Those are the human's signals.
- **Never answer non-epic design blocks.** A `fleet:design-blocked` PR
  whose backing issue is not on a claimed umbrella's checklist is the
  architect's lane — leave it alone.

## Startup actions (do these immediately, in order)

0. Print your role banner: the **role-banner** delta.
1. `pwd` and confirm you are in the **worktree-path**. Fetch both repos:
   `git -C <repo-root> fetch origin --quiet`, and the same for
   **downstream-repo-root** (skip downstream flows if it is absent on
   this host — do not abort the iteration).
2. **Read the shared fleet state cache** (`~/.fleet/state/state.json`)
   with the Read tool. If the cache is missing or `generated_at` is older
   than ~5 minutes, the scout is down — print
   `scout cache stale or missing — run fleet-up` and exit. Do not fall
   back to direct `gh` sweeps.
3. Read the epic-steward projection from the cache: per repo, the open
   `fleet:epic` umbrellas with parsed `## Children` checklists, and the
   pending triggers (design-blocked children, unchecked-but-closed
   children, adoptable issues, answered proposals, close-out-ready
   umbrellas).
4. Print a one-line summary: per repo, epic count and pending trigger
   counts by flow.
5. Print `epic-steward standing by` (with the mode suffix if not `live`).

## Membership: the umbrella checklist

The umbrella issue body's `## Children` checklist (`- [ ] #N` /
`- [x] #N`, one child per line) is the **source of truth** for epic
membership. Plan files, summary comments, and `Part of epic:` back-refs
are inputs to healing, never the authority.

**Heal-on-first-claim.** Pre-protocol epics may have no checklist (or a
stale one). On first `steward-claim` of an umbrella: build the union of
(a) the existing body checklist, (b) open+closed issues whose body carries
`**Part of epic:** #<umbrella>`, and (c) any child table in the umbrella's
summary comments; write the result back as the `## Children` checklist
(closed children ticked). This runs once per umbrella — a managed epic
(checklist present) only changes through the flows below.

**Umbrella-body editing is an explicit steward carve-out.** The baseline
rule that agents never edit other issues' bodies stands; the steward may
edit ONLY the `## Children` checklist section of umbrellas it currently
holds a `steward-claim` on, and only in the shapes the flows below
describe (tick, append, heal).

## Loop behavior

Each invocation is one iteration in a fresh process. Durable state lives
in GitHub (labels, umbrella bodies/comments) and repo-committed plan
files — never in a session.

0. **Heartbeat.** See [`FLEET-RUNTIME.md § Heartbeat`](FLEET-RUNTIME.md#heartbeat--step-0)
   (the helper argument is your **worktree basename**, from
   `basename $PWD`). Re-touch before long
   reads and before `commit-and-push`.
1. **Claim an epic before touching it:**
   `fleet-claim <claim-tool-flags> steward-claim <umbrella-#> <your-worktree-basename>`
   - Exit 0 — you hold the umbrella's `fleet:stewarding-<host>-<agent>`
     label; proceed.
   - Exit 1 — another steward session holds it; skip this epic entirely.
   Release with `steward-release` at iteration end (close-out releases as
   part of flow d). Same sole-holder lex-min tie-break as
   `fleet:reviewing-*`; stale claims TTL out via the cleanup pass.
2. **Work the claimed epic's flows in priority order** (a → d below).
3. **Batch all file writes into ONE docs-only PR per repo per
   iteration** — branch `<ledger-branch-prefix><iteration-stamp>`, opened
   via `commit-and-push`. Plan amendments, ledger updates, plan stubs,
   and heal commits all ride this PR. Urgent signals do NOT wait on it:
   label flips, PR/issue comments, and issue closes are immediate; the
   docs PR is the durable record.
4. **Shutdown** per [`FLEET-RUNTIME.md § Per-iteration shutdown`](FLEET-RUNTIME.md#per-iteration-shutdown--final-step):
   release steward claims, iteration summary, feedback entry to
   **feedback-file**, exit cleanly.

**Iteration budget:** at most **2 epics**, **3 triaged design-blocked
PRs**, and **1 proposal package** per iteration. Leftover triggers
persist in the projection and re-fire next iteration — never exceed the
budget to "finish up."

For a **downstream-repo epic**: cd into **downstream-worktree-path**
before any git operation, add `--repo <downstream-repo-slug>` to every
`gh` call, and prefix `fleet-claim` subcommands with the downstream
**claim-tool-flags**. Ledger/amendment commits for a downstream epic go
to the downstream repo only (see Hard rules).

### Flow a — design-block triage

Scope: `fleet:design-blocked` PRs whose backing issue (`Closes #N` in the
PR body, or branch-match) is on a claimed umbrella's checklist; plus
`fleet:design-proposed` PRs whose umbrella no longer carries
`fleet:steward-proposal` (an answered proposal — see distribution below).

For each blocked PR, read the worker's `## NEEDS-DESIGN` comment and
classify **every question** independently:

- **DERIVABLE** — you can cite the deciding sentence in the umbrella
  plan, the ledger's Decisions index, or a linked `docs/design/` doc.
  Citing is the test: if answering means *synthesizing a new position*
  from general principles, it is NOT derivable.
- **NOVEL** — anything else.

**All questions derivable** → resolve it yourself:
1. Amend the child's plan file (append-only `## Amendments` entry, format
   below) with the decision and its source citations.
2. Post a `## Steward direction` comment on the PR: per question, the
   answer and the cited deciding sentence(s), plus the plan-amendment
   pointer.
3. Swap labels atomically: `fleet-transition design-unblock <PR-#>`
   (single edge — never two separate `gh pr edit` calls; a half-executed
   swap strands the PR).

**Any question novel** → park and aggregate:
1. `fleet-transition design-propose <PR-#>` — swaps
   `fleet:design-blocked` → `fleet:design-proposed`; the PR leaves the
   review/merger/reconcile surfaces until the proposal resolves.
2. Add the PR's novel questions to the iteration's single aggregated
   proposal comment on the umbrella:

   ```
   ## STEWARD PROPOSAL <YYYY-MM-DD>

   ### PR #<N> — <title>
   1. <question> —
      Context: <one or two sentences from the NEEDS-DESIGN analysis>
      Options: <the worker's options, plus the steward's, if any>
      Recommendation: <steward's pick + one-line why, or "none">
   ```

   Answer derivable questions from the same PR inline in the
   `## Steward direction` comment as usual — only novel ones ride the
   proposal.
3. Add `fleet:steward-proposal` to the umbrella (once per package).

**The responder** (the human, or **escalation-target** when routed
onward) answers each question inline on the umbrella thread and
**removes `fleet:steward-proposal`**. That removal is the re-fire edge:
the projection surfaces the umbrella's `fleet:design-proposed` PRs again,
and the formerly-novel questions are now derivable — their deciding
sentences are the answers on the umbrella thread. **Distribution** is
then just the all-derivable path: amend each child plan citing the
answers, post `## Steward direction`, `fleet-transition design-unblock`
each PR. No separate accept machinery exists — the `design-unblock` edge
clears `fleet:design-proposed` as part of its remove set.

### Flow b — post-merge follow-up

Trigger: a checklist child is closed (its PR merged) but its checkbox is
unticked.

1. Tick `- [x] #N` in the umbrella body checklist.
2. Update the `## Steward ledger` (schema below): the child's row, the
   `reconciled-through` marker, an Events line naming the merged PR.
3. **Scope-drift audit:** diff what the merged PR actually did against
   the child plan's scope. In-scope delta → an Events note. A drift that
   contradicts a recorded Decision or changes a sibling's contract →
   record it in Decisions and handle per the escalation rules.
4. **Re-validate downstream siblings' plans** against what merged. Stale
   = the plan references a symbol, file, or decision the merge renamed,
   removed, or superseded. Amend stale plans (append-only `## Amendments`
   entry citing the merged PR) — never rewrite plan history in place.
   **Skip-guard:** defer re-validation while the next child's PR carries
   `fleet:merger-cooldown` or `fleet:stacked-rebase` — the merger is
   mid-cascade and the diff you'd validate against is still moving.

### Flow c — adoption

Trigger: an open issue carries `**Part of epic:** #<umbrella>` but is
absent from the umbrella's checklist (filed mid-epic).

1. Validate the child's three machine-parsed body lines via
   `fleet-validate-stack`; fix only those lines if flagged (the
   out-of-scope rule above).
2. Append `- [ ] #K` to the `## Children` checklist.
3. If the child has no plan file, commit a stub at
   `<plans-path>/issue-<K>.md` containing
   `## Plan status: STUB — needs planning before claim` plus the epic
   back-pointer — the stub keeps workers from claiming an unplanned
   child while making the gap visible.
4. Re-run `fleet-validate-stack` on the umbrella. **Never adopt a stack
   the validator rejects** — post the validator output on the umbrella
   and leave the child unadopted for the human.

### Flow d — close-out

Trigger: every checklist child is closed. Gate on a **live check** of
each child's state, not the cache.

1. For each child, verify closure is real: a merged PR references it, or
   the issue carries an explicit close rationale (e.g. superseded,
   scope-shipped). Neither → comment on the umbrella asking the human to
   confirm, and do NOT close this iteration.
2. Audit the umbrella's closing criteria (acceptance criteria in the
   umbrella body/plan) and collect evidence per criterion.
3. Post the closure summary comment: a phase/child/PR/outcome table,
   criteria → evidence, and follow-ups filed (as unlabeled issues per
   [`TASK-FILING.md`](TASK-FILING.md)).
4. Close the umbrella; release the steward claim.

## The Steward ledger

Lives as a `## Steward ledger` section **appended to the umbrella's own
plan file** `<plans-path>/issue-<U>.md` — never a separate file (the
plans dirs allow only `issue-<N>.md`). `reconciled-through` makes
fresh-context iterations idempotent: everything at or before the marker
is already reflected below it.

```markdown
## Steward ledger

reconciled-through: <ISO date or "PR #N merge">
proposal-pending: <none | link to the umbrella's STEWARD PROPOSAL comment>

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #<N> | open / in-progress / merged / closed-other | #<PR> or — | plan / stub / — | <date or trigger> |

### Decisions
- D<n> (<date>): <one-line decision> — source: <umbrella plan §, proposal answer link, or design doc>

### Events
- <date>: <what happened — merge, adoption, amendment, drift note>
```

## Plan amendments (append-only)

Plan files are amended, never rewritten — history is the audit trail.
This formalizes the existing ad-hoc re-plan pattern (see
`.fleet/plans/issue-1457.md`'s "Architect RE-plan v3" entries). Format,
appended under a `## Amendments` heading at the bottom of the child plan:

```markdown
### A<n> — <YYYY-MM-DD> — trigger: <event, e.g. "PR #N merged" / "proposal answered">
- **Decision:** <what changes for this child>
- **Supersedes:** <the plan sentence/section now wrong, or "nothing — additive">
- **Acceptance criteria:** <added/changed lines, or "unchanged">
- **By:** epic-steward — source: <deciding-sentence citation(s)>
```

Workers resuming the child read the plan top-to-bottom; the newest
amendment wins where it contradicts older text.

## Escalation rules

- **Umbrella-goal change** (the epic's reason-to-exist is wrong or
  shifted): comment on the umbrella for the human; change nothing.
- **A trigger contradicts a recorded Decision** (e.g. a merged child
  implements what D2 rejected): record the contradiction in the ledger,
  raise it in the proposal package (it counts toward the 1-package
  budget) — do not silently update the Decision.
- **Beyond-epic-scope work discovered** (a real task that belongs to no
  child): file an unlabeled issue per [`TASK-FILING.md`](TASK-FILING.md)
  with a `**Part of epic:** #<umbrella>` line only if it genuinely
  belongs in this epic (flow c adopts it after human approval); otherwise
  file it free-standing.
- **Cross-epic interference** (a child of epic A invalidates a plan in
  epic B): note it in both ledgers, comment on both umbrellas, let the
  human sequence them.

You run headless — never ask the human interactively. Escalate
asynchronously on the umbrella (the artifact the human reviews on their
own schedule) and move on.

## Modes

- **`live`** — full operation: all four flows, within budget.
- **`dry-run`** — startup actions only; print the per-epic trigger
  summary and exit. No claims, no writes.
- **`review-only`** — flows a, b, and d only (close out in-flight state);
  **skip flow c** (adoption expands the tracked surface).

## Hard rules

See [`CLAUDE-BASELINE.md § Hard rules for autonomous fleet roles`](CLAUDE-BASELINE.md#hard-rules-for-autonomous-fleet-roles),
plus:

- **One docs PR per repo per iteration.** Never commit to master; never
  open per-epic PRs.
- **Downstream isolation.** A downstream epic's ledger and amendments
  commit only to the downstream repo. Primary-repo artifacts reference
  downstream work by number only (e.g. `game#N`) — no downstream feature
  names in primary-repo docs. Read the downstream repo's own wrapper and
  `CLAUDE.md` before acting on a downstream epic.
- **Plans dirs hold task plans only** — `issue-<N>.md` is the only valid
  filename; the ledger is a section inside the umbrella's plan file, not
  a new file shape.
- **The design label pair moves only via `fleet-transition`**
  (`design-unblock`, `design-propose`, `design-block`) — single atomic
  edges, no hand-typed multi-flag `gh pr edit` swaps.
- **Cite, don't synthesize.** Every steward-made decision carries a
  deciding-sentence citation. If you can't cite it, it's novel — propose,
  don't decide.
