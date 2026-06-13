# Plan: blocked_by parser coverage holes — scout/ingest/claim disagree

- **Issue:** #1749
- **Model:** opus
- **Date:** 2026-06-13

> Engine-repo artifact: described in pure fleet-tooling terms. The live repro
> lives in a downstream-creation epic; per the cross-repo info-isolation rule
> this plan uses generic placeholder issue numbers (`#E`, `#A`…`#F`) for the
> example bodies. The test fixtures use the same synthetic bodies — no live
> downstream issues are fetched.

## Premise correction (verified against the actual code path)

The issue body guesses the recurring epic-child gap is an epic-header
**"Children (D needs A+B+C)" letter-chain** prose format. That is a **red
herring**. I fetched the real epic-phase child bodies and ran the three live
parsers against them. Each **child self-declares** its blockers — but in
**plain text**, e.g. a phase-D child whose body opens with:

```
Part of epic #E (Phase D). [opus] Blocked by: #A, #B, #C.
```

and a phase-E child: `Part of epic #E (Phase E). [sonnet] Blocked by: #D.`

This is **not** the canonical bold standalone `**Blocked by:** #N` line, **not**
the inline-bold `**Blocked by: #N**` span, **not** a `Blocked on` header.
Running the exact current regexes from all three sources against that body
returns **empty** from every one — which is why the live scout projection
marks such a child `blocked:false, blocked_by:"(none)"` and it surfaces as
pickable before its predecessors land.

So the verified root cause is: **plain (non-bold, possibly mid-line)
`Blocked by: #N[, #M]` declarations are skipped by all three bold-only
parsers.** Children carry their own deps in degraded form, so fixing the
plain-form miss fixes the case end-to-end — no letter→issue propagation is
needed (rejected as redundant + brittle to maintain in three places).

Independently, the three parsers also **disagree on the no-blocker sentinel**:
the scout has the rich `_is_no_blocker_value` (handles bare `none`, `n/a`,
`tbd`, a lone dash, `#N`-wins), while ingest and claim only test
`startswith('(none')`. A bare `none — first unblocked.` gates in claim/ingest
but not in the scout — the same "three parsers disagree" defect class the
ticket exists to close (an earlier bare-`none` case was fixed only in the
scout, leaving ingest/claim divergent).

## Scope

Reconcile the scout / queue-ingest / claim `blocked_by` readers into **one
shared parse routine** so they never disagree, **widen it to the plain-text
`Blocked by: #N` form** (the verified mechanism), and lock the contract with a
fixture corpus replaying every observed format. One opus task, one PR —
splitting it would reintroduce the disagreement window the ticket exists to
close.

## Approach (one PR, four parts in order)

### Part 1 — shared module `scripts/fleet/fleet_blocked_by.py`

Create a new importable module, mirroring the existing
`scripts/fleet/fleet_branch_match.py` / `fleet_scope_shipped.py` shared-lib
pattern (scout does `sys.path.insert(0, dirname(__file__)); from
fleet_branch_match import …`; fleet-claim imports via `FLEET_LIB_DIR`; ingest
via `INGEST_SELF` realpath). Single source of truth for:

- `is_no_blocker_value(value) -> bool` — port the scout's current rich version
  verbatim (a concrete `#\d+` anywhere wins → not a sentinel; else first token
  in `{none, n/a, na, tbd}` or a leading dash/`(` → sentinel).
