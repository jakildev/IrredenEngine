---
name: review-fleet-feedback
description: >-
  Read agent-to-human feedback files (~/.fleet/feedback/<role>.md), cluster
  recurring snags into patterns, draft concrete fix proposals (which role
  doc, script, or setting to change), and run a closed-loop check that
  previously proposed fixes have stopped recurring before declaring them
  done. Reads everything newer than ~/.fleet/feedback/.last-reviewed,
  tracks proposed/applied/recurring/closed fixes in
  ~/.fleet/feedback/.fix-log.jsonl, then bumps the marker on completion
  so the same entries don't resurface. Use when the user says "review
  fleet feedback", "check fleet snags", "summarize fleet feedback", "what
  has the fleet been complaining about", "any new fleet snags", or after
  a stretch of autonomous fleet activity when the human wants to absorb
  the backlog. Do NOT invoke proactively — only on explicit ask.
---

# review-fleet-feedback

## Purpose

Fleet agents (`opus-worker-*`, `sonnet-reviewer`, `merger`, `queue-manager`, etc.) append timestamped notes to `~/.fleet/feedback/<role>.md` whenever they observe something the human should know about: fleet bugs, missing permissions, recurring failure patterns, skill UX friction, suggestions. The channel is one-way and easy to forget about.

This skill turns it into a closed loop:

1. Pull every entry newer than the `.last-reviewed` marker.
2. Cluster them into recurring patterns with stable signatures.
3. For each cluster, check `.fix-log.jsonl` — if a previous fix targeted this signature and the cluster has new entries since the fix was applied, the fix didn't work. Surface as **RECURRING**.
4. For fresh clusters with ≥2 occurrences, draft concrete fix proposals.
5. For previously applied fixes with no recurrence in ≥7 days, auto-promote to `closed`.
6. Ask the human which proposals to log, which previously-proposed fixes have been applied, then bump the marker.

## State files

| Path | Purpose |
|---|---|
| `~/.fleet/feedback/<role>.md` | Source feedback entries (one file per role). Read-only here. |
| `~/.fleet/feedback/.last-reviewed` | ISO 8601 timestamp — the watermark for "I've seen everything up to here." |
| `~/.fleet/feedback/.fix-log.jsonl` | One JSON object per line. The closed-loop tracker. |

Both state files live in `~/.fleet/feedback/` (not in the repo) and are managed entirely by this skill. They are not committed.

## Preconditions

1. `~/.fleet/feedback/` exists and contains at least one `<role>.md` file. If not, report "no fleet feedback yet" and exit.
2. `~/bin/fleet-feedback` is installed (it is by `scripts/fleet/install.sh`). If not, fall back to reading the role files directly.

## Flow

### 1. Read the read-watermark

```bash
MARKER=$(cat ~/.fleet/feedback/.last-reviewed 2>/dev/null || echo "")
```

- If `MARKER` is empty or unparseable, default to a 7-day window. Tell the user: "No `.last-reviewed` found — defaulting to last 7d. The marker will be created on completion."
- If `MARKER` is present, compute the duration from `MARKER` to `now()` in hours, then pad by 1h for clock skew. Pass that as `--since Nh` to `fleet-feedback` in step 2.

Duration math (Python one-liner). The marker is written by `date -u` (step 8), so the comparison must also be in UTC — using `datetime.now()` here would skew `--since` by the host's UTC offset:
```bash
SINCE=$(python3 -c "
from datetime import datetime
m = datetime.fromisoformat('$MARKER')
h = int((datetime.utcnow() - m).total_seconds() // 3600) + 1
print(f'{h}h')
")
```

Cap the duration: if `MARKER` is more than 90 days stale, cap `SINCE` at `90d` and warn the user — anything older than 90 days is no longer load-bearing for current fixes.

### 2. Pull new entries

```bash
~/bin/fleet-feedback --since "$SINCE"
```

This prints entries in chronological order, one block per entry:
```
[2026-05-14 16:49] [sonnet-reviewer] <headline>
    <optional body line 1>
    <optional body line 2>
```

If the output is empty, skip to step 8 (still surface the open fix-log and bump the marker).

### 3. Cluster by signature

Group entries by matching their headline + first body line against this pattern bank. Each cluster's `signature` is the join key against the fix-log — it must stay stable across runs.

| Signature | Regex (case-insensitive against headline + first body line) |
|---|---|
| `label-absent-after-verdict` | `label\s+absent\s+after\s+verdict` |
| `stale-task-row` | `T-\d+\b.*\b(stale\|stranded\|no-op\|stale-row)` |
| `worktree-tracker-drift` | `fleet-worktree-busy-branches.*(drift\|stale\|missed)` |
| `state-cache-lag` | `(cache\|projection).*(lag\|stale)` |
| `permission-gate-friction` | `(auto.*classifier\|Self-Modification\s+gate)` |
| `skill-flow-friction` | `(attach-screenshots\|skill\s+flow).*(friction\|surprise)` |
| `queue-staleness` | `queue.*(staleness\|stale-queue\|starved)` |
| `uncategorized` | anything not matching the above |

