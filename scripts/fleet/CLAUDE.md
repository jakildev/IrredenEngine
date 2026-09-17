# scripts/fleet/ — fleet tooling

Bash + Python tooling for the autonomous fleet (scout, dispatcher, claim,
install, per-tool wrappers) and its tests under `tests/`. Style and the
comment policy: [`CLAUDE-BASELINE.md`](../../docs/agents/CLAUDE-BASELINE.md)
§Style, [`.claude/rules/comments.md`](../../.claude/rules/comments.md).
Rationale for the contracts below that is not readable from the code:
[`docs/design/fleet-tooling-contracts.md`](../../docs/design/fleet-tooling-contracts.md).

## Commands

| Check | Run |
|---|---|
| Whole fleet suite | `bash scripts/fleet/tests/run_all.sh [--only <substring>]` |
| Positive control for a new suite | `fleet-positive-control <test-file> <pre-fix-ref>` |
| Python lint | `ruff check scripts/` |
| `state.json` mtime ratchet | `python3 scripts/fleet/lint_state_mtime.py` |

Suites are glob-discovered (no registration) and run only by
`fleet-tests.yml`, path-filtered to `scripts/**` plus its listed out-of-tree
subjects; `ctest` never sees them. Validator index: [`VALIDATION.md`](../../docs/agents/VALIDATION.md).

## Test conventions

- **Hermetic: no live GitHub, no live `~/.fleet`.** Mock every network fetcher
  at a seam that fails closed (a mock miss raises, never falls through to
  `urllib`/`gh`); inject a `tempfile.TemporaryDirectory()` `cache_dir` rather
  than sharing `fleet_gh_poll.DEFAULT_CACHE_DIR`; when a function changes
  transport or gains its first network call, re-point every suite covering it
  in the same PR; prefer obviously synthetic fixtures to plausible real IDs.
- **A CLI stub models the tool's argument parsing, not just its endpoint.**
  Transcribe the accepted flag set from the real tool's `--help` and fail the
  way it fails; a stub that emulates `--jq` evaluates the program against
  fixture JSON and fails closed on a shape it does not model. Pair the stub
  with a fidelity assertion that it still rejects the flag a past bug used.
- **Exercise every arm.** `--opt val` and `--opt=val` are two case arms:
  reject an empty `--opt=` exactly as the space form rejects a missing value.
  A `${VAR:-${FALLBACK:-literal}}` chain has a fallback arm a fixture that
  always pins the primary input never reaches — unset it in at least one case.
- **A wall-clock-derived arm gets an injected clock, not a wider window.**
  Give the subject a `now` seam (`fleet-health` reads `FLEET_HEALTH_NOW`), pin
  it from the fixture's own timestamps, and pair the arm with one whose window
  excludes the fixture, asserting the reported boundary.
- **A new executable ships with `tests/test_<name>.{sh,py}` in the same PR.**
- **A missing subject under test exits 3**, the skip status `run_all.sh`
  tallies as "skipped", with a `SKIP:` stderr prefix reserved for that case;
  an environment-dependency skip may stay `exit 0`. A subject outside
  `scripts/**` is added to `fleet-tests.yml`'s `paths:` **and** to
  `OUT_OF_TREE_SUBJECTS` in `tests/test_fleet_tests_workflow_paths.sh`, which
  asserts both hand-duplicated `paths:` blocks carry it. The list is an
  inclusion list: `python3 scripts/fleet/fleet_test_subjects.py` (T5 there,
  T6 in `tests/test_workflow_paths_sync.sh`) derives the population from the
  suite sources and the both-blocks workflow glob and fails naming any member
  the list or the `paths:` blocks omit — run it, don't hand-audit the list.
- **Bash suites source `tests/lib_assert.sh`** (`ok`/`bad`, `assert_eq`,
  `assert_contains`, `assert_absent`, PASS/FAIL counters) and **end with
  `summarize`** — never a private tally line or a local `summarize()`.
  `tests/test_suite_tally_forms.sh` ratchets this; its `OWN_TALLY_BASELINE`
  is shrink-only.
- **Positive-control a new suite with `fleet-positive-control`, never by
  hand.** It stages the whole directory (a partial stage trips every wrapper's
  lib-dir preflight and prints a plausible wrong tally), dispatches `.sh` and
  `.py` as `run_all.sh` does, and reads each suite's summary line through the
  `--parse-tally` grammar — the single executor shared with
  `tests/test_positive_control.sh` and `tests/test_suite_tally_forms.sh`.
  Report coverage beside any before/after drift count. An exclusion assertion
  is outside its reach: prove it by deleting the exclusion in the current
  implementation and watching the assertion go red ([`CLAUDE-BASELINE.md`](../../docs/agents/CLAUDE-BASELINE.md)
  §"Encode contracts in code, not in comments").
