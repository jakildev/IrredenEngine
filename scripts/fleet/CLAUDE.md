# scripts/fleet/ — fleet tooling

Bash + Python tooling for the autonomous fleet (scout, dispatcher, claim,
install, per-tool wrappers) and its tests under `tests/`. Python style is
ruff-enforced (`ruff check scripts/`, CI-gated); the engine comment policy
applies here too — see `docs/agents/CLAUDE-BASELINE.md` §Style.

## Authoring rules

- **Tests are hermetic — no live GitHub, no live `~/.fleet`.** Mock network
  fetchers at a seam that *fails closed*: a mock miss must raise, never fall
  through to `urllib`/`gh` (#2227 shipped tests that silently hit the live
  API and wrote the production scout's ETag cache). Never let a test share
  `fleet_gh_poll.DEFAULT_CACHE_DIR` (`~/.fleet/state/etag`) — inject a
  `tempfile.TemporaryDirectory()` `cache_dir` instead. When migrating a
  fetcher's transport (e.g. `run_capture(gh)` → `conditional_get`), re-point
  *every* test mock at the new seam in the same PR.
  The same duty applies when a function **acquires** its first network call
  rather than changing an existing one: every suite already covering it was
  written network-free and silently starts reaching live GitHub, so its
  verdicts become a function of production state. Re-point those suites in the
  same PR — and prefer fixtures that are obviously synthetic, since the tell is
  easy to miss when the fixture uses plausible real IDs (#2986 added a live
  fallback to `resolve_needs_plan_blocked_by`; the pre-existing suite's "open
  blocker" fixture was a real issue number that had since closed, so the suite
  failed on live state rather than on the code under test).
- **A CLI stub models the tool's argument parsing, not just its endpoint.**
  A `gh` stub that matches a substring of `"$@"` and ignores flags accepts
  arguments the real binary rejects, so the suite certifies a call that has
  never once succeeded in production. `label_added_epoch` passed jq's
  `--arg` to `gh api` — rejected before the request, swallowed by
  `2>/dev/null`, permanently returning "age unknown" and silently disabling
  every claim-label TTL sweep — while every suite covering it stayed
  green (#2781).
  Validate flags against the real tool's accepted set (transcribe it from
  `--help`) and fail the way it fails; where the stub emulates `--jq`,
  evaluate the program against fixture JSON instead of pre-baking the
  filter's answer, and fail closed on a program shape you don't model.
  Pair it with a fidelity assertion (the stub still rejects the flag the
  bug used), or the next stub rewrite silently reopens the hole.
- **Exercise every arm — dual spellings and defaulting chains alike.**
  A wrapper that accepts `--opt val` and `--opt=val` has two independent
  case arms: reject an empty `--opt=` exactly as the space form rejects a
  missing value. A diverging equals arm lets `--opt=$UNSET_VAR` slip an
  empty string past downstream `[[ -n "$var" ]]` guards (#2193: bypassed a
  claim check). The same rule covers `${VAR:-${FALLBACK:-literal}}`
  defaulting chains: a test fixture that always pins the primary input
  (e.g. `export FLEET_MODEL_*`) shadows the fallback arm, so the default
  path ships unexercised — unset the pinned inputs in ≥1 case (#2569:
  the alias-default fallback in dispatcher/solo-architect model resolution
  had zero coverage until T19).
- **`state.json` staleness comes from the in-file `generated_at`, never file
  mtime.** Canonical rule + rationale: `docs/agents/FLEET-RUNTIME.md`.
  Shared helper: `fleet_poll_topology.state_age_seconds` (a heredoc consumer
  that can't import mirrors it inline, commented as such). A best-effort CI
  ratchet (`lint_state_mtime.py`) flags a new `.st_mtime` read that co-occurs
  with a `state.json` reference; opt a justified read out with an inline
  `# lint: state-mtime-ok <reason>` comment.
- **A test arm measured from wall-clock now needs an injected clock, not a
  wider window.** Fixture timestamps are fixed; anything the subject derives
  from `now` is not, so the arm's meaning is a function of the date the suite
  runs. Widening the window (`1d` → `3650d`) removes the red without restoring
  the assertion — the arm then passes for every input and no longer discriminates.
  Give the subject a `now` seam its tests can pin (`fleet-health`'s `now_utc()`
  reads `FLEET_HEALTH_NOW`), derive the pinned instant from the fixture's own
  timestamps, and pair the arm with the complementary one — a window that
  *excludes* the fixture, asserting the reported boundary rather than the
  resulting emptiness, which a real clock reproduces for free (#3132).
- **A new executable ships with a `tests/test_<name>.{sh,py}`** in the same
  PR — the fleet-tooling form of the review checklist's "new feature with no
  new test"; the `simplify` pre-commit pass flags the omission. Run the whole
  set with `bash scripts/fleet/tests/run_all.sh` (`--only <substring>` while
  iterating); discovery is by glob, so a new suite needs no registration.
  These are not CMake tests — `ctest` does not cover them, so the dedicated
  `fleet-tests.yml` workflow calls the runner directly on every push and PR
  that touches `scripts/**`. That workflow is the only thing gating
  them; an unexecuted suite goes red silently (see #2712). The filter is the
  whole of `scripts/`, not `scripts/fleet/`, because `lint_python_registry.py`
  derives its population from that wider root (#2859) — it deliberately
  overlaps `render-harness-tests.yml`'s `scripts/*.py`.
- **A suite whose subject under test is missing must not report success.**
  A guard shaped `if [[ ! -f "$SUBJECT" ]]; then echo "SKIP: ..."; exit 0;
  fi` reports the same exit status as a real pass, so `run_all.sh` folds a
  suite that verified nothing into its "N passed" tally — the vacuous-pass
  failure mode #2712 wired execution to prevent, one level down (#2786).
  Exit **3** instead — the shared skip status `run_all.sh` recognizes and
  tallies separately as "skipped", never "passed". Reserve the `SKIP:`
  stderr prefix for this case (an environment-dependency skip like "git not
  available" can stay `exit 0`; only a missing *subject* is a #2786 case).
  If the subject lives outside `scripts/**`, also add its path to
  `fleet-tests.yml`'s `paths:` filter — otherwise a PR that moves or edits
  only that file never runs the suite that would have caught the break.
  (A subject *inside* `scripts/` needs no entry: the filter's first glob
  already covers it.)
  **This is executed** — add the path to `OUT_OF_TREE_SUBJECTS` in
  `tests/test_fleet_tests_workflow_paths.sh` in the same change, and the
  ratchet asserts it appears in **both** `paths:` blocks (they are
  hand-duplicated — GitHub Actions has no YAML anchors — so they drift
  independently). The list is the ratchet's whole domain: a subject absent
  from it is a subject nothing guards, however green the suite runs
  (#2810). `OUT_OF_TREE_SUBJECTS` is `fleet-tests.yml`-scoped by design —
  it is that one workflow's own subject-domain list, a different axis from
  whether a workflow's `push:` and `pull_request:` blocks *agree* on
  whatever they list. That second axis — the sync ratchet itself — is
  **not** `fleet-tests.yml`-scoped: `tests/test_workflow_paths_sync.sh`
  (#2929) derives the workflow population from a `.github/workflows/*.yml`
  glob and checks every workflow that declares both blocks
  (`fleet-tests.yml`, `format-check.yml`, `header-checks.yml`,
  `perf-gate.yml`, `python-lint.yml`, and `render-harness-tests.yml`
  today). Its *population* needs no registration step; its *trigger*
  does. Those workflows live outside `scripts/fleet/**`, so
  they are themselves out-of-tree subjects and carry entries in
  `OUT_OF_TREE_SUBJECTS` beside the fixed-file ones — a workflow that
  declares both blocks but is missing from `fleet-tests.yml`'s `paths:` is
  one this suite inspects and CI never runs it for. That is the pair the
  two ratchets have to agree on, and the derived side is the one a
  hand-maintained list cannot follow, so `test_workflow_paths_sync.sh`'s T4
  asserts the agreement directly off the glob: a newly-covered workflow
  fails a suite instead of silently costing itself its trigger —
  `python-lint.yml` (#2718) is the case that exercised it, and
  `format-check.yml` (#3187) the second.
- **Bash tests source `tests/lib_assert.sh`** for the PASS/FAIL counters,
  `ok`/`bad`, `assert_eq`/`assert_contains`/`assert_absent`, and the
  `summarize` exit idiom — don't re-copy the helpers into a new test.
  Genuinely test-specific asserts (path existence, exit codes) stay local,
  built on `ok`/`bad`. **End a new suite with `summarize`**, never a private
  `echo "PASS: $PASS  FAIL: $FAIL"` — the wrapper below reads that line for
  its counts, and a bespoke spelling made 41 of 92 suites un-controllable
  (#2917). **This is executed**: `tests/test_suite_tally_forms.sh` is the
  ratchet. Its `OWN_TALLY_BASELINE` names the 45 suites that predate the rule
  and is shrink-only — an entry leaves when its suite migrates, none may be
  added. The suite also refuses a local `summarize()` redefinition, which
  would print anything it liked while passing the population check.
- **Positive-control a new suite with `fleet-positive-control`, never by hand.**
  `fleet-tests.yml` proves a suite is *green*; only a run against the pre-fix
  ref proves it would have gone *red* on the bug, so a new suite's worth still
  rests on that control. Don't hand-stage it: the wrappers dispatch to the
  `fleet_*.py` modules beside them, so a partial stage aborts every invocation
  on its lib-dir preflight and the suite scores those as ordinary assertion
  failures — printing a **plausible but wrong tally** rather than an error
  (#2713: a mis-stage read 2 passed / 21 failed where the truth was 14 / 9,
  inflating the fix's apparent coverage). `fleet-positive-control <test-file>
  <ref>` stages the whole directory with `git archive`, reports MEANINGFUL vs
  VACUOUS, and emits the test-plan line with its arithmetic shown. It drives
  both suite types — `.sh` under bash, `.py` under python3, dispatched on the
  extension exactly as `run_all.sh` does, since neither carries a shebang
  (#2848) — and reads each one's own summary line for the counts, so a suite
  that prints none, or that runs zero assertions, is a setup failure rather
  than a verdict.
  For bash that summary line is a **closed grammar of seven forms**:
  `summarize()`'s two (`passed: N  failed: M`, `<label>: N passed, M failed`)
  plus four legacy `PASS`/`pass` × `FAIL`/`fail` colon spellings and
  `PASS=N FAIL=M`, whitespace-flexible and anchored at both ends (which is what
  keeps `run_all.sh`'s own `… N passed, M failed, K skipped` summary out of the
  set). `--parse-tally <file>` applies that grammar standalone and prints
  `<passed> <failed>` — it is the **single executor**, called by
  `tests/test_positive_control.sh`'s accept/reject tables and by the
  `tests/test_suite_tally_forms.sh` ratchet, so the grammar has one copy and
  nothing to drift from. A suite that ran and printed a tally the grammar
  rejects gets its own diagnostic naming the offending line; only a suite that
  printed *nothing* is reported as having aborted before summarizing. Both exit
  2 — the split is in the text, and the tests assert on each arm's wording *and*
  the absence of the other's (#2917).
  `tests/lib_preflight.sh` is the backstop for controls still run by hand, and
  a bash suite adopts it with one line after its `SCRIPT_DIR` assignment:
  `source "$(dirname "$0")/lib_preflight.sh"`. Sourcing `lib_assert.sh` pulls
  it in too, but only from that line down — put the preflight source **above
  the first line that builds a path to a `fleet-*` wrapper**, which is what
  `test_positive_control.sh`'s adoption ratchet checks. Don't scope a guard
  like this to the file it happens to live in: the previous form fired only
  for suites that sourced `lib_assert.sh` with `SCRIPT_DIR` already set —
  49 of 91 hazard-bearing suites were uncovered, its documented
  "call `require_fleet_lib_dir` explicitly" escape hatch collected zero takers
  in two weeks, and a mis-staged lane-3 suite reported `PASS: 4  FAIL: 7`
  against a truth of 11/11 (#2845). The ratchet is the durable half: adoption
  is enforced tree-wide, not remembered, so a new suite that forgets the line
  goes red instead of reporting a plausible-but-wrong tally.
  Same for **before/after** evidence: report **coverage** beside the drift
  count — an input rejected at the entry guard agrees on both revisions,
  vacuously (#2875: `drifted: 0` was 41 of 166 linted).
  The one axis this control **cannot** grade is an **exclusion** assertion —
  one checking something is deliberately *not* matched. The verdict is a
  whole-suite aggregate, and an exclusion usually predates the fix, so it
  passes against the pre-fix ref too and MEANINGFUL came from the other arms.
  The load-bearing proof is mutation of the *current* implementation — delete
  the exclusion there and watch that assertion go red — per
  [`docs/agents/CLAUDE-BASELINE.md`](../../docs/agents/CLAUDE-BASELINE.md)
  §"Encode contracts in code, not in comments". Same-fixture reachability (a
  known violation beside the exempt declaration, or a coverage count that
  moves) is cheaper but complementary: it proves the subject was reached, not
  that the assertion bites (#3127).
- **A new `tests/test_*.sh` file needs its executable bit committed**
  (`git update-index --chmod=+x` if `git add` didn't pick it up from your
  filesystem's mode). `run_all.sh` invokes suites through an explicit
  interpreter, so a `100644` one still *runs* in CI — but it returns 126
  under the direct `./"$f"` form its shebang implies, and any `[[ -x "$f" ]]`
  filter a future runner adds would skip it silently (#2725).
  `test_suites_are_executable.sh` guards the convention for `test_*.sh`;
  `test_*.py` suites are `100644` by convention (always run as `python3 "$f"`).
- **Bash array footguns: guard the empty case and the paired case.** A
  `"${arr[@]}"` expansion under `set -u` where `arr` can legitimately be
  empty must pre-check `(( ${#arr[@]} > 0 ))` — or the script's declared
  bash floor must actually be ≥ 4.4, since 4.0–4.3 throw "unbound
  variable" expanding an empty `[@]` under `nounset` (a floor guard that
  admits 4.0 while relying on 4.4 behavior is internally inconsistent,
  #2455). A helper that reconstructs two logical arrays from a flattened
  `"${OLD[@]}" "${NEW[@]}"` argument list must assert length parity both
  at the definition site and inside the callee — an edit to one twin
  otherwise silently mis-maps every element past the imbalance point
  (#2454).
- **`fleet-up`'s bootstrap heredoc cannot import — inline, and guard the
  copy with a test.** That block is a standalone `python3 - <<'PY'` with no
  path to `FLEET_LIB_DIR`, run under `|| true`: an `ImportError` from sys.path
  plumbing is swallowed silently and takes the bootstrap trigger down for
  *every* role at once, which is strictly worse than whatever the import was
  buying. Copy the constant/predicate in (the precedent is
  `fleet_task_class._current_host`, #1578), name the canonical copy in a
  `keep in sync` comment, and ship a drift guard that lifts the heredoc's defs
  and asserts they still agree — a duplicate with no guard is the whole cost of
  inlining. `test_smoke_worker_projection.py`'s `TwoCopiesAgree` is the
  reference shape. Bash reaches `fleet_task_class.py` the other way, through a
  CLI arm (`--pick`, `--pick-role`, `--smoke-check`), never an import —
  `fleet_completion.py assess` (the dispatch completion contract) is the same
  shape: the dispatcher fetches, the module decides on files it is handed.
- **`state.json` has a hard size ceiling; don't widen the projection without
  checking it.** Every role reads it through the Read tool's 256 KB cap, so the
  scout emits it **compact** and retains a review body only on the latest review
  per PR — do not "tidy" either back. The scout warns and writes an alert past
  7/8 of the cap. Invariant + the measured budget:
  [`docs/agents/FLEET-CACHE.md`](../../docs/agents/FLEET-CACHE.md) §"Size
  invariant". **A change to the per-PR record shape bumps `PR_RECORD_SCHEMA`**
  in the same commit, or `fetch_prs`'s 304 fast path keeps serving the pre-change
  shape out of the on-disk `state.json` after the deploy — for an unbounded
  number of ticks on a quiet repo. The trim above shipped that way and emitted
  348 KB on tick 1 of every restart with the trim code live (#3037).
- **Concurrently-read state writers use `write_atomic`, never plain
  `write_text`.** A JSON/state/cache file that another process may read
  mid-write is persisted with the module's `write_atomic()` helper
  (unique-temp + `os.replace`) — a plain `Path.write_text(json.dumps(...))`
  can hand a reader a torn splice (#2459). Grep-able tell:
  `.write_text(json.dumps(` in a module that defines `write_atomic`.
- **A script's `--help` and docstrings must not drift from its code.** A
  `--help` that slices its own header via a hardcoded `sed -n 'N,Mp' "$0"`
  range must derive the end from the header content (first non-`#` line)
  or ship a `--help` regression test asserting no post-header code lines
  leak (#2433 leaked `set -u` into help output). And when a diff
  adds/removes an enumerated pass/subcommand/sweep, the `--help`
  count/enumeration and any enumerating docstring update in the same
  change (#2467: `cmd_cleanup_gh` grew a 5th sweep while its banner said
  "four" and its docstring said "Three-pass").
  The sweep covers operator-facing siblings in other files
  (`fleet-up.conf.sample`, `fleet-help`'s index, printed column labels), both
  ways: the retired name gone *and* its replacement documented (#3094).
- **A new consumer of a PR label excludes PRs claimable by other lanes
  with disjoint claim namespaces.** Disjoint claim-label namespaces
  (`fleet:amending-*` vs `fleet:resolving-*`) provide **no** mutual
  exclusion — a PR matching two lanes' filters on one scout tick gets two
  panes force-pushing the same head branch, last writer wins (#2423: the
  semantic-conflict lane had to learn to exclude feedback-owing PRs;
  #2801: the same for `fleet:reviewing-*` vs `fleet:amending-*`, where the
  exclusion existed on the reviewer side only, so a worker could amend a PR
  mid-review). Check this whenever a lane is added or its label set widened.
  The exclusion is **directional** — closing one side leaves the hazard
  live, and both lanes are woken by the same labels by design, so a
  one-sided guard reads as complete while the race is untouched. It is also
  **per-lane-pair**: #2801 closed `fleet:reviewing-*` against
  `fleet:amending-*` and left the *same* hazard live against
  `fleet:resolving-*` for three weeks, because the fix's own acceptance
  criteria named one lane (#3001). When a guard lands, enumerate every
  claim-taking lane that force-pushes — today `cmd_amending_claim` and
  `cmd_resolving_claim`, both calling `check_no_foreign_review_claim` — not
  just the one the incident came from. The **merger** force-pushes too
  (`fleet-rebase`) and is outside this rule by design: it takes no
  `fleet-claim` lock at all, using `--force-with-lease` as its concurrency
  control (`role-merger.md`; `NO_CLAIM_FABRIC_ROLES` in `fleet-dispatcher`).
  A guard added here does not reach it — a mid-review mechanical rebase is a
  separate, accepted design point, not an oversight this rule covers. A
  lane's admission test must also be the **sole** one for that lane: an
  emit-site filter that its `slice_<role>` twin doesn't share leaves every
  consumer of the slice (class election, quiet check, `fleet-up`'s bootstrap
  trigger) reading the unfiltered set (#2801 again — `project_worker` and
  `slice_worker` now share `worker_feedback_labels()`).
- **Path-containment checks normalize before the literal match.** A
  path-containment policy check (worktree guard, scope guard, allowlist)
  must normalize its input to canonical form *before* the literal match —
  lexically collapse `.`/`..`/repeated slashes (see `normalize_path` in
  `fleet-guard-worktree-edit` for the hermetic no-filesystem form) and
  canonicalize platform spellings (`canonicalize_path_spelling` in
  `fleet-common.sh`). A raw `case`/prefix test on an unnormalized path is
  bypassable by construction (#2413: `..`-laden targets escaped the
  worktree guard; #2036: drive-letter spellings defeated a byte-compare).
  Ship the escape shapes (`..`-relative, `..`-embedded-absolute,
  allowlist-prefix rides like `/tmp/../…`) as hermetic test cases with
  the guard.
- **`git merge --ff-only` is not a dirty-tree guard.** It refuses only when
  the incoming commits *overlap* the dirty files; a **disjoint** dirty tree
  fast-forwards silently — WIP-loss-adjacent on a shared clone. Any path
  that advances or restores a shared clone's branch gates on an explicit
  `git status --porcelain` tracked-dirty check *before* the fetch/merge,
  and the guard covers **every** branch path, not just the off-master arm
  (#2378: the on-master arm fell through to an unguarded ff-advance).
  Generally: an unattended mutation of shared state gates on **liveness**, not
  recoverability — one fail-closed predicate on every mutating path (#2668).
- **Config-file generators preserve hand-edits under every emitted key.**
  A generator that wholesale-rewrites a config file (`fleet-up`'s
  `write_worktree_settings` → `settings.local.json`) must carry
  preservation logic — and a test — for **each** key it emits; adding a new
  generated key means extending the preservation in the same change, or the
  next regeneration silently clobbers human edits under that key (#2284:
  the first `hooks` key repeated the lesson `permissions.allow` already
  encoded).
- **Worktree scoping is assignment-derived, not cwd-derived (#2402).**
  `fleet-up` bakes `FLEET_ASSIGNED_WORKTREE` (the absolute worktree path) into
  each generated `settings.local.json` `env`; when set, `fleet-guard-worktree-edit`
  and `fleet-edit` allow a mutation only inside that worktree (engine **or** game,
  matched by basename), `$HOME/.fleet`, `/tmp`, `/private/tmp`, the native-Windows
  MSYS tmp (`C:\msys64\tmp`, the harness scratchpad), or the auto-memory
  dir — so a drifted cwd can't misroute an edit into the shared main clone. The
  guard normalizes native-Windows path spellings (`C:\…` / `C:/…`) to the MSYS
  `/c/…` form before testing — un-normalized they read as relative and deny
  every edit on a Windows host. Env
  unset ⇒ the legacy cwd-derived behavior (human / non-fleet sessions). The
  allowlist mirrors the settings' `additionalDirectories`; extend both together.
  Mutating git wrappers (`fleet-pr-amend-push`, `fleet-review-verdict --agent`)
  call `fleet-assert-worktree`; scout / ingest / claim / rebase legitimately run
  from the main clone and are deliberately NOT asserted.
- **A `continue` that withholds a worker-visible affordance — a stackable
  offer, a claim candidate, a queue row — must `log()` its reason.** The
  symptom lands a whole pipeline away from the cause (#2760: 3 silent arms).
  `log()` is the run's stdout trace, not a stderr warn — a skip that *persists*
  is the next bullet's escalate-then-quiet case.
- **An every-tick guard that warns must escalate-then-quiet.** A skip
  condition in an unattended loop (`advance_main_clone`, the dispatcher's
  per-tick guards) persists until a human acts, so a plain `echo … >&2`
  re-emits identically forever — spam that hides the outage instead of
  reporting it (#2363: a parked main clone froze every claim on both repos
  for 30 min behind one line repeated per minute). Count consecutive
  identical skips keyed by `<reason>|<subject>`; at the Nth emit one loud
  line plus a flat `${FLEET_ALERTS_DIR:-$HOME/.fleet/alerts}/<tool>-<tag>`
  file, then go silent until a healthy pass clears both. Size N against the
  outage you're catching, not a round number, and keep every counter/alert
  write best-effort (`|| true`) so a read-only `$HOME` can't break the path
  being guarded. **Keep rewriting the alert on every tick past N**, not just
  at N: once stderr goes quiet the file is the only standing signal, so a
  write-once alert freezes its `count=`/age at the escalation instant and —
  worse — lets a human triaging the alerts inbox silence a still-live
  condition permanently (nothing would ever recreate it). The reference
  implementation is `fleet-clone-freshness.sh`'s `_freshness_warn` +
  `_freshness_all_clear` pair — it is the one that actually counts, keys,
  escalates at N, and quiets, and `tests/test_clone_freshness.sh` T19–T22
  is the matching test shape (every-tick loop, N sized in ticks).
  `fleet-rebase`'s `escalate_if_hung_lock` + `hung_lock_all_clear` is the
  same cycle for a **one-shot** tool the dispatcher re-invokes: the streak
  counter lives on disk because there is no in-process loop to hold it, and
  N is 1 because an age ceiling — not a tick count — does the sizing there
  (#2363, #2795). Copy whichever matches your call cadence; don't invent a
  third counter block by hand.
- **Neither a skip nor a failed action may consume an edge-triggered lane's
  edge.** The scout's `queue-manager` reconcile and `queue-manager-ingest`
  lanes compare their own projection hash inline instead of routing through
  `update_role_trigger`, precisely because nothing re-arms them. The seen-hash
  write therefore belongs
  after every early-`continue` guard **and after the action itself succeeded**,
  never before either: recording the hash and then skipping — or recording it
  and then failing to `Popen` — discards the change permanently, since the next
  tick compares equal and skips too. A degraded tick swallowed an
  agent-approved issue's ingest exactly this way, and the freshly-stamped
  seen-hash mtime — the strongest available "this lane is healthy" signal —
  was the bug's own fingerprint (#2965). Periodic claim cleanup follows the
  same rule for its last-run marker: only a successful spawn consumes the
  deadline. Fixing only the guard half left the
  identical strand one line down, where both lanes swallowed a spawn failure
  with a bare `log` (#2972): the write must clear the *whole* fallible region,
  so "put it below the guards" is the special case, not the rule. Where a lane
  fires **several** commands per firing, state the partial-failure rule
  explicitly rather than implying it — the `queue-manager` lane is all-or-none
  (any failed spawn leaves the hash unwritten and re-runs the whole set next
  tick, safe because both sweeps are idempotent). Because the unwritten hash
  makes the lane retry every tick, a failure path here also needs the
  escalate-then-quiet pair below (`_spawn_failed` / `_spawn_ok`), or it becomes
  the per-tick log spam that rule forbids. Adding a guard — or a new fallible
  action — to a hash-self-managing lane means placing it above the write and
  covering it in `tests/test_scout_degraded_fetch.py`.
- **An ingest round-trip's candidate set is captured above the section
  filters, never derived from `tasks.open`.** The remove-half of a
  `fleet-queue-ingest` round-trip (`_ingest_unblock_candidates`,
  `_ingest_retract_candidates`) needs the population its predicate targets —
  and `fetch_task_queue` `continue`s on `fleet:plan-review`, `fleet:needs-human`
  and `fleet:gated` **before** the task dict is built, then splits what
  survives into `open` vs `in_progress` by claim state. So a candidate derived
  from `tasks.open` is silently blind to three label populations and to every
  claimed issue. #2740's own plan proposed exactly that and would have shipped
  covering one of its two gate arms; the fix captures into a flat
  `tasks.plan_gated` list inside the loop, above the `continue`s. This is the
  same reachability class as the defect such a round-trip usually exists to
  fix — the input-set builder filtering the target before the detector runs —
  so it is worth re-deriving from the raw issue list rather than reusing a
  section that already looks close enough. Pin the placement with a test that
  asserts the row is in the candidate list **and** absent from every section
  (`tests/test_scout_task_queue_plan_gated.py`); asserting only the former
  passes with the capture moved back below a `continue`.
- **Unattended daemons timeout-guard their network calls.** The host's
  connections to GitHub intermittently black-hole (silent TCP death), so a
  hung `git fetch` / `gh …` in a fleet daemon (dispatcher loop, `fleet-rebase`,
  `fleet-claim`) wedges the fleet indefinitely (#2362). `source
  fleet-net.sh` — it shadows `git()`/`gh()` with a `timeout` and bounds every
  current and future call site by construction — rather than adding per-site
  guards. Python fetchers use their own subprocess/urllib timeouts instead.
  The escalated form of the same failure is host-wide: leaked/hung
  connections exhaust the ephemeral port range and every network call dies
  instantly with EADDRNOTAVAIL ("Can't assign requested address") — that is
  not GitHub being down; run `fleet-net-doctor` (exit 2 ⇒ reboot the host).
- **Shell portability: GNU spelling first, no BSD-only flag forms.** Both arms
  of `$(a || b)` write the capture, so a failing first arm poisons it (GNU
  `stat -f` prints a filesystem block `2>/dev/null` can't catch, #2686). Prefer
  one portable spelling: `mktemp -d "${TMPDIR:-/tmp}/p.XXXXXX"`, not `-t p`.
- **A REST *list* fetch paginates or it silently truncates.** On a collection
  endpoint pass an explicit `per_page` **and** page it (`gh --paginate`;
  `_rest_list`'s `max_pages`): the 30-item default returns with no error, so a
  bare `per_page` reads as handled (#2856: `human_approved` was 4 of 12).
- **Native `jq` on Windows (MSYS2) emits CRLF, not LF.** `mapfile` only
  strips the trailing `\n` delimiter, so `mapfile -t arr < <(jq -r '...')`
  leaves an embedded `\r` on every element on that host — silently breaking
  any later string comparison against LF-clean data (`fleet-transition`'s
  label-validation loop misreported every edge's labels as unknown node
  names, #3029). Pipe through `tr -d '\r'` (or equivalent) at the `mapfile`
  source, not just at the first comparison site — the array is usually
  reused by more than one downstream consumer. Defensive, not
  host-conditional: a no-op on Linux/macOS, where `jq -r` already emits LF.
- **Native `python3` on Windows CRLF-terminates every `print()` to stdout,
  same as native `jq -r`.** A `while IFS=$'\t' read -r a b` loop consuming a
  `python3 -c '... print(f"{n}\t{name}")'` producer strips only the trailing
  `\n`, so the CR rides along on the *last* field of every line — on
  `cmd_cleanup_gh`'s label sweeps (and `cmd_reset_sweep_host_claims`, which
  shares the producer shape) that field is the label name, and it feeds
  both a same-host liveness-marker path lookup (`$CLAIMS_DIR/_prlabel-<tag>-
  <agent>\r` never matches the real marker file, so a live claim reads as a
  confirmed orphan and gets swept) and the final `gh issue edit --remove-label
  "$label_name"` call (#3060: `cmd_cleanup_gh`'s planning-sweep pass, #2711's
  marker vouching). Pipe the producer through `tr -d '\r'` before the value is
  captured — at the `python3 -c` invocation itself, not the first comparison
  site, the same discipline as the `jq -r` gotcha — since a plan/label-name
  string is usually reused by more than one downstream consumer. Defensive,
  not host-conditional: a no-op on Linux/macOS, where `python3` never emits
  CRLF regardless of stream type. Guard the fix with a **byte-level** check,
  never grep: GNU grep on MSYS2 strips CRs from text input before matching,
  and `$(...)` trims a trailing CR along with the trailing newline, so a grep
  assert — or a one-line fixture — reads clean on the very host that has the
  bug (`tests/test_fleet_claim_parked_release.sh` Phase 2d is the reference
  shape: two-line fixture, assert on the first line, binary read).
