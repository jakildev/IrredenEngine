# Plan: scripts/fleet: best-effort lint flag for st_mtime reads near state.json consumers

- **Issue:** #2267
- **Model:** sonnet — every design decision (co-occurrence heuristic, inline-suppression ratchet, exit policy, CI placement) is pinned below; implementation is bounded mechanical work (one ~70-line stdlib script + one test + one CI step + two one-line comment seeds).
- **Date:** 2026-07-08

### Scope

Add a best-effort, standalone Python lint that flags a `.st_mtime` **freshness** read in any `scripts/fleet/` file that *also* consumes the scout cache (`state.json` / `STATE_JSON` / `STATE_FILE`), wired into the same CI lint job as `ruff check scripts/`. Cache freshness must derive from the in-file `generated_at` (canonical helper `fleet_poll_topology.state_age_seconds`, contract in `docs/agents/FLEET-RUNTIME.md`); mtime lies when a #1394-Q2 follower rewrites `state.json` every tick while preserving a dead leader's `generated_at`. The mtime shape was introduced twice independently (`fleet-dispatcher:_state_age_seconds`, `fleet-epic-status:load_state_cache`, PR #2231) with no gate to catch the third. This adds that gate. Migrating the two existing readers to `generated_at` is **out of scope** (separate work); this task only lands the lint and grandfathers them.

### Verified current state