- **Hand-run controls source `tests/lib_preflight.sh`** on the line after
  `SCRIPT_DIR`, above the first path built to a `fleet-*` wrapper;
  `test_positive_control.sh`'s adoption ratchet checks the placement.
- **`tests/test_*.sh` commits its executable bit** (`git update-index
  --chmod=+x`); `test_suites_are_executable.sh` guards it. `test_*.py` stays
  `100644` and runs as `python3 "$f"`.

## Bash pitfalls

- **Empty and paired arrays.** Pre-check `(( ${#arr[@]} > 0 ))` before
  expanding a possibly-empty `"${arr[@]}"` under `set -u`, or raise the
  script's declared bash floor to ≥ 4.4. A helper that reconstructs two
  logical arrays from one flattened argument list asserts length parity at
  the definition site and inside the callee.
- **GNU spelling first, no BSD-only flag forms.** Both arms of `$(a || b)`
  write the capture, so a failing first arm poisons it. Prefer one portable
  spelling: `mktemp -d "${TMPDIR:-/tmp}/p.XXXXXX"`, not `-t p`.
- **Native Windows `jq -r` and `python3 print()` emit CRLF.** `mapfile -t`
  and `read -r` strip only `\n`, so the CR rides the last field into every
  later comparison, path lookup, and `gh … --remove-label` call. Pipe the
  producer through `tr -d '\r'` at the producer, not the first comparison
  site (no-op on Linux/macOS). Guard the fix with a byte-level check, never
  grep or `$(...)` — both strip the CR on the host that has the bug
  (`tests/test_fleet_claim_parked_release.sh` Phase 2d is the shape).
- **`--help` and docstrings track the code.** A `--help` that slices its own
  header derives the end from the first non-`#` line or ships a regression
  test; a diff that adds or removes an enumerated pass/subcommand/sweep
  updates every count and enumeration in the same change, including
  operator-facing siblings (`fleet-up.conf.sample`, `fleet-help`'s index,
  printed column labels), both ways: retired name gone, replacement present.
- **`git merge --ff-only` is not a dirty-tree guard** — a disjoint dirty tree
  fast-forwards silently. Every path that advances or restores a shared
  clone's branch gates on `git status --porcelain` (tracked-dirty) before the
  fetch/merge, on every branch arm. Generally, an unattended mutation of
  shared state gates on liveness, not recoverability: one fail-closed
  predicate on every mutating path.
- **Path-containment checks normalize before the literal match.** Collapse
  `.`/`..`/repeated slashes (`normalize_path` in `fleet-guard-worktree-edit`)
  and canonicalize platform spellings (`canonicalize_path_spelling` in
  `fleet-common.sh`) first; ship the escape shapes (`..`-relative,
  `..`-embedded-absolute, allowlist-prefix rides like `/tmp/../…`) as
  hermetic cases with the guard.
- **Worktree scoping is assignment-derived, not cwd-derived.** `fleet-up` bakes
  `FLEET_ASSIGNED_WORKTREE` into each generated `settings.local.json`; when
  set, `fleet-guard-worktree-edit` and `fleet-edit` allow a mutation only
  inside that worktree (engine or game, by basename) or an allowlist mirroring
  the settings' `additionalDirectories` (extend both together); unset means
  legacy cwd-derived behavior. Mutating git wrappers call
  `fleet-assert-worktree`; scout, ingest, claim and rebase run unasserted from
  the main clone by design.
