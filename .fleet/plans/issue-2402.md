## Plan: fleet: mechanically enforce worktree-scoped edits/cwd — doc rules keep failing (fix-011 recurring)

- **Issue:** #2402
- **Model:** opus — careful bash/python hook + generator work with fail-open/fail-closed subtleties and preservation tests; the approach below is fully committed, no novel design left
- **Date:** 2026-07-14

### Verified current state (audit — issue item 1)

Coverage map of today's enforcement, verified against the scripts on `origin/master`:

| Surface | Mechanism | Verified gap |
|---|---|---|
| Edit/Write/MultiEdit, engine-launched sessions | `fleet-guard-worktree-edit` PreToolUse hook, wired via the checked-in engine `.claude/settings.json` (`$CLAUDE_PROJECT_DIR/scripts/fleet/…`) | Derives the worktree from the **dynamic session cwd**. Two fail-open holes: (a) cwd drifts to the main clone → `case */.claude/worktrees/*` doesn't match → **no restriction at all** (the worker-1 2026-07-07 Edit-misroute mechanism); (b) cwd in any *other* agent's worktree → guard re-derives to that worktree and allows writes there. |
| Edit/Write/MultiEdit, game-repo-launched sessions | **nothing** | Game repo's checked-in `.claude/settings.json` has no `hooks` key and the game repo has no `scripts/fleet/`; meanwhile the generated game-worktree `settings.local.json` grants `additionalDirectories` access to the engine main clone. Game sessions can write anywhere in either clone unguarded. |
| Legitimate agent state writes (`~/.fleet/*`, auto-memory dir) | denied by the same guard (outside worktree root) | Over-blocking trains the documented Bash-heredoc workaround — i.e. the guard teaches agents the bypass channel. |
| Bash git mutations | `fleet-assert-worktree`, called by `fleet-pr-checkout-detached` (script-level) and by the `commit-and-push` / `start-next-task` skills (doc-level, per PR #1332) | `fleet-pr-amend-push` (push runs from cwd repo; only the amend-ref sentinel protects it — a stale sentinel in a main clone would push from there), `fleet-review-verdict` `--agent` path (the #2153/#2326 wrong-tree verdict/body flow), and `fleet-edit` (a file-writing CLI that bypasses the PreToolUse hook entirely) have no assert. |
| Bash cd drift in reviewer resets | PR #2381 (merged): assert + `git -C` discipline in reviewer scratch flows | Complementary; not touched here. |

Premise of the issue confirmed: enforcement exists but is cwd-derived and engine-only, so exactly the drifted-cwd and game-side cases recur (fix-011: 5 occurrences since PR #1332).

### Scope

Make worktree scoping **assignment-derived instead of cwd-derived**, extend it to game-repo sessions, stop over-blocking the legitimate agent-state write paths, and add `fleet-assert-worktree` to the three mutating wrappers that lack it. Explicit non-goal: no PreToolUse hook on Bash (parsing arbitrary shell for paths over-blocks and risks wedging the fleet); raw-Bash drift stays covered at the wrapper boundaries plus the merged PR #2381 discipline.

### Approach

1. **`scripts/fleet/fleet-up` — `write_worktree_settings()` emits the assignment.** For every generated worktree `settings.local.json`, add:
   - `"env": {"FLEET_ASSIGNED_WORKTREE": "<abs worktree path>"}` — the authoritative identity signal, available to hooks and every Bash call in sessions launched from that worktree.
   - a PreToolUse hook group (`Edit|Write|MultiEdit`) invoking the **engine clone's** copy of the guard by absolute path baked at generation time (`$ENGINE/scripts/fleet/fleet-guard-worktree-edit`), wrapped in an `[ -x … ] && … || true` so a missing/renamed script fails open (never wedge the fleet; also avoids `~/bin` staleness — #2262 is in flight on that surface). Engine-launched sessions will double-fire (project-settings hook + this one); that is accepted — two identical verdicts, deny still wins, one extra subprocess per edit.
   - Per the `scripts/fleet/CLAUDE.md` §Authoring-rules preservation contract (#2284): extend the fleet-owned-hook marker match (currently `fleet-session-track` only) to also recognize `fleet-guard-worktree-edit` so regeneration replaces stale variants rather than duplicating, preserve hand-added keys under the new `env` key (merge, fleet's key wins only for `FLEET_ASSIGNED_WORKTREE`), and extend `tests/test_worktree_settings_hooks.sh` for both new keys in the same change.
   - `game-architect` / game `epic-steward` calls pass `repo_root=$GAME`, which has no `scripts/fleet` — the hook path must come from `$ENGINE` (global in `fleet-up`), not from `repo_root`.

2. **`scripts/fleet/fleet-guard-worktree-edit` — dual-mode rework.**
   - **Assignment mode (`FLEET_ASSIGNED_WORKTREE` set):** ignore cwd entirely. Allow a mutation iff the target path is inside: (a) any `*/.claude/worktrees/<basename>/` worktree whose basename equals the assigned one — this covers the engine+game worktree pair of the same pane with one rule while still excluding sibling agents (keep the existing trailing-slash prefix idiom); (b) `~/.fleet/`; (c) `/tmp/` and `/private/tmp/` (macOS canonical form); (d) the auto-memory dir (`~/.claude/projects/*/memory/`). Everything else — main clones included, **regardless of where cwd drifted** — is denied. Relative paths: resolve against the hook-input cwd first, then apply the same test (closes the drifted-cwd + relative-path hole).
   - **Legacy mode (env unset):** current cwd-derived behavior, byte-for-byte semantics — human sessions in the main clone and hand-made worktrees (e.g. experiment worktrees) stay unaffected.
   - Keep the existing failure discipline: parse/jq failure fails OPEN; a policy deny uses the `hookSpecificOutput` deny form with the corrective message (extend it to name `FLEET_ASSIGNED_WORKTREE` and the allowlist).
   - New hermetic `tests/test_guard_worktree_edit.sh` (none exists today) driving the script with synthetic hook JSON on stdin: the deny/allow matrix above, both modes, plus fail-open on malformed input.

3. **Wrapper asserts (issue item 2).**
   - `fleet-pr-amend-push`: `fleet-assert-worktree` (no-arg form) before the push — closes the stale-sentinel-in-main-clone case.
   - `fleet-review-verdict`: on the `--agent` path only, assert with the agent name as expected basename (`fleet-assert-worktree "$agent"`) before applying the verdict; the no-`--agent` human carve-out keeps working from anywhere, mirroring the wrapper's existing dual-use design.
   - `fleet-edit`: when `FLEET_ASSIGNED_WORKTREE` is set, refuse a target file outside the same allowlist as the hook (same-basename worktrees + `~/.fleet` + tmp dirs); unset → unchanged. This closes the "Edit-tool-equivalent CLI" bypass.
   - Explicitly NOT asserted: `fleet-rebase`, `fleet-state-scout`, `fleet-queue-ingest`, `fleet-claim`, `fleet-clone-freshness.sh` — these legitimately run from the main clone (merger/scout/dispatcher lanes).
   - Optional belt, warn-only: `fleet-build` prints a one-line warning when its cwd resolves to a main clone (the "fix had no effect" precursor); never fails — humans build main clones deliberately.

4. **Docs (non-gated only).** Update the guard's header comment and `scripts/fleet/CLAUDE.md` (a one-bullet note that worktree scoping is assignment-derived via `FLEET_ASSIGNED_WORKTREE`). Do NOT touch role docs or SKILL.md files (gated self-config; #1332 already put the doc-level rules there — this ticket is the enforcement side precisely because those keep failing).

### Affected files

- `scripts/fleet/fleet-up` — `write_worktree_settings()`: `env` key, guard hook group, marker + preservation extensions
- `scripts/fleet/fleet-guard-worktree-edit` — dual-mode rework
- `scripts/fleet/fleet-pr-amend-push` — assert before push
- `scripts/fleet/fleet-review-verdict` — assert on the `--agent` path
- `scripts/fleet/fleet-edit` — allowlist check on target path
- `scripts/fleet/fleet-build` — warn-only main-clone cwd notice (optional belt)
- `scripts/fleet/tests/test_worktree_settings_hooks.sh` — extend for `env` + guard-hook emission/preservation
- `scripts/fleet/tests/test_guard_worktree_edit.sh` — new
- `scripts/fleet/tests/test_fleet_edit_scope.sh` (or extend an existing fleet-edit test if one exists) — new coverage for the `fleet-edit` allowlist
- `scripts/fleet/CLAUDE.md` — one authoring-rule note

### Acceptance criteria

- Hermetic tests (source `tests/lib_assert.sh`; no live GitHub, no live `~/.fleet`) pass for the full guard matrix: with `FLEET_ASSIGNED_WORKTREE` set — main-clone absolute write denied **even when hook-input cwd is the main clone**; sibling-agent worktree denied; same-basename game worktree allowed; `~/.fleet`, `/tmp`, `/private/tmp`, memory-dir writes allowed; relative path resolved-then-tested; malformed JSON fails open. With it unset — current behavior unchanged.
- `test_worktree_settings_hooks.sh` proves: both new keys emitted; a stale fleet guard-hook variant is replaced, not duplicated; hand-added hooks and hand-added `env` entries survive regeneration.
- `fleet-pr-amend-push` and `fleet-review-verdict --agent …` exit non-zero with the assert message when run from a main-clone cwd, and work unchanged from a worktree.
- `fleet-edit` refuses a main-clone target under assignment mode; unchanged otherwise.
- `ruff check scripts/` clean; all existing `scripts/fleet/tests/` still pass.
- Grep-verifiable: no role-doc / SKILL.md / `.claude/commands` diffs in the PR.

### Gotchas

- **Fail-open vs fail-closed:** parse failures fail open (hook must never wedge the fleet); policy verdicts fail closed. Don't invert either.
- **#2284 preservation rule:** every newly emitted `settings.local.json` key ships its preservation logic + test in the same change — `env` and the second hook group both count.
- **macOS path canonicalization:** the kernel resolves `/tmp` → `/private/tmp` before the hook sees it; allowlist both, and compare realpath-normalized prefixes where cheap to do so.
- **Don't depend on `~/bin`:** #2262 (install-symlink-lag) is in flight; the hook command must reference the engine clone / worktree copy by absolute path. No `install.sh` manifest change is needed (the guard is already installed; behavior changes ride the repo copies the settings point at).
- **`fleet-claim`/scout/ingest run from the main clone by design** — adding asserts there would break the fleet; the not-asserted list above is deliberate.
- **Sibling-prefix trap:** keep the trailing-slash prefix-match idiom so `worker-1-foo` can't prefix-match `worker-1`.
- **MultiEdit shares `tool_input.file_path`;** `NotebookEdit` (`notebook_path`) stays outside the matcher — out of scope.
- **Engine-public wording** in all comments/messages (this repo is public; no game-content terminology).

