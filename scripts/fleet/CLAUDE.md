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
  new test"; the `simplify` pre-commit pass flags the omission.
- **Bash tests source `tests/lib_assert.sh`** for the PASS/FAIL counters,
  `ok`/`bad`, `assert_eq`/`assert_contains`/`assert_absent`, and the
  `summarize` exit idiom — don't re-copy the helpers into a new test.
  Genuinely test-specific asserts (path existence, exit codes) stay local,
  built on `ok`/`bad`.
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
  condition permanently (nothing would ever recreate it). `fleet-rebase`'s
  `escalate_if_hung_lock` is the reference shape (#2363).
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