For each cluster, compute:
- `signature`
- `count` (number of entries)
- `roles` (set of role names involved)
- `first_seen` / `last_seen` (timestamps)
- 2-3 representative example entries (role, timestamp, one-line headline)

Singletons (count == 1) still surface but never get a fix proposal — too thin a signal.

### 4. Cross-reference the fix-log

Load `~/.fleet/feedback/.fix-log.jsonl` (or treat as empty if absent). Each line is a JSON object — schema below.

For every cluster:

- **Look up open fixes** (`status ∈ {proposed, applied, recurring}`) whose `signature` matches.
- **If a matching `applied` fix has entries in this cluster newer than its `applied_at`:** the fix didn't close the loop. Update the row in place: `status: recurring`, bump `recurrence_count`, set `last_recurrence_at` to the cluster's `last_seen`. Surface as **RECURRING**.
- **If a matching `proposed` fix has entries newer than its `proposed_at`:** the proposal is still active but the pattern is still happening — surface as **STILL PROPOSED — N new occurrences** to nudge the human.
- **If a matching `applied` fix has `last_recurrence_at` null or before `applied_at` AND `(now - applied_at) ≥ 7 days`:** auto-promote to `closed`. Set `verified_closed_at` to now. Surface in **Closed this run**. The check uses the stored `last_recurrence_at` rather than "no entries in this cluster this run" because a previous run's bumped marker can hide a recurrence outside the current window — `last_recurrence_at` is the cross-run invariant.
- **If no matching open fix exists** for the cluster: it's a fresh cluster — falls through to step 5.

### 5. Draft fix proposals for fresh clusters

For each fresh cluster with `count ≥ 2`, draft a one-paragraph proposal that names the **concrete artifact to change**. Examples:

> **Cluster `label-absent-after-verdict`** (6 occurrences, 2 reviewers, 2026-05-14 → 2026-05-19)
> Proposed fix: harden the verdict-label step in `.claude/skills/review-pr/SKILL.md` — wrap `gh pr edit --add-label` in a retry-and-verify guard that exits non-zero if the label is still missing after one retry. Cross-reference [.claude/commands/role-sonnet-reviewer.md](.claude/commands/role-sonnet-reviewer.md) step 5b.

> **Cluster `stale-task-row`** (12 occurrences, T-166/T-217/T-279/T-300, 2026-05-14 → 2026-05-21)
> Proposed fix: add a maintenance-sync step to `.claude/commands/role-queue-manager.md` that re-derives `TASKS.md` from issue bodies on every queue-manager wake. Today the issue body says "done" but the row stays `free`, so workers keep claiming no-ops.

A "concrete artifact" means a specific file path or named script. If you can't name one, the proposal is too vague — say so in the proposal text and leave the cluster as **needs-investigation** instead of generating a low-quality proposal.

### 6. Present the digest

Print three sections in this order (most urgent first):

```
RECURRING (previous fixes didn't close the loop):
  ! fix-003 (worktree-tracker-drift) — 2 new occurrences since applied 2026-05-10
    Last recurrence: 2026-05-20 18:30 (opus-worker-2)
    Original proposal: <one-line summary>

STILL PROPOSED (not yet applied, still recurring):
  · fix-005 (state-cache-lag) — 1 new occurrence since proposed 2026-05-15

NEW PROPOSALS:
  1. [6x] label-absent-after-verdict
     <proposal text>
  2. [12x] stale-task-row
     <proposal text>

SINGLETONS (no proposal — surfaced for awareness):
  - 2026-05-20 02:14 opus-worker-2: attach-screenshots skill flow has two friction points
  - 2026-05-20 02:21 opus-reviewer: Projection-cache 6-second lag…
  - …

CLOSED THIS RUN (no recurrence in ≥7d):
  ✓ fix-002 (state-cache-lag) — closed after 11d clean

Open fix-log: 4 proposed, 2 applied, 1 recurring, 8 closed (lifetime).
```

### 7. Reconcile and log

Ask the human two questions in sequence (use `AskUserQuestion`, one question at a time so the user can think):

**Q1: New proposals.** For each new proposal numbered above, ask: "Log proposal #N (`<signature>`) to the fix-log?" Options: `log` / `skip` / `tweak text`. For each `log`, append a new line to `.fix-log.jsonl` with `status: proposed`, fields per the schema below.

