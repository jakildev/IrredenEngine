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
- **Dual-spelling options validate both arms identically.** A wrapper that
  accepts `--opt val` and `--opt=val` has two independent case arms: reject
  an empty `--opt=` exactly as the space form rejects a missing value. A
  diverging equals arm lets `--opt=$UNSET_VAR` slip an empty string past
  downstream `[[ -n "$var" ]]` guards (#2193: bypassed a claim check).
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
