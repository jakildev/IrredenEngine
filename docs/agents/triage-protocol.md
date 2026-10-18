# Triage protocol — issue triage against the standing objectives

The judgment layer between "an issue exists" and "the human approves it".
Triage reads every untriaged open issue (no `fleet:` / `human:` label),
judges it against the target repo's **objectives-path** and the filing
rules, and produces a verdict the human acts on in one read. Two modes
share the same inputs and judgment:

| Mode | Who | Output | Approval |
|---|---|---|---|
| **Dispatched** | the transient `triage` role, on the singleton host | a `## Triage` comment + the label set `fleet-triage-sweep stamp` earns it | `fleet:agent-approved` for a graduated class with its bar cited (§ Graduation); otherwise none — the human acts from the digest |
| **Architect-managed sweep** | an architect session, on human cue | a staged report under `~/.fleet/triage/`, labels applied after the human confirms | the human's, executed by the session; a graduated entry stamps `fleet:agent-approved` |

Neither mode writes `human:approved`. The five graduated classes
(§ Graduation) stamp `fleet:agent-approved` through one executable judge;
every other verdict waits for the human.

## Repo deltas this flow needs

| Delta key | Meaning |
|---|---|
| **repo-slug** | The target GitHub repo (`owner/name`) for `gh issue` calls. |
| **repo-root** | Absolute path of that repo's clone. |
| **worktree-path** | The pool worktree the role was dispatched into. Its basename (`basename $PWD`) is the agent name for heartbeats — never derive it from the role name. |
| **role-name** | The role's name for banners and feedback (e.g. `triage`). |
| **role-banner** | The one-line banner printed at startup. |
| **objectives-path** | Where the repo's standing objectives live — the direction issues are judged against. |
| **singleton-env** | The env flag designating the one host that runs the dispatched mode (e.g. `FLEET_TRIAGE=1`). |
| **feedback-file** | This role's end-of-iteration feedback file under `~/.fleet/feedback/`. |

The `fleet:*` / `human:*` vocabulary is shared infrastructure
([`fleet-labels-reference.md`](fleet-labels-reference.md)); the per-repo
classification reference is **objectives-path**.

## Hard rules

- **Never close an issue, never retitle or re-scope a body.** A
  recommend-close verdict is staged or commented, never executed. The one
  body write the architect-managed sweep may make is **adding** a standalone
  `**Blocked by:** #<issue>` line (one `#N` per line; never removing one)
  when the basis cites an open PR that edits the same files and names its
  backing issue — the entry records it as `blocked_by_added` and a comment
  on the issue carries the citation. Dispatched mode never writes a body.
- **Never approve on the role's own judgment.** Dispatched mode writes
  labels only through `fleet-triage-sweep stamp`, which grants
  `fleet:agent-approved` to a graduated class with its bar cited
  (§ Graduation) and `fleet:triage-recommend` to everything else; sweep
  mode writes labels only from a staging file the human confirmed, and a
  graduated entry there passes the same judge. Never `human:approved`, and
  never `fleet:queued` / `fleet:task` / verdict labels (scout- and
  ingest-owned — [`TASK-FILING.md`](TASK-FILING.md)).
- **Read-only on the tree** — grep, file reads, `gh` queries; no builds, no
  runs.