- **Config-file generators preserve hand-edits under every emitted key**
  (`fleet-up`'s `write_worktree_settings`); a new key extends the
  preservation logic and its test in the same change.
- **`fleet-up`'s bootstrap heredoc cannot import.** It is a standalone
  `python3 - <<'PY'` under `|| true`, so an `ImportError` silently disables
  the bootstrap trigger for every role. Inline the constant or predicate,
  name the canonical copy in a `keep in sync` comment, and ship a drift guard
  (`test_smoke_worker_projection.py`'s `TwoCopiesAgree` is the shape). Bash
  reaches `fleet_task_class.py` and `fleet_completion.py` through CLI arms,
  never an import (the dispatcher fetches, the module decides).

## Shared-state contracts

- **`state.json` staleness comes from the in-file `generated_at`, never file
  mtime** ([`FLEET-RUNTIME.md`](../../docs/agents/FLEET-RUNTIME.md)); use
  `fleet_poll_topology.state_age_seconds`, or an inline mirror commented as
  such where a heredoc cannot import. `lint_state_mtime.py` ratchets new
  `.st_mtime` reads beside a `state.json` reference; opt a justified read
  out with `# lint: state-mtime-ok <reason>`.
- **`state.json` has a hard size ceiling** ([`FLEET-CACHE.md`](../../docs/agents/FLEET-CACHE.md)
  §"Size invariant"): the scout emits it compact and keeps a review body only
  on the latest review per PR — do not tidy either back. A change to the
  per-PR record shape bumps `PR_RECORD_SCHEMA` in the same commit, or the 304
  fast path keeps serving the old shape from disk.
- **Concurrently-read writers use `write_atomic`**, never `Path.write_text`.
- **A REST list fetch passes an explicit `per_page` and pages**
  (`gh --paginate`; `_rest_list`'s `max_pages`); the 30-item default
  truncates without error.
- **Unattended daemons timeout-guard every network call**: `source
  fleet-net.sh` shadows `git()`/`gh()` with a `timeout`, bounding current and
  future call sites by construction; Python fetchers carry their own
  subprocess/urllib timeouts. Instant `EADDRNOTAVAIL` on every call is port
  exhaustion on the host, not GitHub — run `fleet-net-doctor` (exit 2: reboot).

## Lane and loop contracts

- **A new consumer of a PR label excludes PRs claimable by other lanes.**
  Disjoint claim-label namespaces (`fleet:amending-*`, `fleet:resolving-*`,
  `fleet:reviewing-*`) give no mutual exclusion on their own. Every
  force-pushing claim lane (`cmd_amending_claim`, `cmd_resolving_claim`)
  routes through the shared exclusion table with arbitration on the POST
  response over the lane union; the table is symmetric, and a guard added for
  one lane pair is enumerated against every other pair. The merger takes no
  `fleet-claim` lock by design (`--force-with-lease`, `NO_CLAIM_FABRIC_ROLES`
  in `fleet-dispatcher`). A lane's admission filter lives in one place —
  `project_<role>` and `slice_<role>` share it (`worker_feedback_labels()`).
  Pin both with the source-derived `tests/test_claim_namespace_matrix.py`.
- **A `continue` that withholds a worker-visible affordance `log()`s its
  reason**; a skip that persists is the every-tick case below.
- **Neither a skip nor a failed action consumes an edge-triggered lane's
  edge.** In the scout's hash-self-managing lanes (`queue-manager`,
  `queue-manager-ingest`, periodic claim cleanup) the seen-hash or
  last-run write sits after every early-`continue` **and** after the action
  succeeded, clearing the whole fallible region; a multi-command lane states
  its partial-failure rule (`queue-manager` is all-or-none), and its retry
  path carries the escalate-then-quiet pair (`_spawn_failed` / `_spawn_ok`).
  A new guard or action goes above the write and into
  `tests/test_scout_degraded_fetch.py`.
- **An ingest round-trip captures its candidate set above the section
  filters.** `fetch_task_queue` drops `fleet:plan-review`, `fleet:needs-human`
  and `fleet:gated` before building the task dict and splits the rest by
  claim state, so a candidate derived from `tasks.open` is blind to all of
  them; capture into `tasks.plan_gated` inside the loop, above the
  `continue`s, and test the row present in the candidate list **and** absent
  from every section (`tests/test_scout_task_queue_plan_gated.py`).
- **A pane-keyed signal is not an iteration-keyed one.** Per-iteration claim
  liveness compares dispatch ids, never a file a later role in the same pane
  refreshes (heartbeat, `FLEET_CLAIM_FLAG`); missing identity = "cannot
  vouch", not "orphan". The verdict is not a mutex: claim and sweep serialize
  on the owned, fenced `amend-snapshots/<pr>.lock` (`fleet-claim`).

### An every-tick guard that warns must escalate-then-quiet

Count consecutive identical skips keyed `<reason>|<subject>`; at N emit one
loud line and a `${FLEET_ALERTS_DIR:-$HOME/.fleet/alerts}/<tool>-<tag>` file,
rewritten on every later tick, then stay silent until a healthy pass clears
both. Size N against the outage, keep every counter/alert write `|| true`.
Copy `fleet-clone-freshness.sh`'s `_freshness_warn` / `_freshness_all_clear`
for an in-process loop, `fleet-rebase`'s `escalate_if_hung_lock` /
`hung_lock_all_clear` for a re-invoked one-shot; do not write a third.
