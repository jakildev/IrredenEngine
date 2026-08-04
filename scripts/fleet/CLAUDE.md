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
- **A new executable ships with a `tests/test_<name>.{sh,py}`** in the same
  PR — the fleet-tooling form of the review checklist's "new feature with no
  new test"; the `simplify` pre-commit pass flags the omission. Run the whole
  set with `bash scripts/fleet/tests/run_all.sh` (`--only <substring>` while
  iterating); discovery is by glob, so a new suite needs no registration.
  These are not CMake tests — `ctest` does not cover them, so the dedicated
  `fleet-tests.yml` workflow calls the runner directly on every push and PR
  that touches `scripts/fleet/**`. That workflow is the only thing gating
  them; an unexecuted suite goes red silently (see #2712).
- **A suite whose subject under test is missing must not report success.**
  A guard shaped `if [[ ! -f "$SUBJECT" ]]; then echo "SKIP: ..."; exit 0;
  fi` reports the same exit status as a real pass, so `run_all.sh` folds a
  suite that verified nothing into its "N passed" tally — the vacuous-pass
  failure mode #2712 wired execution to prevent, one level down (#2786).
  Exit **3** instead — the shared skip status `run_all.sh` recognizes and
  tallies separately as "skipped", never "passed". Reserve the `SKIP:`
  stderr prefix for this case (an environment-dependency skip like "git not
  available" can stay `exit 0`; only a missing *subject* is a #2786 case).
  If the subject lives outside `scripts/fleet/**`, also add its path to
  `fleet-tests.yml`'s `paths:` filter — otherwise a PR that moves or edits
  only that file never runs the suite that would have caught the break.
- **Bash tests source `tests/lib_assert.sh`** for the PASS/FAIL counters,
  `ok`/`bad`, `assert_eq`/`assert_contains`/`assert_absent`, and the
  `summarize` exit idiom — don't re-copy the helpers into a new test.
  Genuinely test-specific asserts (path existence, exit codes) stay local,
  built on `ok`/`bad`.
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
  VACUOUS, and emits the test-plan line with its arithmetic shown.
  `require_fleet_lib_dir` in `lib_assert.sh` is the backstop for controls still
  run by hand — it fires automatically for suites that set `SCRIPT_DIR` before
  sourcing.
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
- **A new consumer of a PR label excludes PRs claimable by other lanes
  with disjoint claim namespaces.** Disjoint claim-label namespaces
  (`fleet:amending-*` vs `fleet:resolving-*`) provide **no** mutual
  exclusion — a PR matching two lanes' filters on one scout tick gets two
  panes force-pushing the same head branch, last writer wins (#2423: the
  semantic-conflict lane had to learn to exclude feedback-owing PRs).
  Check this whenever a lane is added or its label set widened.
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