- **One target repo per run**, never citing another repo's private content
  in output that lands on a public repo ([`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md)
  § Cross-repo information isolation). Staging files live under
  `~/.fleet/`, outside every tree.

## Inputs

1. Every `Status: active` objective under **objectives-path**. A repo with
   no objectives degrades to defect-shaped / park classification and says so.
2. The untriaged set (the predicate `fleet-decisions` surfaces as the
   untriaged cue), minus the mode's idempotency skips.
3. The issue thread in full — body and comments.
4. [`TASK-FILING.md`](TASK-FILING.md) (the structured-body bar) and
   [`fleet-labels-reference.md`](fleet-labels-reference.md).

Oldest first, at most **10 issues per run**.

## Per-issue judgment

An issue's premise is a claim: grep the cited files and symbols, check
whether a named defect is already fixed on `master`, search for an open
duplicate. Then exactly one verdict:

- **recommend-approve** — serves an active objective (name it and the
  Done-means row) or is defect-shaped with verifiable forensics, scoped to a
  plannable surface. Include a suggested `**Model:**` and, if the body lacks
  it, the `**Objective:** <slug>` line to add.
- **park (needs-human)** — direction-shaped (new capability, public API,
  design choice), crosses a Non-goal, targets gated self-config, or has a
  premise you could not verify either way.
- **recommend-close** — duplicate (cite the open issue) or already shipped
  (cite the merged PR or the master evidence).
- **insufficient-info** — name exactly what is missing (repro command,
  observed output, the file); the comment must let the filer fix it in one
  edit.

A recommend-approve verdict also names its **class**: one of the five
graduated classes in § Graduation when every bar line can be cited, else
`direction`.

## Dispatched mode

Outputs a `## Triage` comment and one label set, both written by
`fleet-triage-sweep stamp`: `fleet:agent-approved` plus the model label
(and `fleet:no-plan` for `verification-run`) for a recommend-approve
verdict in a graduated class whose bar is cited (§ Graduation),
`fleet:triage-recommend` for everything else. Never
`human:approved`, `fleet:queued`, or a label written by hand.

- **Singleton by designation:** runs only on the host carrying the
  **singleton-env** flag (the `FLEET_EPIC_STEWARD` pattern).
- **Idempotent by guard:** skip any issue that already carries a `fleet:`
  or `human:` label, or has a `## Triage` comment; `stamp` refuses those
  and closed issues too, and re-reads state and labels before its label
  write so a close or state label that lands mid-stamp still wins.

```markdown
## Triage

**Verdict:** recommend-approve | park | recommend-close | insufficient-info
**Class:** rebless | docs-capture | plan-residual | verification-run | objective-row | direction
**Objective:** <slug + Done-means row, or "(none — defect-shaped)" or "(none — see verdict)">
**Suggested model:** opus | sonnet | (n/a)
**Bar:**
- <key>: <citation>    (one line per key the class requires; omit the block for direction)
**Basis:** <2-5 lines: what you verified in the tree, what you searched,
what the issue serves or duplicates — citations, not vibes>
```

Write the comment to a file and run
`fleet-triage-sweep stamp --repo <repo-slug> <N> --comment-file <file>`: it
posts the comment, re-reads the issue, applies the label set unless it
closed or a `fleet:` / `human:` label landed in between, and logs the stamp. A recommend label routes the issue into the
`fleet-decisions` digest; the human acts with ordinary mechanics
(`human:approved`, close, `human:owned`, a reply) and removes the label,
which re-arms the guard for a re-triage. A graduated stamp shows once in
`fleet-decisions` § "Agent-approved since last digest".

Invocation (cue "triage sweep", or a cron one-shot on the designated host):

```
cd ~/src/IrredenEngine/.claude/worktrees/pool-0 && \
  FLEET_TRIAGE=1 fleet-dispatch-wrap pane-0 "$FLEET_MODEL_OPUS" high triage "" live
```

Engine-only until a downstream fleet adds its own `role-triage.md` wrapper;
the sweep mode needs no wrapper.

## Architect-managed sweep

Runs inside the target repo's architect session, so the human is present:
enumerate, judge, stage; the human confirms; then labels are written.
**Cue-driven only** ("triage sweep [repo]").

### 1. Enumerate

```
fleet-triage-sweep list --repo <repo-slug>
```

Prints the untriaged set oldest-first, annotating any issue that appears in
a prior staging file under `~/.fleet/triage/` with that verdict and date.

### 2. Judge

§ Per-issue judgment against **objectives-path**. No `## Triage` comment,
no `fleet:triage-recommend`; the report is the output and the untriaged
predicate is the idempotency guard (an applied label removes the issue from
the set; a rejected entry re-surfaces, annotated).

### 3. Stage

Write `~/.fleet/triage/<repo>-sweep-<date>.json` and present it in
conversation:

```json
{
  "repo": "owner/name",
  "generated_at": "<ISO-8601>",
  "human_confirmed": false,
  "entries": [
    {
      "number": 123,
      "title": "…",
      "verdict": "recommend-approve",
      "labels": ["human:approved", "fleet:sonnet"],
      "basis": "2-5 lines of citations, not vibes",
      "class": "rebless",
      "bar": {"measurement": "…", "host": "macos", "confinement": "…"},
      "labels_at_staging": [],
      "blocked_by_added": ["#NNN (PR #MMM edits the same files)"],
      "confirmed": false
    }
  ]
}
```

`blocked_by_added` is optional and informational: it records a dependency
line the session added under § Hard rules; `apply` does not write it.
`class` and `bar` are required on an entry that stages
`fleet:agent-approved` (§ Graduation). `labels_at_staging` records the
`fleet:` / `human:` labels the issue already carried when staged, for an
entry that reaches the sweep through the authority table rather than the
untriaged list.

Proposed labels draw from a narrow allowlist the tool enforces:
`human:approved`, `human:no-plan`, `human:owned`, `fleet:agent-approved`
(a graduated class only, beside its model label), `fleet:no-plan` (a
graduated `verification-run` only, which must stage it), and one of
`fleet:sonnet` / `fleet:opus` / `fleet:fable`. `fleet:queued` and `fleet:task` are absent
by design. A park verdict whose basis names the open PR or the human
session that owns the item stages `human:owned`, so the issue leaves the
untriaged set instead of re-surfacing every sweep.

### 4. Confirm

The human confirms or rejects per issue or in batch. Set top-level
`human_confirmed` to `true` and each accepted entry's `confirmed` to `true`;
rejected entries stay `false` and are not applied.

### 5. Apply

```
fleet-triage-sweep apply --repo <repo-slug> ~/.fleet/triage/<file>.json [--dry-run]
```

Refuses unless both markers are set. Before each write it re-fetches labels
and skips any issue that gained a `fleet:` / `human:` label since staging
(beyond the entry's `labels_at_staging`); a `fleet:agent-approved` entry
whose class or bar fails the judge refuses the whole apply. Applied issues
append to `~/.fleet/triage/log.jsonl` with their class and a timestamp;
`fleet-decisions` reads the graduated stamps from it. **Closes stay
human-executed**: a recommend-close entry is reported with a ready-to-run
`gh issue close` line; neither `list` nor `apply` closes anything.

### 6. Extended sweep — backlog drains

Two sibling human-gated backlogs — the `fleet:coding-improvement` pile and
the `~/.fleet/feedback/` channel — have cue-only drain skills
(`triage-coding-improvements`, `review-fleet-feedback`); `fleet-decisions`
flips their cues to OVERDUE past the thresholds (12 open tickets; a
`.last-reviewed` marker 14+ days old or absent). After step 5, check
`fleet-decisions` and run each drain whose cue shows, issues first. The
sweep cue carries the drain cues; the dispatched mode never runs them.

## Graduation

Autonomous approval is earned: the human audits verdict quality over a
window, then authorizes bounded classes. The owner graduated the five
classes below on 2026-10-17 (#4388). Direction-shaped work (a new
capability, a public API, a Non-goal crossing, a design choice, an
objective amendment, fleet self-config) stays recommend-only, and no agent
ever stamps `human:approved`.

| class | the verdict cites, one `**Bar:**` line per key |
|---|---|
| `rebless` — reference re-bless / render-verify refresh | `measurement`: the measured table (match %, max delta, PSNR) on `host`; `confinement`: the rule in the criteria (one framebuffer pixel) |
| `docs-capture` — docs capture of a posted ruling | `ruling`: the architect, steward or design-answerer comment it captures; `grep`: the search showing the doc lacks it on master |
| `plan-residual` — plan-named residual of an approved issue | `plan`: the approved `## Plan` comment; `decision`: the decision line that carves it out |
| `verification-run` — verification run / parity measurement | `criteria`: runnable; `host`: named; `no_plan`: exactly `fleet:no-plan` |
| `objective-row` — objective Done-means row | `objective`: the active objective file; `row`: verbatim; `filer`: `architect` or `steward` |

`scripts/fleet/fleet_triage_class.py` is the one judge of the verdict
(exactly `recommend-approve`; a suffixed or extended value never stamps)
and the bar, and both writers (`fleet-triage-sweep stamp` and `apply`)
call it before any approval label leaves them. A graduated verdict stamps `fleet:agent-approved`
plus the model label, and a `verification-run` also `fleet:no-plan` so
ingest queues it without a plan; `fleet:agent-approved` is never removed and is the
audit trail. Every stamp lands once in `fleet-decisions` § "Agent-approved
since last digest"; the human's veto is ordinary mechanics (close,
`human:owned`, `fleet:needs-human`). Two weeks after the first stamp the
audit list is posted on the follow-up issue; zero vetoes keeps the classes.
The `fleet-unblock` authority table
([`skills/fleet-unblock.md`](skills/fleet-unblock.md)) carries the same
five rows for the cued sweep, beside its defect-shaped, epic-child and
human-filed rows.