- CI lint lives in `.github/workflows/quality.yml` → step **"Python lint (ruff)"** (`astral-sh/ruff-action@v3`, `check scripts/`) in the `quality-checks` job, which runs on **`windows-latest`**. `ruff.toml` pins the rule set; ruff has no native cross-line rule for "`.st_mtime` read AND `state.json` reference in the same file", which is why a separate grep-style check is needed (confirmed against the issue's own note).
- The canonical helper already exists: `scripts/fleet/fleet_poll_topology.py:222` `state_age_seconds(generated_at, mtime, now=None)` — prefers `generated_at`, falls back to `mtime` only when `generated_at` is missing/unparseable. It receives `mtime` as a **parameter**; it does **not** itself read `.st_mtime`, and `grep -n st_mtime scripts/fleet/fleet_poll_topology.py` is empty — so the helper is correctly NOT a target.
- `grep -rn "\.st_mtime" scripts/fleet/` returns exactly five reads. Classifying each against the "file also references `state.json`" discriminator:
  - `fleet-dispatcher:1685` (`_state_age_seconds`, `STATE_FILE.stat().st_mtime`) — **offender** (file defines `STATE_FILE = STATE/"state.json"`).
  - `fleet-epic-status:108` (`load_state_cache`, `p.stat().st_mtime` on `STATE_JSON`) — **offender** (file defines `STATE_JSON = .../state.json`).
  - `fleet-debug:105` (strftime display of a trigger file's mtime) — legitimate; verified `fleet-debug` does **not** reference `state.json` → not flagged.
  - `fleet-pr:124` (sort key by mtime) — legitimate; verified `fleet-pr` does **not** reference `state.json` → not flagged.
  - `tests/test_fleet_debug_triggers.py` (`st_mtime_ns` snapshot) — under `tests/`, excluded from scan scope.
- So the file-level co-occurrence heuristic is precise on the current tree: it flags exactly the two real offenders and nothing else.

### Approach (one approach, picked)

1. **New lint script `scripts/fleet/lint_state_mtime.py`** — stdlib-only, pure-Python (no `grep`/shell, so it runs unchanged on the Windows CI runner). Behavior:
   - Accept one-or-more root paths on argv (default `scripts/fleet/`). Walk `*.py` **and** the extension-less Python executables; skip `tests/`, `__pycache__/`, and the lint script itself.
   - For each file, read text once. Flag the file iff it contains **both** (a) a `.st_mtime` attribute read — regex `\.st_mtime(_ns)?\b` (dot-anchored, so the helper's bare `mtime` parameter never matches) — **and** (b) a state-cache token — `state.json` / `STATE_JSON` / `STATE_FILE`.
   - For each flagged `.st_mtime` line **not carrying the inline suppression**, print `path:line: state.json freshness must use fleet_poll_topology.state_age_seconds / generated_at (FLEET-RUNTIME.md); mtime lies under Q2 follower rewrite. Suppress with '# lint: state-mtime-ok <reason>' if this read is genuinely non-staleness.`
   - **Inline suppression escape hatch (the "not a hard ban" mechanism):** a `# lint: state-mtime-ok <reason>` trailing comment on (or immediately above) the `.st_mtime` line suppresses that line. Diff-local and self-documenting, so it doesn't drift like a central line-number allowlist would.
   - **Exit policy (the one interpretive call I'm making for the human):** exit **non-zero** when any *unsuppressed* finding remains; exit 0 otherwise. Rationale: a warn-only exit-0 step is green-CI-ignorable, and the whole motivation (#2231 introduced the shape twice *unnoticed*) is that silent surfacing already failed. The suppression-with-reason hatch keeps it from being a "hard ban" on `st_mtime` — a legit use is one justified comment away, which a reviewer sees in the diff. If plan-review prefers pure warn-only, it is a one-line change (drop the non-zero exit, keep the prints) — but I recommend the ratchet.
2. **Seed the two known offenders** with `# lint: state-mtime-ok pending generated_at migration (#2231/#2267)` so the tree is green on landing and both are documented as known-pending: `scripts/fleet/fleet-dispatcher:1685` and `scripts/fleet/fleet-epic-status:108`.
3. **Wire CI:** add a step to `.github/workflows/quality.yml` in the `quality-checks` job, immediately after "Python lint (ruff)":
   ```yaml
   - name: State-mtime freshness lint (best-effort ratchet)
     run: python scripts/fleet/lint_state_mtime.py scripts/fleet/
   ```
4. **Test `scripts/fleet/tests/test_lint_state_mtime.py`** (pytest-style, matching `test_blocked_by.py`): write tmp fixtures and assert the exit code + findings — (a) positive: `.st_mtime` + `state.json`, no suppression → flagged, non-zero; (b) negative-display: `.st_mtime` for strftime, no state token → clean; (c) negative-suppressed: offender + `# lint: state-mtime-ok …` → clean; (d) no-state-token file with mtime sort → clean. Register it the same way the sibling `.py` tests are picked up by the ctest/pytest harness (mirror `test_blocked_by.py`'s CMake/ctest registration).
5. **Author-side pointer (minor):** add one line to the ruff pre-commit note (`scripts/bootstrap_linux.sh` / `scripts/bootstrap_macos.sh`, wherever the `ruff check scripts/fleet/` local reminder is) pointing at `python scripts/fleet/lint_state_mtime.py scripts/fleet/` so it runs locally too. The doc/review-checklist half of this already landed via #2235; this is just the runnable pointer.

### Affected files

- `scripts/fleet/lint_state_mtime.py` — **new**, the lint (~70 lines).
- `scripts/fleet/tests/test_lint_state_mtime.py` — **new**, fixtures + assertions.
- `.github/workflows/quality.yml` — **+1 step** after the ruff step.
- `scripts/fleet/fleet-dispatcher` — **+comment** at the `_state_age_seconds` mtime read (line ~1685).
- `scripts/fleet/fleet-epic-status` — **+comment** at the `load_state_cache` mtime read (line ~108).
- `scripts/bootstrap_linux.sh` / `scripts/bootstrap_macos.sh` — **+1 line** pointer (optional, low-priority).
- Test registration file (CMake/ctest list) — mirror `test_blocked_by.py`'s entry.

### Acceptance criteria

- `python scripts/fleet/lint_state_mtime.py scripts/fleet/` exits **0** on the current tree (both offenders suppressed) and **non-zero** when a new unsuppressed `.st_mtime`+state.json file is added.
- The new test passes under the existing test harness (`ctest`/pytest), covering positive, display-negative, suppressed-negative, and mtime-sort-negative cases.
- The new CI step runs in `quality-checks` and is green; the ruff step still passes over the new `.py` (it auto-enters `ruff check scripts/` scope because it has a `.py` extension — keep it ruff-clean; **no `ruff.toml` edit needed**).
- Both existing instances carry the justification comment.

### Gotchas

- **Windows CI runner** — keep the lint pure-Python (`pathlib`, no `grep`/shell); emit `file:line` via `Path.as_posix()` for stable, OS-independent refs.
- **Don't broaden the regex to bare `mtime`** — it would false-positive `fleet_poll_topology.state_age_seconds`'s legitimate `mtime` *parameter*. Anchor on `.st_mtime`.
- **Scope stays `scripts/fleet/`** (per the issue), not all of `scripts/` — the render/perf/gui harnesses sort/format by mtime and don't touch `state.json`; narrowing avoids even considering them.
- **New `.py` auto-lints under ruff** — line-length 100, import order, or the ruff step fails. No `ruff.toml` change (it's a `.py`, not an extension-less executable that would need explicit enumeration).
- **Wire the real CI file** — `.github/workflows/quality.yml`, not the stray `.githooks/quality.yml`.
- **Not gated** — `scripts/fleet/` and `.github/workflows/` are outside the `.claude/**` self-config commit gate, so a worker can implement and push this normally (no `fleet:gated` risk).

### One task or subtasks

**One task** — ~5 small files, single PR. No stack, no shared-resource migration (additive: a lint + two documenting comments), so no cross-system audit section is required.

