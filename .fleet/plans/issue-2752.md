## Plan: fleet/scout: state.json is past the Read tool's 256KB cap (replan — Phase 3 committed)

- **Issue:** #2752
- **Model:** opus
- **Date:** 2026-08-08
- **Revision:** v2 — addresses the 2026-08-01 design-soundness bounce: Phase 3
  is committed to a single arm (the review's arm (b)), the projected shortfall
  is closed with one further committed reduction (not a menu), AC #3's fixture
  is pinned label-absent, and arms (a)/(c) are dropped. Phases 1, 2, and the
  guard phase carry over verbatim from v1 (renumbered; mapping below). All
  numbers below re-measured this session (2026-08-08, macOS pool-1,
  post-#2743).

### Verified current state (re-measured 2026-08-08)

- On-disk `~/.fleet/state/state.json` = **476.8 KB** (`os.path.getsize` =
  488,274; cap = 262,144). Grew from 423 KB at filing — #2743 (merged via
  PR #2915) lifted the `prs[]` window as v1's reconciliation predicted, so the
  sequencing constraint from v1 is **resolved**: this plan measures against
  the real window (27 engine + 19 game open PRs).
- `reviews` = **218.6 KB (~46 % of the file)**: 101 non-empty bodies
  (63 engine + 38 game), `med=max=2063 B` — still 62-of-63-at-the-cap shaped;
  the per-body cap works, the aggregate is unbounded (unchanged root cause,
  `_truncate_review_body`, `fleet-state-scout:180-192`).
- **New measurement — 89.4 KB of the on-disk file is pretty-print
  whitespace.** The same state serialized compact
  (`separators=(",", ":")`) is 387.4 KB. The emitter is
  `fleet-state-scout:3060`: `json.dumps(state, indent=2, sort_keys=True)`.
  Review bodies are single JSON strings, so body-trimming and
  whitespace-removal are **orthogonal, stacking** savings.
- Latest-review-only body retention keeps 81.7 KB and drops **117.9 KB**
  (measured by re-projecting the live cache).
- **Arm (b) feasibility measured:** 8 live review bodies carry the
  `Opus recheck required` phrase; the distance from the phrase-line start to
  end-of-body is `[146, 150, 165, 181, 234, 261, 437, 539]` — max **539 B**,
  so a 640 B tail preserves every live instance with headroom.
- Consumer contract (re-verified at current line numbers): sonnet-reviewer
  reads `reviews[].author` only (`role-sonnet-reviewer.md:72`); opus-reviewer
  tests the **latest** review's `body` for the fixed phrase
  (`role-opus-reviewer.md:101`), as one disjunct alongside the
  `fleet:needs-opus-recheck` label (`:35`) — the phrase path must keep firing
  independently of the label (`fleet-state-scout` documents the PR #1473
  regression this guards). Both role docs are gated; this plan changes
  neither the schema nor either predicate, so the whole fix stays
  worker-applicable.

### Ruled out / out of scope (carried from v1 + bounce, do not re-derive)

- `closed_fleet_queued` (48 KB) — load-bearing (queue-manager projection
  blocker-clearing, `tests/test_queue_manager_projection.py:273`; epic-steward
  reads it). Do not drop.
- `tasks.done` (41.4 KB) — internal derived mirror with **two live readers**
  (trigger-hash feed `fleet-state-scout:2073`; role-projection slice `:2552`).
  v1's arm (c) re-admitted it without resolving either reader — per the
  bounce, it stays **out of scope**; the projection below clears the cap
  without touching it. If it is ever dropped, that is its own issue resolving
  both readers explicitly.
- v1's arm (a) (drop `body` from non-candidate PRs) — dropped per the bounce:
  as specified it saved zero bytes, and widened it kills the label-independent
  phrase path.

### Committed approach — four reductions + a guard, no menu

All four reductions are committed; none is implementer's-choice. Projection
arithmetic (measured basis above): 476.8 − 117.9 (P1) − 35.8 (P2) − 15.4 (P3)
− 89.4 (P4) ≈ **218 KB**, under the 256 KB cap with ~38 KB headroom — and
growth is now bounded per-PR (~200 B/review metadata + one ≤ 800 B body), so a
45-PR window projects ~+8 KB over today, not +48.

**Phase 1 — body only where a consumer reads it** *(v1 Phase 1, verbatim).*
In the PR fetch (`_fetch_prs_graphql`), retain `body` on the **latest** review
per PR (by `submittedAt`); emit `body: ""` for older ones; keep
`author`/`state`/`submittedAt` on every entry. Schema unchanged. Saves
117.9 KB measured.

**Phase 2 — trim the unread head** *(v1 Phase 2, verbatim).* No code or role
doc reads the 1024 B head; the phrase is a trailing line, covered by the tail.
`REVIEW_BODY_HEAD` 1024 → **128**. Saves ~35.8 KB (≈40 kept bodies × 896 B).

**Phase 3 — committed to the bounce's arm (b): minimal tail.**
`REVIEW_BODY_TAIL` 1024 → **640**, chosen from the measured live maximum
phrase-tail distance of 539 B (+19 % headroom). Saves ~15.4 KB. At
implementation time, re-measure the phrase-tail distances against the
then-live corpus (one python pass over the cache, as above); if any live
instance exceeds 640, raise the constant to cover it and say so in the PR
body — measured, never guessed.

**Phase 4 (new) — compact emit for `state.json` only.** At
`fleet-state-scout:3060`, emit with `separators=(",", ":")` (keep
`sort_keys=True` — it is what makes ticks diffable). Saves **89.4 KB**
measured, fully orthogonal to Phases 1–3. Consumer audit (done this session):
every reader parses JSON — `read_json_retry` sites in the scout, the
dispatcher's stdlib `generated_at` parse (`fleet-dispatcher:2100-2116`),
`fleet-epic-status` (python), `fleet-up` (existence check only,
`fleet-up:1327-1338`), the leader-bundle protocol (verbatim text + parse).
No line-oriented (grep/sed) consumer exists. Scope guard: the per-PR/issue
detail caches (`:1064`, `:1103`) and the role projections (`:3178`) **keep**
`indent=2` — they are small and human-read; only the oversized aggregate goes
compact. Debug ergonomics: `python3 -m json.tool` / `jq .` reflate on demand.

**Phase 5 — regression guard + doc correction** *(v1 Phase 4, verbatim).*
Scout logs loudly when the emitted on-disk size exceeds ~200 KB; correct
`docs/agents/FLEET-CACHE.md:13` to state the **invariant** ("must stay under
the 256 KB Read cap") rather than a point-in-time size.

Do not ship on a projected number: re-measure `os.path.getsize` on the live
host after the change (46 open PRs across repos today), alongside acceptance
test 2's deterministic synthetic bound.

### Affected files

- `scripts/fleet/fleet-state-scout` — Phases 1–5 (fetch projection, the two
  constants, the emit call, the size guard)
- `docs/agents/FLEET-CACHE.md` — Phase 5 invariant wording
- `scripts/fleet/tests/test_state_projection_size.py` — new (acceptance below)

### Acceptance criteria

1. **Both predicates still fire** (new test): fixture PR whose **latest**
   review body contains `Opus recheck required` and an older review that does
   not — **fixture carries NO `fleet:needs-opus-recheck` label** (pinned per
   the bounce: the label-absent input is the one that can regress) — the
   opus-reviewer predicate (latest-body phrase test) must select it after the
   projection. A PR with zero reviews must still satisfy sonnet-reviewer's
   `reviews[].author`-empty predicate. **Positive control:** flip the
   fixture's phrase to absent and assert the predicate stops firing.
2. **Size bound (the test that fails today):** synthesize 45 PRs × 3 reviews ×
   oversized bodies, run the full projection + emit path, assert the emitted
   **on-disk bytes** < 262,144 (i.e. the bound covers Phases 1–4 together,
   including the compact emit).
3. **Phrase survives truncation at the new constants:** a body shaped like the
   live maximum (phrase line starting 539 B from the end) still contains the
   phrase after `_truncate_review_body` with HEAD=128/TAIL=640.
4. Existing scout tests stay green (hermetic — mock at a fail-closed seam,
   never the live API, per `scripts/fleet/CLAUDE.md`).

### Sibling / in-flight reconciliation

- **#2743 — merged** (PR #2915); measurements above are post-merge. The v1
  sequencing constraint is discharged.
- Open scout-touching PRs #2964 (per-kind trigger suppression) and #2967
  (degraded-skip projection edge) touch the trigger/projection machinery, not
  `_truncate_review_body` / `_fetch_prs_graphql` / the `STATE_FILE` emit —
  disjoint hunks in the same file; merge-order independent (worst case a
  trivial textual rebase).
- No open PR or sibling issue touches the review-retention path.

### Gotchas

- The phrase path is **independent of the label by design**
  (`fleet-state-scout`'s own comment documents the PR #1473 regression);
  nothing in Phases 1–4 may condition body retention on the label.
- `sort_keys=True` stays in the Phase 4 emit — dropping it would make every
  tick a full-file diff for the leader-bundle change detection.
- The scout is the *writer*; roles read the file through parse-based
  extraction. No role-doc edit is needed anywhere in this plan (all consumers
  keep working unchanged), which is what keeps the fix inside the ungated
  worker surface.
- One task, one PR. Phases are one commit each or grouped; test file lands
  with them.

