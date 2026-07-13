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
- **Unattended daemons timeout-guard their network calls.** The host's
  connections to GitHub intermittently black-hole (silent TCP death), so a
  hung `git fetch` / `gh …` in a fleet daemon (dispatcher loop, `fleet-rebase`,
  `fleet-claim`) wedges the fleet indefinitely (#2362). `source
  fleet-net.sh` — it shadows `git()`/`gh()` with a `timeout` and bounds every
  current and future call site by construction — rather than adding per-site
  guards. Python fetchers use their own subprocess/urllib timeouts instead.