- `parse_blocked_by(body) -> str` — union of every blocker declaration,
  returning the canonical comma-joined value (or `""` when genuinely
  unblocked). Forms recognized, **in this precedence**:
  1. canonical standalone `**Blocked by:** #N` line (multi-line, multi-ref)
  2. inline-bold `**Blocked by: #N (label)**` span (#1423)
  3. **NEW** plain `Blocked by:` (bold optional, anywhere on a line) directly
     followed by a `#N[, #M …]` ref-list — capture **only** the `#N` ref
     tokens, never trailing prose, so it cannot over-capture a sentence.
     Regex: `(?<!\*)Blocked by:\s*(#\d+[^\n]*)` — note `#\d+` (not `#\d`);
     `[^\n]*` absorbs the rest of the line but `#\d+` makes explicit the
     capture starts at a full issue number, not just `#` + one digit.
  4. `Blocked on …` header prose fallback (#1326) — only when 1–3 found nothing.
- `blocker_refs(body, default_repo) -> [(slug, num)]` — qualified-ref
  extraction (lift ingest's `REF_RE` + `_REF_NAME_TO_SLUG` + `_qual_refs`
  cross-repo routing, #1522, verbatim) applied to the parsed value.

Design notes the implementer must honor:
- The plain pattern (3) also matches the bold forms (1,2) since `**` is
  optional — fine for the **gating** decision (it only cares about `#N` refs).
  Keep a *separate* canonical-line presence check ONLY where the
  sentinel-vs-absent distinction matters (ingest's "Blocked by field missing"
  WARN at fleet-queue-ingest:443). i.e. unify the ref-extraction, preserve the
  "was a Blocked-by field present at all?" signal.
- False-positive safety: the plain pattern requires `#\d+` *immediately* after
  `Blocked by:` — verified that `"not blocked by anything yet"` yields no match.

### Part 2 — refactor the three consumers to import the module

- **scout** (`fleet-state-scout` `_parse_blocked_by` ~L368): replace the body
  with a `from fleet_blocked_by import parse_blocked_by` delegation. Move
  `_BLOCKED_ON_RE` into the module if it stops being referenced locally.
  `resolve_blocked_by` / `find_stackable_blockers` keep calling
  `_parse_blocked_by` (now a thin shim) so their downstream logic is untouched.
- **ingest** (`fleet-queue-ingest` `_blocker_refs`/`_has_header_blocker`/
  `BLOCKED_*` ~L210-279): delegate `_blocker_refs` to
  `blocker_refs(body, default_repo)`; keep the `BLOCKED_RE.search` presence
  check at L443 (the "field missing" WARN) but source the regex from the
  module so it can't drift.
- **claim** (`fleet-claim` check_blockers heredoc at L500 + the stackable
  heredoc at L974): add `sys.path.insert(0, os.environ["FLEET_LIB_DIR"]); from
  fleet_blocked_by import parse_blocked_by, blocker_refs, is_no_blocker_value`
  and replace the inline `field_re`/`inline_re`/`blocked_on_re` block. Add the
  lib-presence guard next to the existing `fleet_branch_match.py` check at
  fleet-claim:117 so a missing module fails loud, not a silent exit-0.

### Part 3 — fleet-claim backstop (issue's explicit ask) — falls out of Part 2

The issue asks claim to "backstop a scout miss rather than trusting the cached
projection." `check_blockers` already reads the **issue body directly** (never
the scout cache) — so once the shared parser catches the plain form, claiming
the phase-D child parses its `Blocked by: #A, #B, #C` and gates correctly with
**no extra work and no epic fetch**. Because the verified mechanism is child
self-declaration, the heavier "fetch the parent epic and parse its
letter-chain in the claim path" design is unnecessary — explicitly rejected.

### Part 4 — fixtures + manual replay

- `scripts/fleet/tests/test_blocked_by.py` — unit tests for the module, one
  case per format (synthetic bodies): canonical standalone; inline-bold
  `**Blocked by: #N (x)**`; **plain `Blocked by: #A, #B, #C`** (the
  epic-phase-child replay); multi-line + multi-ref union; `Blocked on #N`
  header; sentinels (`(none)`, bare `none`, `n/a`, `tbd`, lone dash);
  cross-repo `[owner/]Repo#N` qualifier routing; false-positive guard
  (`"not blocked by anything"` → unblocked). Each asserts both
  `parse_blocked_by` and `blocker_refs`.
- **Cross-parser agreement** is structural once all three import the module;
  the corpus test documents and locks it. Additionally extend the existing
  integration tests so the plain form is exercised end-to-end through the real
  scripts: add a plain-`Blocked by:` child case to
  `test_fleet_queue_ingest_blocked_by.sh` (asserts `fleet:blocked` is stamped)
  and to `test_fleet_claim_blockers.sh` (asserts claim refuses).
- **Canonical regression**: replay #1500's form `**Blocked by:** #1499 (prose)`
  → still gated (already worked; guard against regressing it).
- **Plain-form replay**: feed the synthetic phase-D child body →
  `parse_blocked_by` returns `#A, #B, #C` and the child stays out of the
  claimable set; phase-E child → `#D`.

## Affected files

- `scripts/fleet/fleet_blocked_by.py` — **new** shared module (parse + sentinel + qualified-ref extraction).
- `scripts/fleet/fleet-state-scout` — `_parse_blocked_by` delegates to module.
- `scripts/fleet/fleet-queue-ingest` — `_blocker_refs`/`_has_header_blocker`/`BLOCKED_*` delegate; keep field-present WARN.
- `scripts/fleet/fleet-claim` — both blocker heredocs import the module; add lib-presence guard (~L117).
- `scripts/fleet/tests/test_blocked_by.py` — **new** corpus/unit + agreement test.
- `scripts/fleet/tests/test_fleet_queue_ingest_blocked_by.sh` — add plain-form case.
- `scripts/fleet/tests/test_fleet_claim_blockers.sh` — add plain-form case.

## Acceptance criteria

- A single shared parse routine (`fleet_blocked_by.py`) is the only place the
  blocked-by regexes + sentinel logic live; scout, ingest, and claim all
  import it. `grep -n 'Blocked by:' scripts/fleet/fleet-{state-scout,queue-ingest,claim}`
  shows no surviving private copy of the parse logic (comments/WARN strings ok).
- The plain `Blocked by: #N[, #M]` form (non-bold, mid-line) is gated by all
  three: replaying the synthetic phase-child bodies yields their blocker refs;
  the live scout projection would mark them `blocked:true`.
- Regression fixtures under `scripts/fleet/tests/` cover canonical, inline-bold,
  plain, multi-ref, header `Blocked on #N`, sentinels, cross-repo qualifier,
  and the false-positive guard. New + existing fleet tests pass
  (`test_blocked_by.py`, `test_fleet_queue_ingest_blocked_by.sh`,
  `test_fleet_claim_blockers.sh`, `test_issue_queue_parser.py`,
  `test_live_blocker_resolution.py`).
- The canonical (#1500-form) and plain (epic-phase-child) cases both stay out
  of the claimable set.

## Gotchas

- **Premise**: do NOT build a "Children (D needs A+B+C)" letter→issue
  propagation. Verified unnecessary — children self-declare. Building it would
  burn a round (the protocol's "verify the premise" case). Replay via the
  **child** body, not the epic header.
- **`FLEET_LIB_DIR` / symlink resolution (#1578)**: the new module must live
  beside the scripts so fleet-claim resolves it through the same
  `FLEET_LIB_DIR` the existing `fleet_branch_match.py` guard uses; add a
  matching presence guard so a missing file fails loud instead of silently
  exiting 0 (which would make every claim look unblocked — worse than the bug
  being fixed).
- **Sentinel-vs-absent**: keep ingest's "Blocked by field missing → treat as
  unblocked" WARN (L443) working — that needs "was any `**Blocked by:**`
  present?", which the unified `#N`-anchored ref pattern alone doesn't answer.
  Preserve a field-present check sourced from the module's regex.
- **Cross-repo qualifier (#1522)**: don't drop the `[owner/]Repo#N` routing
  when lifting ref extraction into the module — ingest and claim both rely on
  it; the unit test's cross-repo case guards this.
- **`Blocked on` vs `Blocked by`**: keep them distinct. The plain widening is
  for `Blocked by:` + `#N`; the `Blocked on` header stays a separate
  lower-precedence fallback (it already needs `#`/`PR` to gate, so an
  incidental "Blocked on the redesign" sentence can't gate forever).
- **Recommended companion (NOT in this PR, note for the human)**: file-epic
  already mandates the bold standalone form and marks plain/header prose a ❌
  anti-pattern (file-epic.md:334,375). The degraded plain form is a filing-time
  contract violation. A `fleet-validate-stack` lint that flags a child whose
  only `Blocked by:` is the degraded plain form would stop the regression at
  the source — worth a follow-up issue (filed with no labels per
  TASK-FILING.md), but the parser widening above makes the fleet robust to it
  regardless.
