# Plan: fleet-plan-lint — the deferred-approach phrase check reads prose, not code

- **Issue:** #2989
- **Model:** sonnet
- **Date:** 2026-08-23

## Verified current state

All six measured today against master `6eb34a21` (the ticket was filed 2026-08-09
against `5a1fdc9ec`; `git log origin/master -- scripts/fleet/fleet-plan-lint` ends
at `0d027b0b`/#2875, so the code at `:166-177` is exactly as the ticket quotes).

1. **Repro is live.** `fleet-plan-lint 2833` exits **1**, naming the placeholder
   literal as a deferred-approach phrase. #2833 is still OPEN and still held
   (`human:review-plan`, `fleet:agent-approved`), so the false hard-FAIL is
   standing right now, not a historical observation.
2. **Corpus, committed plans.** All 178 `.fleet/plans/*.md` on origin/master,
   scanned with the lint's own phrase predicates: exactly **1** hard-FAILs today —
   `.fleet/plans/issue-1749.md`. Its three hits are backticked sentinel-token
   literals in a no-blocker parser spec (`is_no_blocker_value` recognizing a bare
   sentinel), i.e. input data the plan commits to handling — the same shape as
   #2833's row, and notably #1749 is the sibling ticket #2833 names in its title.
   Removing code regions before the scan takes the corpus to **0** hard-FAILs.
   **No plan in the corpus loses a true positive.**
3. **Corpus, live plan comments.** 0 open engine issues sit in
   `fleet:plan-review`; of the 7 open `fleet:queued` issues, 3 carry `## Plan`
   comments (#2321, #2298, #1969) and all 3 already PASS. #2833 is the only live
   false hard-FAIL reachable today.
4. **Fork-check side.** Over the same 178 plans the two fork checks produce 0 hits
   on raw text and 0 hits on code-stripped text — **0 verdict changes**. Feeding
   them stripped text is corpus-neutral today and closes the same exposure class
   going forward.
5. **A latent scoping defect the same change fixes.** 2 of 178 plans
   (`issue-1824.md`, `issue-2743.md`) contain code-region lines that `sections()`
   currently parses as markdown headings — 15 headings collapse to 9, and 14 to 11,
   once code regions are removed. Phantom headings mis-slice the Approach/Gotchas
   allowlist the fork checks depend on.
6. **Nothing in flight collides.** No open PR touches `scripts/fleet/fleet-plan-lint`
   or `scripts/fleet/tests/test_fleet_plan_lint.sh`.

## Scope

Make the three phrase-shaped checks — the 11-entry `DEFER` list at `:173`, the
word-boundary placeholder-literal check at `:174-175`, and the substring check at
`:176-177` — read plan **prose**: fenced blocks and inline-code spans are removed
before scanning. The two fork checks, which iterate the same body, read the same
stripped text. Structural checks keep reading raw text. The exit-code contract
(0 sound / 1 hard fail / 2 usage) is unchanged.

## Approach

**One approach, picked.** The second narrowing the ticket floats — additionally
restricting the phrase set to Approach/Gotchas sections, the way the fork checks
are restricted — is **rejected on measurement**: it buys **0** additional verdict
changes across the 178-plan corpus, so it is pure behaviour surface with no
demonstrated benefit, and it would stop the self-describing phrases from firing
in Scope prose, where a deferral is still a deferral.

**Phase 1 — the helper.** In `scripts/fleet/fleet-plan-lint`, above the
`sections()` definition:

```python
# CommonMark fences are 3-OR-MORE backtick/tilde chars, closed by a run of the
# same character >= that length (corrected per opus-reviewer plan-review,
# #2989: a hardcoded exactly-3 fence misses a 4+ backtick fence, e.g. one that
# embeds a 3-backtick literal in its own body).
FENCE_RE = re.compile(
    r"(?ms)^[ \t]*(?P<f>`{3,}|~{3,}).*?^[ \t]*(?P=f)[`~]*[ \t]*$")
# CommonMark code-span rule: a run of N backticks closes on the next run of N,
# so a double-backtick span containing single backticks pairs correctly. Content
# may cross a soft line break but never a blank line, which bounds the damage an
# unbalanced backtick can do to one paragraph.
INLINE_RE = re.compile(r"(?s)(`+)((?:(?!\n[ \t]*\n).)+?)\1")


def strip_code(text):
    """Plan text with fenced blocks and inline-code spans replaced by a space,
    for the phrase-shaped checks: a plan that NAMES a deferral phrase as data (a
    test-corpus row, a quoted rejected alternative, a literal it commits to
    detecting) is the opposite of a deferred decision (#2989)."""
    return INLINE_RE.sub(" ", FENCE_RE.sub(" ", text))
```

Fences are stripped first so a fence's own backtick runs cannot pair with prose
backticks. Regions are replaced with **a space**, never the empty string, so
removal cannot glue two words into a matching phrase.

**Phase 2 — apply at exactly two seams.**

- After `plan = plans[-1]` (`:137-138`), keep `low = plan.lower()` and add
  `prose = strip_code(plan)` / `prose_low = prose.lower()`. Re-point the three
  phrase checks at `prose` / `prose_low`. Leave `is_spike` (`:139`), the core
  section matcher (`:153-163`), `is_plan_heading`, and the soft signals
  (`:242-258`) on the raw text.
- The fork loop (`:219`) becomes `for heading, body in sections(prose):` — reuse
  the local, do not call `strip_code` twice.

**Phase 3 — tests** in `scripts/fleet/tests/test_fleet_plan_lint.sh`, fixture
numbering continuing from 122. Every existing arm (100-122) must keep its verdict.

- **123 `CODE_SPAN_LITERAL`** — the #2833 regression. Pin the corpus row from
  #2833's plan comment Acceptance section **verbatim**, together with the two
  lines above it (they carry a code span that crosses a soft line break). Expect
  exit **0**. Hermetic — pin it in the fixture, do not re-fetch #2833 live.
- **124 `DEFER_IN_FENCE`** — a `DEFER` phrase (one of the 11, *not* the
  placeholder literal) appearing only inside a fenced block. Expect exit **0**.
- **125 `DEFER_IN_BACKTICKS`** — the same phrase, same wording, inside a
  single-backtick span. Expect exit **0**.
- **126 `DEFER_PROSE_CONTROL`** — the same phrase, same wording, in running
  Approach prose. Expect exit **1**.
- **127 `DOUBLE_BACKTICK_SPAN`** — the placeholder literal inside a
  double-backtick span that itself contains single-backtick spans. Expect exit
  **0**. This is a synthetic property pin (not a corpus save — #2833's plan has
  0 double-backtick spans; its `TBD` survival under a naive per-line stripper
  comes solely from the soft-line-break desync, per plan-review correction 2).
- **128 `UNBALANCED_BACKTICK`** — one stray backtick in prose, then a blank line,
  then a real deferred phrase in running Approach prose. Expect exit **1**:
  proves the blank-line bound stops a stray backtick from swallowing the rest of
  the plan and silently disabling the check.
- **129 `FORK_IN_FENCE`** — a `check whether ... or ...` sentence inside a
  3-backtick fenced block in the Approach section. Expect exit **0**.
- **130 `FORK_IN_LONG_FENCE`** (added per plan-review correction 1) — the same
  fork sentence inside a **4-backtick** fence. Expect exit **0**. Pins the
  run-length-aware `FENCE_RE`; a hardcoded-3 fence regex leaves this fixture's
  fork sentence attributed to a phantom-headed section and passing for the
  wrong reason (or failing to strip at all), so this arm is required to
  actually gate the corrected regex rather than the original ticket's version.

**Phase 4 — report the corpus run in the PR body** with counts (plans scanned,
verdict changes), re-run at implementation time rather than copied from the plan.

## Affected files

- `scripts/fleet/fleet-plan-lint` — add `FENCE_RE`, `INLINE_RE`, `strip_code()`;
  re-point the three phrase checks and the fork loop's `sections()` input at the
  stripped text; extend the header comment's "Checks (HARD => bounce)" bullet to
  record that the phrase checks read prose only.
- `scripts/fleet/tests/test_fleet_plan_lint.sh` — 8 new fixtures (123-130) and
  their arms.

No `.github/workflows/fleet-tests.yml` `paths:` edit is required: both files live
under `scripts/fleet/**`, already covered by both blocks.

## Acceptance criteria

1. **Positive-fire:** `fleet-plan-lint 2833` exits **0**. Measured exit 1 on
   master today, so this assertion changes state on the fix. Its hermetic twin is
   fixture 123.
2. Fixture 126 — a real deferred phrase in running Approach prose — still exits
   **1**. The narrowing is not a blanket disable.
3. Fixtures 124 and 125 exit **0**, proving the fix covers the whole phrase set
   rather than the one literal.
4. Fixture 127 exits **0** and fixture 128 exits **1**, pinning the two regex
   properties a simpler stripper gets wrong.
5. Fixture 129 exits **0**, fixture 130 exits **0** (run-length fence
   correctness); arms 114, 117 and 122 still exit 1 and 115, 118-121 still exit
   0 — the fork checks keep firing where forks belong after the seam change.
6. `bash scripts/fleet/tests/run_all.sh --only plan_lint` green, full `run_all.sh`
   shows no new failures, and the PR body carries the corpus counts.

## Gotchas

- **A per-line `` `[^`]*` `` stripper does not satisfy acceptance 1** — this was
  measured, not predicted. It leaves the literal standing in #2833's plan at the
  point where a code span crosses a soft line break and desynchronizes every
  pairing after it. The run-length form in phase 1 clears it. Anyone reaching
  for the obvious one-liner ships a fix whose headline criterion still fails.
- **Keep the blank-line bound.** A `(?s)` lazy pair with no bound lets one
  unbalanced backtick erase the remainder of the plan, which would make every
  plan PASS — the inverse failure, and invisible, because the tool just prints
  PASS. Fixture 128 is the pin.
- Strip fences before inline spans; that ordering is load-bearing, and is *more*
  load-bearing now that `FENCE_RE` is run-length-aware — a run-length fence
  matcher and a run-length inline matcher compete for the same backtick runs,
  and order is what resolves it.
- **`FENCE_RE` must be run-length-aware (3-or-more with a matching close),
  not hardcoded to exactly 3 backticks** (plan-review correction 1). A plan
  that embeds a fence-syntax code sample must use a longer fence than the
  sample, and the original ticket's `FENCE_RE` misses that case — including on
  this very plan comment's own Phase-1 block, which needs a 4-backtick fence
  to embed a 3-backtick literal in its regex source.
- Leaving `is_spike` on raw text is deliberate: downgrading a spike is the safe
  direction, and the structural checks genuinely need the real heading text.
- A deferral written with a code span *inside* the phrase itself would stop
  matching. Accepted: no corpus instance in 178 plans, and a stray miss is the
  same direction the fork checks' own narrowings already accept.
- The python lives in a `python3 - "$@" <<'PY'` heredoc inside a bash script, so
  `ruff check scripts/` does not reach it. The suite is the only gate — a syntax
  error surfaces as every arm failing at once, so run the suite before reading
  the failures as logic bugs.
- `test_fleet_plan_lint.sh` predates the `tests/lib_assert.sh` convention and
  carries its own `ok` / `bad` / `assert_exit`. Follow the file's local idiom.
