## Plan: widen `fleet-positive-control`'s bash tally grammar to a closed, documented form set and ratchet the suite population onto it

- **Issue:** #2917
- **Model:** opus
- **Date:** 2026-08-23

### Scope

Make the mandated control reachable for every bash suite under
`scripts/fleet/tests/`: the wrapper reads the tally of the 48 own-tally suites
as well as the 51 `summarize()` callers, says what it saw when it cannot, and a
CI-executed ratchet freezes the accepted spellings so a sixth bespoke form is
caught at the suite that introduces it rather than at the next control that
fails. The issue's fork is resolved as **(c) widen + ratchet**. No sweep over the
own-tally suites; the python lane (`unittest` trailer) is untouched.

### Verified current state (re-measured this planning pass, `origin/master` @ `6eb34a21`)

**Census** — 99 `test_*.sh` (the issue's 92 have grown by 7 since `12b8a49c8`),
re-runnable with two greps:

- `grep -l -E '^[[:space:]]*summarize\b' scripts/fleet/tests/test_*.sh | wc -l` → **51**
  call `summarize` (and no suite defines its own `summarize()` — `lib_assert.sh:98`
  is the only definition tree-wide).
- `grep -L -E '^[[:space:]]*summarize\b' scripts/fleet/tests/test_*.sh | wc -l` → **48**
  keep their own `PASS`/`FAIL` counters and `echo` the tally themselves — every
  one on a single line naming both `$PASS` and `$FAIL`. Rendering that echo and
  applying the tool's matcher (`fleet-positive-control:167`) splits them **8
  accepted / 40 rejected**. The 40 rejected spellings are the issue's five, one
  fewer in the first bucket:

  | count | rendered shape | example |
  |---|---|---|
  | 24 | `PASS: <n>  FAIL: <n>` | `test_derive_host.sh`, `test_dispatch_wrap_session.sh` |
  | 13 | `  PASS: <n>    FAIL: <n>` (indented) | `test_fleet_queue_ingest_blocked_by.sh` |
  | 1 | `PASS: <n>    FAIL: <n>` | `test_fleet_queue_ingest_late_no_plan.sh` |
  | 1 | `PASS=<n> FAIL=<n>` | `test_classify_auto_rereview.sh` |
  | 1 | `pass: <n>  fail: <n>` | `test_ir_build_dir_resolution.sh` |

  The 8 accepted own-tally suites print `passed: <n>  failed: <n>` (6) or
  `<label>: <n> passed, <n> failed` (2: `test_fleet_claim_acquire.sh`,
  `test_fleet_rebase_plan_automerge.sh`).

**Repro, first-hand through the actual wrapper** (7 arms, same ref; the only
variable is the suite's tally spelling):

| arm | suite | tally it printed | exit | verdict |
|---|---|---|---|---|
| 1 | `test_dispatch_wrap_session.sh` | `PASS: 41  FAIL: 0` | 2 | none — "printed no tally … aborted before summarizing" |
| 2 | `test_derive_host.sh` | `PASS: 8  FAIL: 0` | 2 | same |
| 3 | `test_fleet_queue_ingest_blocked_by.sh` | `  PASS: 9    FAIL: 0` | 2 | same |
| 5 | `test_classify_auto_rereview.sh` | `PASS=5 FAIL=0` | 2 | same |
| 4 (control) | `test_fleet_claim_safety_guards.sh` (`summarize`) | `passed: 25  failed: 0` | 1 | `VACUOUS: all 25 assertions pass against origin/master (6eb34a21)` |
| 7 (control) | `test_worktree_settings_hooks.sh` (own tally, summarize spelling) | `passed: 20  failed: 0` | 1 | `VACUOUS: all 20 assertions pass …` |
| 6 | `test_ir_build_dir_resolution.sh` | (none — aborts: `helpers not found at <stage>/engine/tools/lib/concurrency_helpers.sh`; with `--include engine/tools/lib` it still aborts: `ir-tools: cannot resolve engine root`) | 2 | the **genuine** no-tally case |

Arm 7 is the extra discriminator: an own-tally suite spelled like `summarize()`
reaches a verdict, so the spelling — not own-tally-ness — is the whole defect.
Arm 6 is the true setup-failure path the fix must keep routing to exit 2.

**Host note (how the arms were run, not a plan dependency).** On the
native-Windows fleet host the wrapper dies earlier with `test file is outside the
repo` (git toplevel `C:/…` vs `pwd` `/c/…`, both bashes) — filed as **#3047**
(agent-approved, no-plan, independent of this issue). The arms above were run
through WSL against the main clone at the same sha:
`wsl.exe -e bash -lc 'cd /mnt/c/Users/evinj/src/IrredenEngine && scripts/fleet/fleet-positive-control <suite> origin/master'`.
The wrapper is also not allowlisted for headless panes (#3033), so the
implementer's own controls need an interactive bash-capable pane on Linux/macOS
or the WSL recipe.

**Sibling + in-flight reconciliation.** No open PR touches
`fleet-positive-control` or `test_positive_control.sh` (file lists of #3045,
#3043, #3035, #3000, #2964, #2936 checked). **#2845** (planned, plan-review
cleared 2026-08-09, not queued, not claimed, no PR) adds one
`source lib_preflight.sh` line to its 47 lane-3 suites and new blocks to
`test_positive_control.sh`; it never edits a tally line, so the two compose —
this plan deliberately edits **none** of the 48 own-tally suites so the two
never conflict on them; the shared `test_positive_control.sh` appends merge
mechanically whichever lands second. #3035 and #2936 each edit a different
bullet of `scripts/fleet/CLAUDE.md` (mechanical overlap with the doc change
here). #2848/#2854 (python lane) and #2713/#2720 (staging) are adjacent and
disjoint.

### Approach — (c) widen + ratchet, committed

Rejected: **(a) alone** leaves the grammar unbounded, and per #3033 the next
bespoke spelling would go unnoticed until a human runs the control. **(b)** (a
48-file normalization onto `summarize()`) collides with #2845's pending 47-file
sweep and the six open fleet PRs on the same files, and still needs the ratchet
to stop *new* own-tally suites — so (b) is (c) plus a sweep. (c) reaches every
acceptance criterion by touching the tool, its test, one new ratchet suite and
the docs; the legacy set becomes a baseline that only shrinks, which reaches
(b)'s end state incrementally as suites migrate.

**Phase 0 — premise probe (cheap).** At the implementation base re-run the two
census greps and confirm `grep -L` count = 99 − `grep -l` count, and that every
`grep -L` file's last `echo` naming `$PASS`/`$FAIL` renders to one of the seven
forms in the grammar below (the planning pass classified all 48 with 0
unclassified). A live eighth spelling is a census delta: add it to the grammar,
the lock table and the diagnostic's accepted-forms list, and note it on the
issue. The premise that is load-bearing — *every own-tally suite emits both
counts on one line* — is what the single-line grammar rests on; if a suite
prints them on separate lines, stop, comment the measurement on the issue and
re-plan rather than build a multi-line parser under this plan.

**Phase 1 — the grammar, one ERE, in the wrapper.** Replace the matcher at
`fleet-positive-control:167` with a named regex covering exactly seven forms —
`summarize()`'s two plus the five legacy spellings; the only generalisation is
whitespace (leading indent, the run between the two tokens):

```sh
# Accepted bash tally forms — keep in sync with the table in tests/test_positive_control.sh
# and the ratchet in tests/test_suite_tally_forms.sh.
BASH_TALLY_RE='^[[:space:]]*(.*: [0-9]+ passed, [0-9]+ failed|passed: [0-9]+  failed: [0-9]+|(PASS|pass): [0-9]+[[:space:]]+(FAIL|fail): [0-9]+|PASS=[0-9]+ FAIL=[0-9]+)[[:space:]]*$'
TALLY=$(grep -E "$BASH_TALLY_RE" "$STAGE/.out" | tail -1 || true)
```

Count extraction replaces the two per-form `BASH_REMATCH` arms with one rule —
the **last two integers** on the matched line (`$`-anchored, so a label with
digits such as `summarize "T12 tests"` still yields the final pair):
`[[ "$TALLY" =~ ([0-9]+)[^0-9]+([0-9]+)[^0-9]*$ ]]` → `P=${BASH_REMATCH[1]}`,
`F=${BASH_REMATCH[2]}`. `tail -1` stays: the suite's last tally-shaped line
wins (`test_positive_control.sh` echoes fixture tallies mid-run and its own
`summarize` last). `run_all.sh`'s own summary (`… passed, N failed, M skipped`)
is rejected by the `$` anchor — pin it.

Expose the parser: **`fleet-positive-control --parse-tally <file>`** applies
`BASH_TALLY_RE` to `<file>` and prints `<P> <F>` (exit 0), or exits 2 with the
same diagnostic the main path prints (Phase 2). This is the single executor the
ratchet and the lock call, so there is one copy of the grammar and nothing to
drift (the `cpp-globals.md` "a detection spec nothing runs drifts silently"
lesson; #2876 on ratchets needing their own positive control). It short-circuits
before `git rev-parse`/`mktemp`/staging, needs no `<ref>`, and the
`no_tally`/unrecognized diagnostics print their `Ran as:` line only when
`INTERP`/`TEST_REL` are set. Document the mode in the header comment (that is
the `--help`, per the no-drift rule) and in `fleet-help:376`.

**Phase 2 — the diagnostic says what it saw.** Split the bash `no_tally` exit
(still exit 2 on both arms):

- *unrecognized tally* — the output has a tally-looking line
  (`grep -iE '(pass|passed)[a-z]*[:=]? *[0-9]+.*(fail|failed)[a-z]*[:=]? *[0-9]+' | tail -1`)
  that `BASH_TALLY_RE` rejected → print `the suite printed a tally this tool
  does not recognize: '<line>'`, list the accepted forms, and say: end the
  suite with `summarize` from `tests/lib_assert.sh` (`scripts/fleet/CLAUDE.md`).
  The "aborted before summarizing" text must **not** appear on this arm.
- *no tally at all* — the existing setup-failure text, unchanged.

Update `SUMMARY_FORM` for `.sh` to "a recognized tally line (summarize()'s, or
one of the legacy `PASS: N  FAIL: M` forms the ratchet pins)".

**Phase 3 — matcher lock, in `tests/test_positive_control.sh`** (the issue's
named home for the regression lock):

- `--parse-tally` table — one temp file per accepted form, asserting exit 0 and
  the printed pair: `passed: 8  failed: 0` → `8 0`; `suite: 25 passed, 0 failed`
  → `25 0`; `T12 tests: 5 passed, 0 failed` → `5 0` (digits in the label);
  `PASS: 41  FAIL: 0`; `  PASS: 9    FAIL: 0`; `PASS: 3    FAIL: 1` → `3 1`;
  `pass: 2  fail: 0`; `PASS=5 FAIL=0`. A file whose last tally-shaped line
  follows an earlier one (`PASS: 1  FAIL: 1` then `passed: 7  failed: 0`) → `7 0`
  ("last wins").
- Negative table — exit 2 each: `Passed=5 Failed=0` and a two-line
  `PASS: 5` / `FAIL: 0` → output contains `does not recognize` and **not**
  `aborted before summarizing`; `run_all.sh: 3 suite(s) — 3 passed, 0 failed, 0
  skipped` → rejected; an empty file → `printed no tally` + `aborted before
  summarizing` and **not** `does not recognize`.
- End-to-end through staging, same shape as the existing VAC/MEAN fixtures
  (untracked-marker discriminator, `STRAYS` cleanup): an own-tally fixture that
  ends with `echo "PASS: $PASS  FAIL: $FAIL"` (no `summarize`) → exit 0,
  `MEANINGFUL: 1 of 2 assertions fail`, `(1 + 1 = 2, the full suite.)`; an
  own-tally vacuous fixture → exit 1 `VACUOUS`; an own-tally fixture printing
  `PASS: 0  FAIL: 0` → exit 2 `reported 0 assertions` (the widened grammar must
  not turn an empty run into a verdict). The existing NOTALLY fixture (exits 3,
  no tally) keeps its exit-2 `printed no tally` assertion.

**Phase 4 — population ratchet, new `tests/test_suite_tally_forms.sh`**
(a per-file convention check, the `test_suites_are_executable.sh` shape; kept
out of the 420-line staging suite). Two lists and three checks, implemented as
a function `check_tally_forms <tests-dir> <baseline-name>...` so fixtures can
drive it:

1. Every on-disk `test_*.sh` in `<tests-dir>` that does not call `summarize`
   (`^[[:space:]]*summarize\b`) must be named in `OWN_TALLY_BASELINE` — the
   48 names re-derived at the implementation base with the `grep -L` one-liner.
   The header states the ratchet rule: a **new** bash suite ends with
   `summarize` (source `lib_assert.sh`); an entry may be **removed** when its
   suite migrates, never added.
2. Every baseline entry must exist on disk (a renamed or deleted suite prunes
   its entry — the list stays honest, the `cpp-globals.md` register rule), and
   its last `echo` line naming both `$PASS`/`${PASS}` and `$FAIL`/`${FAIL}`,
   rendered with `PASS→7`, `FAIL→3` and one layer of quotes stripped, must make
   `fleet-positive-control --parse-tally` print `7 3`. A baseline suite whose
   tally emission the renderer cannot find is **flagged** (fail closed — a
   bespoke emission style is exactly what this exists to surface); the header
   documents the accepted emission shape (`echo "<form>"`, both variables on
   one line). This is what pins the *spellings*, not just the population.
3. No `test_*.sh` defines its own `summarize()` (`^[[:space:]]*summarize[[:space:]]*\(\)`)
   — a local redefinition printing a bespoke form would bypass both the grammar
   and check 1.

Its own positive controls, against a temp fixture dir: a `summarize` caller
passes; a baseline-named suite with a legacy echo passes; a non-baseline
own-tally suite is flagged (check 1); a baseline-named suite echoing
`Passed=$PASS Failed=$FAIL` is flagged (check 2); a baseline entry with no file
is flagged (check 2); a suite with a local `summarize()` is flagged (check 3).
Then the real tree: `check_tally_forms "$TESTS_DIR" "${OWN_TALLY_BASELINE[@]}"`
reports nothing. `run_all.sh` discovers the new suite by glob and
`fleet-tests.yml` runs it on every PR touching `scripts/fleet/**` — no
registration, no `install.sh` change (no new executable).

**Phase 5 — docs.** `scripts/fleet/CLAUDE.md`: the positive-control bullet
names the seven-form grammar, `--parse-tally`, and the ratchet rule; the
"Bash tests source `tests/lib_assert.sh`" bullet points at
`test_suite_tally_forms.sh` as its executor. `fleet-help:376` gains the
`--parse-tally <file>` usage line. The wrapper's header comment (its `--help`)
is rewritten in Phase 1. `lib_assert.sh` is untouched.

### Affected files

- `scripts/fleet/fleet-positive-control` — `BASH_TALLY_RE` (seven forms), last-two-integers extraction, `--parse-tally <file>` mode, two-arm bash diagnostic, `SUMMARY_FORM` text, header/`--help`
- `scripts/fleet/tests/test_positive_control.sh` — `--parse-tally` accept/reject tables, own-tally end-to-end fixtures (MEANINGFUL / VACUOUS / 0-assertion), last-wins case; existing NOTALLY assertion kept
- `scripts/fleet/tests/test_suite_tally_forms.sh` — **new**: `OWN_TALLY_BASELINE` (48 at `6eb34a21`; re-derive), `check_tally_forms`, the three checks, fixture positive controls, executable bit committed
- `scripts/fleet/CLAUDE.md` — positive-control bullet (grammar, `--parse-tally`, ratchet rule); `lib_assert.sh` bullet (executor pointer)
- `scripts/fleet/fleet-help` — `--parse-tally` usage line

### Acceptance criteria

1. `fleet-positive-control scripts/fleet/tests/test_derive_host.sh origin/master`
   → exit 1, `VACUOUS: all 8 assertions pass against origin/master (<sha>)`
   (pre-fix measured: exit 2, `printed no tally`). Same verdict shape for
   `test_dispatch_wrap_session.sh` (41/0, bare), `test_fleet_queue_ingest_blocked_by.sh`
   (9/0, indented) and `test_classify_auto_rereview.sh` (5/0, `PASS=` form) —
   one per rejected bucket; the lowercase bucket is pinned by the parser table
   (its only live suite, `test_ir_build_dir_resolution.sh`, aborts under the
   wrapper for an unrelated staging reason — arm 6).
2. Unchanged lanes: `test_fleet_claim_safety_guards.sh` → exit 1
   `VACUOUS: all 25 assertions pass`; the NOTALLY fixture → exit 2
   `printed no tally`; an own-tally `PASS: 0  FAIL: 0` fixture → exit 2
   `reported 0 assertions`; `test_ir_build_dir_resolution.sh` without
   `--include` → exit 2 with the setup-failure text (no false `does not
   recognize`).
3. The diagnostic: a bespoke-form fixture → exit 2, output names the offending
   line, contains `does not recognize`, and does **not** contain `aborted before
   summarizing`.
4. `test_positive_control.sh`'s new tables and fixtures pass on the branch, and
   its positive control is MEANINGFUL by construction:
   `fleet-positive-control scripts/fleet/tests/test_positive_control.sh origin/master`
   runs the new suite against the **staged old wrapper**, so every `--parse-tally`
   and own-tally-fixture assertion fails there (expected fail count = those
   assertions; the pre-existing ones still pass) — paste the emitted test-plan
   line.
5. `test_suite_tally_forms.sh` on master's population flags nothing (51 + 48);
   its six fixture controls fire; removing one `OWN_TALLY_BASELINE` entry
   (spot-check) fails the suite naming that file; and
   `fleet-positive-control scripts/fleet/tests/test_suite_tally_forms.sh origin/master`
   is MEANINGFUL (the staged old wrapper has no `--parse-tally`, so every
   check-2 render fails there).
6. `bash scripts/fleet/tests/run_all.sh` reports no suite newly failing against a
   same-host master baseline; `fleet-tests.yml` green.

### Gotchas

- **`tail -1` is load-bearing.** The grammar now also matches the
  `PASS: n  FAIL: m` lines that `test_positive_control.sh` prints *inside*
  fixture output mid-run; its own `summarize "fleet-positive-control tests"`
  is last and wins. Keep the last-wins case in the lock table so a reorder is
  caught.
- **Exit codes, not exit words.** Both diagnostic arms exit 2 — the split is
  in the text; the lock asserts on the text of each arm *and* the absence of
  the other's.
- **`set -u` in `--parse-tally` mode.** `STAGE`, `INTERP`, `TEST_REL`, `RC`
  are unset there; the shared diagnostic functions must read them as
  `${VAR:-}` (the `#2455` array/`nounset` rule applies to every new
  expansion).
- **Fixture hygiene.** Fixtures written into `tests/` go on `STRAYS` and are
  removed on every exit path (the wrapper requires an in-repo suite); marker
  files must stay untracked (`git archive` emits tracked content only — the
  #2723 self-invalidating-fixture lesson in the file header).
- **The renderer is deliberately narrow.** `echo "…"` / `echo '…'` with
  `$PASS`/`${PASS}` and `$FAIL`/`${FAIL}` on one line is the entire accepted
  emission shape (all 48 today); `printf` or a split echo is flagged, by
  design. Say so in the suite header so a flagged author knows the fix is
  `summarize`, not a renderer patch.
- **Keep this PR out of the 48 own-tally suites.** #2845's sweep lands a line
  in 47 of them; this PR touching any of those files turns a mechanical rebase
  into a 47-file conflict for whichever lands second.
- **Host reality.** The wrapper cannot run headless (#3033) and cannot run at
  all under MSYS bash + Git-for-Windows git until #3047 lands; run the AC
  controls from an interactive bash pane on Linux/macOS, or through WSL against
  the main clone as in the planning pass. The new suites themselves run under
  `run_all.sh`/CI normally on every host.
- **`--help` derives from the header comment** (`awk` over the `#` block), so
  the seven forms and `--parse-tally` go in the header, not a separate usage
  string; `fleet-help:376` is a second hand-maintained copy of the usage line.
- Plans are engine-public; nothing here names game-side work.
