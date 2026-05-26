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

A closed-loop reviewer for `~/.fleet/feedback/`. Each role appends
timestamped notes when it sees something the human should know about;
this skill turns that one-way channel into a track of proposed → applied
→ closed fixes.

**Deterministic work lives in
[`scripts/fleet/review-fleet-feedback`](../../../scripts/fleet/review-fleet-feedback).**
This skill body picks the script, reads its JSON output, and separates
two kinds of decisions:

- **Mechanical transitions** (`pending_mutations` in the digest —
  merged-PR flips, auto-closures, recurrence flags): apply these
  silently without asking. They are deterministic; the script detected
  them from GitHub state.
- **Human-judgment decisions** (which NEW proposals to log, whether to
  drop a proposed fix): ask the human only for these.

The agent writes both kinds back through the script's `apply`
subcommand. If you find yourself parsing
feedback entries with regex in the skill body, stop — extend the script's
`SIGNATURES` table instead.

## State files

| Path | Purpose |
|---|---|
| `~/.fleet/feedback/<role>.md` | Source entries (one file per role). Read-only here — agents append. |
| `~/.fleet/feedback/.last-reviewed` | ISO timestamp watermark. Bumped on completion. |
| `~/.fleet/feedback/.fix-log.jsonl` | Tracker. One JSON object per line. Tolerates manual edits. |

Both state files live in `~/.fleet/feedback/` (not in the repo) and are
managed entirely by this skill via the script. They are not committed.

## Preconditions

If `~/.fleet/feedback/` is missing or empty, report "no fleet feedback yet"
and exit.

## Flow

### 1. Pull the digest

```bash
scripts/fleet/review-fleet-feedback digest > /tmp/fleet-digest.json
```

The script reads `.last-reviewed`, computes `--since`, clusters every
entry by signature, cross-references the fix-log, and emits a single
JSON document. Key fields:

- `clusters` — count ≥ 2, grouped by signature. Each carries `examples`
  (top 3) and `all` entries.
- `singletons` — count 1 or unmatched. Surfaced for awareness; never
  proposable.
- `recurring` — applied fixes that have new entries after `applied_at`.
- `still_proposed` — proposed fixes with new entries after `proposed_at`.
- `closed_this_run` — applied fixes quiet for ≥ 7 days, auto-promoted.
- `fresh_proposable` — clusters ≥ 2 with no open fix-log row.
- `pending_mutations` — flips the agent will pass back to `apply` verbatim.
- `fix_log_counts` — running totals by status.

If `total_entries == 0`, skip to step 5 (still print the open fix-log and
bump the marker — the contract is "seen," not "fixed").

### 2. Draft proposals for `fresh_proposable` clusters

Read the JSON. For each cluster in `fresh_proposable`, write a
one-paragraph proposal that **names the concrete artifact to change**
(a file path or named script). Examples:

> **Cluster `label-absent-after-verdict`** (37 occurrences, 5 roles, 2026-04-30 → 2026-05-20)
> Replace the combined `gh pr edit --remove-label A --add-label B` in
> `.claude/skills/review-pr/SKILL.md` with two separate calls and wrap
> the add in a retry-and-verify guard (re-query labels, fail loud if the
> verdict label is still missing after one retry). Cross-ref
> `role-sonnet-reviewer.md` step 5b and `role-opus-reviewer.md`.

If you can't name a file path or script, the proposal is too vague — say
so in the proposal text and tag the cluster `needs-investigation` instead.
**One occurrence is noise; two is a pattern. Do not propose for singletons.**

### 3. Present the digest

Print three sections in this order (most urgent first):

```
RECURRING (previous fixes didn't close the loop):
  ! fix-NNN (<signature>) — <N> occurrences since applied <date>

STILL PROPOSED (not yet applied, still recurring):
  · fix-NNN (<signature>) — <N> new occurrences since proposed <date>

NEW PROPOSALS:
  1. [Nx] <signature>
     <proposal text>
  …

CLOSED THIS RUN (no recurrence in ≥7d):
  ✓ fix-NNN (<signature>) — closed after <N>d clean

Open fix-log: <P> proposed, <A> applied, <R> recurring, <C> closed.
```

If there are also singletons worth surfacing (a one-off but interesting
gap), list 3-5 of them in a collapsed block; don't dump all of them.

### 4. Reconcile

**Mechanical transitions — never ask.** `pending_mutations` from the
digest (merged-PR → applied, 7-day-quiet → closed, post-applied-entry
→ recurring) are deterministic. Include them in the decisions JSON
unconditionally. Do not present them as options, do not ask for
confirmation, do not gate them behind a question. Just apply them and
report what happened in the summary.