**Q2: Previously proposed fixes.** For each existing row with `status: proposed`, ask: "Fix `<id>` (`<signature>`) was proposed on `<date>`. Status now?" Options: `applied` / `still-proposed` / `drop`.
- `applied` → also ask for an `applied_ref` (PR URL, commit SHA, or file path). Flip `status: applied`, set `applied_at` = now, store `applied_ref`.
- `still-proposed` → no change.
- `drop` → ask for a one-line `notes` reason. Flip `status: dropped`, store the reason.

Don't ask about recurring fixes here — those need design work, not a status flip. Tell the user "the recurring fixes above need new proposals; treat them as fresh clusters next run, or open a follow-up task now."

### 8. Bump the marker

```bash
date -u +"%Y-%m-%dT%H:%M:%S" > ~/.fleet/feedback/.last-reviewed
```

Always bump at the end, even if the user dismissed everything without acting. The contract is "I've seen these," not "I've fixed these."

If the skill errors out before reaching this step, **do not** bump the marker — leave it stale so the next run re-surfaces the entries.

### 9. End-of-run summary

Print a one-paragraph summary:

```
Reviewed N entries since YYYY-MM-DD HH:MM (X clusters, Y singletons).
M new proposals logged, K previously proposed fixes resolved, L auto-closed.
Marker bumped to YYYY-MM-DDTHH:MM:SS UTC.
Open fix-log: <proposed-count> proposed, <applied-count> applied, <recurring-count> recurring.
```

## Fix-log schema (`~/.fleet/feedback/.fix-log.jsonl`)

One JSON object per line. New rows append; updates rewrite the file (fine — log stays small).

```json
{
  "id": "fix-007",
  "signature": "label-absent-after-verdict",
  "proposed_at": "2026-05-21T18:42:00",
  "first_seen": "2026-05-14T16:49:00",
  "occurrences_at_proposal": 6,
  "proposal": "Harden verdict-label step in review-pr/SKILL.md — wrap gh pr edit in retry-and-verify guard…",
  "status": "proposed",
  "applied_at": null,
  "applied_ref": null,
  "verified_closed_at": null,
  "last_recurrence_at": null,
  "recurrence_count": 0,
  "notes": null
}
```

**ID assignment:** `fix-NNN`, monotonically increasing. Read the highest existing ID, add 1. Pad to 3 digits.

**Status transitions:**

| From | To | Trigger |
|---|---|---|
| `proposed` | `applied` | Human confirms during step 7 |
| `proposed` | `dropped` | Human drops during step 7 |
| `applied` | `recurring` | Step 4 sees fresh entries after `applied_at` |
| `applied` | `closed` | Step 4 sees `last_recurrence_at` null-or-before-`applied_at` and `(now - applied_at) ≥ 7d` |
| `recurring` | `applied` | Human re-applies a tweaked fix (manual JSON edit, or surface a fresh proposal next run that supersedes) |
| `recurring` | `dropped` | Give up |

**Manual edits:** the human can hand-edit `.fix-log.jsonl` between runs. The skill must tolerate missing fields gracefully (treat as `null`) and re-emit a valid line on rewrite.

## Anti-patterns

- **Don't bump `.last-reviewed` before the digest is complete.** If the skill crashes mid-run, the entries must still be there next time.
- **Don't auto-close a fix on the same run it was applied.** The 7-day no-recurrence window is the whole verification mechanism — collapsing it defeats the closed loop.
- **Don't propose a duplicate fix for a signature that already has an open log entry.** That cluster should be surfaced as RECURRING or STILL PROPOSED, never re-proposed.
- **Don't propose fixes for singletons.** One occurrence is noise. Two is a pattern.
- **Don't propose vague fixes.** Every proposal must name a concrete file path or script. Vague ones go to **needs-investigation** instead.
- **Don't touch the role feedback files** (`<role>.md`). They are append-only from the agents' side; rewriting them would corrupt the agents' future appends and lose history.
- **Don't apply fixes inline.** This skill is propose-only. The human (or a follow-up skill) applies the fix in a separate edit, then confirms in the next run.
- **Don't scrape PR comments in v1.** That's a separate feedback channel with different semantics. Adding it here would double the surface area for marginal gain.

## Recovery

- **Corrupt `.fix-log.jsonl`** (a line fails to parse): print the offending line, skip it, continue with the rest. Tell the user at the end so they can fix it manually.
- **`fleet-feedback` CLI missing**: fall back to reading `~/.fleet/feedback/*.md` directly with the same regex used in [scripts/fleet/fleet-feedback](../../../scripts/fleet/fleet-feedback): `^## (\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}(?::\d{2})?)`.
- **Marker file unwritable**: error out before the digest. Don't half-bump.
