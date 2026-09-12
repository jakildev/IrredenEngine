---
name: review-fleet-feedback
description: >-
  Reads the agent-to-human feedback files (~/.fleet/feedback/<role>.md)
  newer than the .last-reviewed marker, clusters recurring snags, drafts
  concrete fix proposals naming the role doc, script, or setting to
  change, and runs the closed-loop check that previously applied fixes
  stopped recurring before closing them. Use when the user says "review
  fleet feedback", "check fleet snags", "summarize fleet feedback", "what
  has the fleet been complaining about", or "any new fleet snags", or as
  the feedback leg of an architect-run triage sweep
  (docs/agents/triage-protocol.md "Extended sweep"); never proactively.
---

# review-fleet-feedback

The deterministic work lives in
[`scripts/fleet/review-fleet-feedback`](../../../scripts/fleet/review-fleet-feedback);
this skill runs it, reads its JSON, and separates two kinds of decisions:

- **Mechanical transitions** (`pending_mutations` in the digest —
  merged-PR flips, 7-day auto-closures, recurrence flags): applied
  without asking; the script detected them from GitHub state.
- **Human-judgment decisions** (which new proposals to log, whether to
  drop a proposed fix): asked.

Both go back through the script's `apply`. Signature parsing lives in the
script's `SIGNATURES` table, never in this skill body.

State, all under `~/.fleet/feedback/` and never committed: `<role>.md`
(source entries, read-only here), `.last-reviewed` (ISO watermark),
`.fix-log.jsonl` (one JSON object per line; tolerates manual edits). If
the directory is missing or empty, report "no fleet feedback yet" and exit.

## Flow

1. **Digest.** `scripts/fleet/review-fleet-feedback digest >
   /tmp/fleet-digest.json`. Fields: `clusters` (count ≥ 2 by signature,
   with `examples` and `all`), `singletons` (never proposable),
   `recurring` (applied fixes with entries after `applied_at`),
   `still_proposed`, `closed_this_run` (applied fixes quiet ≥ 7 days),
   `fresh_proposable` (clusters with no open fix-log row),
   `pending_mutations`, `fix_log_counts`. `total_entries == 0` → skip to
   step 5 (still print the open fix-log and bump — the contract is
   "seen", not "fixed").
2. **Proposals.** One paragraph per `fresh_proposable` cluster naming the
   concrete artifact to change (a file path or script) and any cross-refs.
   A proposal that cannot name an artifact says so and tags the cluster
   `needs-investigation`. One occurrence is noise; two is a pattern.
3. **Present**, most urgent first:
   ```
   RECURRING (previous fixes didn't close the loop):
     ! fix-NNN (<signature>) — <N> occurrences since applied <date>

   STILL PROPOSED (not yet applied, still recurring):
     · fix-NNN (<signature>) — <N> new occurrences since proposed <date>

   NEW PROPOSALS:
     1. [Nx] <signature>
        <proposal text>

   CLOSED THIS RUN (no recurrence in ≥7d):
     ✓ fix-NNN (<signature>) — closed after <N>d clean

   Open fix-log: <P> proposed, <A> applied, <R> recurring, <C> closed.
   ```
   List 3–5 interesting singletons in a collapsed block at most.
4. **Reconcile.** Mechanical transitions go into the decisions JSON
   unconditionally. Ask only for: ≤ 4 new proposals → one
   `AskUserQuestion` with each as a multi-select option; > 4 → one bulk
   question (log all / top N / none / let me pick); manual drops and
   manual applied-confirms only when the human raises them or a
   `still_proposed` row is ≥ 30 days old (batch `still-proposed / drop`
   when > 3). `recurring` fixes need a fresh proposal, not a status flip —
   say so.
5. **Apply and bump.** Write the decisions JSON (`apply --schema` prints
   the schema, including the `pending_mutations`), then:
   ```bash
   scripts/fleet/review-fleet-feedback apply --decisions /tmp/fleet-decisions.json
   scripts/fleet/review-fleet-feedback bump
   ```
   If anything errored before this step, do not bump — a stale marker
   re-surfaces the entries next run.
6. **(Optional) file tasks.** To hand proposals to the `human:approved` →
   `fleet-queue-ingest` → worker pipeline:
   ```bash
   scripts/fleet/review-fleet-feedback file-tasks --dry-run
   scripts/fleet/review-fleet-feedback file-tasks [--ids fix-001,fix-003]
   ```
   Each proposed row without a `tracking_issue` becomes a `human:approved`
   issue quoting the proposal and three representative entries; the URL
   is written back to the row, and a merged `Closes #N` PR flips it to
   `applied` on the next digest. Confirm with the human first — these are
   public issues; default to `--dry-run`.
7. **Summary.** `Reviewed N entries since <date> (C clusters, S
   singletons). M new proposals logged, K previously proposed resolved, L
   auto-closed. Marker bumped to <ts> UTC. Open fix-log: <counts>.`

## Status transitions

| From | To | Trigger | Ask? |
|---|---|---|---|
| `proposed` | `applied` | merged PR on the tracking issue (`pending_mutations`) | no |
| `proposed` | `applied` | human confirms a fix merged outside the issue | yes |
| `proposed` | `dropped` | human drops in step 4 (requires `notes`) | yes |
| `applied` | `recurring` | fresh entries after `applied_at` (`pending_mutations`) | no |
| `applied` | `closed` | no recurrence for ≥ 7 days (`pending_mutations`) | no |
| `recurring` | `applied` / `dropped` | human re-applies a tweaked fix / gives up | yes |

Never auto-close on the same run a fix was applied; never re-propose a
cluster the script already reports as `recurring` or `still_proposed`. A
corrupt fix-log line is skipped with a `WARN` on stderr — surface it. A
marker older than 90 days caps `--since` at 90d and sets `marker_note` —
print it in the digest header.