**Human-judgment decisions — ask only for these:**

- **≤ 4 new proposals**: ask one `AskUserQuestion` with each proposal as
  an option (multiSelect = true).
- **> 4 new proposals**: ask one bulk question — "log all / log top N /
  log none / let me pick" — then drill in only if they pick the last.
- **Manual drops** (human wants to stop tracking a proposed fix): ask
  only when the human explicitly requests it, or when presenting
  `still_proposed` rows that have been open for an extended period
  (≥30d). Batch with options `still-proposed / drop` when count > 3.

Don't ask about `recurring` fixes here — they need a fresh proposal, not
a status flip. Tell the human: "the recurring fixes need new design;
treat them as fresh clusters next run, or open a follow-up task now."

### 5. Apply and bump

Write decisions to a JSON file (`apply --schema` prints the schema), then:

```bash
scripts/fleet/review-fleet-feedback apply --decisions /tmp/fleet-decisions.json
scripts/fleet/review-fleet-feedback bump
```

The script handles ID assignment (`fix-NNN`, monotonic), status flips,
and atomic file rewrites. **Always include `pending_mutations` from the
digest in the decisions JSON** — these are the mechanical transitions
from step 4 that were applied without asking. Combine them with any
human-chosen new-proposal or drop decisions in one JSON file.

If anything errored before this step, **do not bump the marker** —
leave it stale so the next run re-surfaces the entries.

### 5b. (Optional) File the proposals into the fleet queue

If the human wants the proposals worked on rather than just tracked,
hand them to the existing `human:approved` → `fleet-queue-ingest` →
queue-manager → worker pipeline:

```bash
scripts/fleet/review-fleet-feedback file-tasks --dry-run     # preview titles
scripts/fleet/review-fleet-feedback file-tasks                # file all proposed rows
scripts/fleet/review-fleet-feedback file-tasks --ids fix-001,fix-003   # subset
```

For each proposed-status row without a `tracking_issue`, `file-tasks`
creates a GitHub issue with `human:approved`, a title derived from the
signature, and a body that quotes the proposal + 3 representative
feedback entries. The issue URL gets written back to the row's
`tracking_issue` field. Fleet workers eat the issues via the normal
queue; merged `Closes #N` PRs auto-flip the fix-log row to `applied`
on the next `digest` run.

Confirm with the human before filing — these are public issues, and
filing all 9 at once visibly floods the queue. Default to `--dry-run`
first.

### 6. End-of-run summary

One paragraph:

```
Reviewed N entries since <date> (C clusters, S singletons).
M new proposals logged, K previously proposed resolved, L auto-closed.
Marker bumped to <ts> UTC.
Open fix-log: <counts>.
```

## Fix-log schema

The script owns the schema; run `apply --schema` for the canonical form.
Manual edits to `.fix-log.jsonl` are tolerated — the script `setdefault`s
missing fields and re-emits a valid line on rewrite.

Status transitions (driven by step 5 + the script):

| From | To | Trigger | Ask human? |
|---|---|---|---|
| `proposed` | `applied` | Digest detects merged PR on the tracking issue (`pending_mutations`). | **No** — mechanical. |
| `proposed` | `applied` | Human manually confirms (no tracking issue, or PR merged outside the issue). | Yes. |
| `proposed` | `dropped` | Human drops during step 4. | Yes (requires `notes`). |
| `applied`  | `recurring` | Digest sees fresh entries after `applied_at` (`pending_mutations`). | **No** — mechanical. |
| `applied`  | `closed` | Digest sees no recurrence for ≥ 7d (`pending_mutations`). | **No** — mechanical. |
| `recurring`| `applied` | Human re-applies a tweaked fix (manual edit or new proposal supersedes). | Yes. |
| `recurring`| `dropped` | Give up. | Yes. |

## Anti-patterns

- **Don't bump the marker before the digest is complete.** If anything
  errors mid-run, entries must still be there next time.
- **Don't auto-close on the same run a fix was applied.** The 7-day quiet
  window is the whole verification mechanism.
- **Don't propose duplicates of an open fix-log row.** The script surfaces
  the cluster as `recurring` or `still_proposed`; re-proposing it
  bypasses the closed-loop.
- **Don't extend `SIGNATURES` in the skill body.** It lives in the script
  so it's diff-able and (when needed) testable. The skill body refers to
  it by name.

## Recovery

- **Corrupt fix-log line**: the script logs `WARN: …; skipping` to stderr
  and continues. Surface the warning to the human at the end.
- **Marker `>90d` stale**: the script caps `--since` at 90d and sets
  `marker_note`. Print the note in the digest header.
