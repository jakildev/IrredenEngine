## Plan: fleet: GL-vs-Metal host-capability gate for backend-specific tasks

- **Issue:** #1998
- **Model:** opus
- **Date:** 2026-06-25

### Verified current state (source-checked)

- **`fleet-claim` already has host detection** — `host_from_uname()` (`scripts/fleet/fleet-claim:207-225`) maps `uname -s` -> `mac | linux | windows | unknown`, and `derive_host()` (`:227-237`) wraps it with a `FLEET_TEST_HOST` override seam (test `tests/test_derive_host.sh`). The claim label is already host-stamped: `fleet:claim-${host}-${agent}` (`:860`). So host is known at claim time.
- **The dispatcher and the scout have NO host detection.** Grepped `fleet-dispatcher` and `fleet-state-scout` for `uname|host|darwin|macos|FLEET_TEST_HOST` -- zero hits in the dispatch/projection paths. So the *churn-stopping* fix can't live only in `fleet-claim`.
- **Backend<->host mapping is settled and one-directional** (`docs/agents/FLEET.md:414-425`, `FLEET-CROSS-HOST-SMOKE.md:3-7`): OpenGL `{linux, windows}` is one tier, Metal `{mac}` is the other. Shaders are `#version 450 core` -> **OpenGL 4.5 required**; macOS GL is 4.1, so a Metal host genuinely cannot build/run/verify the GL backend. => **GL-capable hosts = {linux, windows}; Metal-only = {mac}.**
- **Why the churn happens (the real lever):** `project_worker()` (`fleet-state-scout:1245-1269`) is only the *trigger hash*. The actual "which task/class to dispatch" decision is `fleet_task_class.resolve()` -> `_candidates()` -> **`_task_claimable(task)`** (`scripts/fleet/fleet_task_class.py:62-73`), invoked by the dispatcher's `resolve_worker_class()` (`fleet-dispatcher:1008-1022`) as `python3 fleet_task_class.py <slice> <lane-default> <fable-blocked>`. This is the **same filter** #1726 used to skip `inflight_pr` tasks and go `defer` instead of churning. A GL-host gate belongs here.
- **The slice carries full task dicts:** `slice_worker()` -> `_slice_task_with_repo()` does `dict(task)` (`fleet-state-scout:1672-1675, 1684-1703`), so any field added to the task in `fetch_task_queue()` flows straight into the dispatcher's slice (and into `state.json` `tasks.open[]`).
- **Verified live churn:** `#1937` (epic #1933 C1, "analytic edge-aware coverage -- GL backend") is opus-tagged + `owner:free` + `blocked:false`, released 5+ times by mac panes. `#1726` is CLOSED and was a different scope (design-blocked-PR projection), per the issue NB -- confirmed; the GL-host gate was never filed before this.

### Approach (single committed design)

A **host-capability label** `fleet:needs-gl-host` is respected at two enforcement points -- the dispatcher's claimability filter (stops the *dispatch*, the load-bearing fix) and a `fleet-claim` backstop gate (catches manual/raced claims). The label is a **triage signal applied by the human/architect** (like the model labels), not auto-derived -- the scout cannot reliably infer "GL-only" from a render task.

1. **Add the `fleet:needs-gl-host` label** (issue-scope, color `c5def5`). Per the `fleet-labels --check` drift guard, three files in one commit:
   - `scripts/fleet/fleet-labels` -- add to the `LABELS=(...)` array near the smoke entries (~`:137`): `"fleet:needs-gl-host|c5def5|Task needs an OpenGL-4.5 host (Linux/Windows); macOS/Metal panes skip it"` (<=100-char desc).
   - `docs/agents/fleet-state-machine.json` -- add a `labels[]` node `{"name":"fleet:needs-gl-host","scope":"issue","color":"c5def5","description":"<same <=100 desc>"}` (matches the `fleet:opus`/`fleet:blocked` issue-scope node shape at `:160-178`).
   - `docs/agents/fleet-labels-reference.md` -- prose entry near the host-smoke section (~`:154`): who applies it (human triage), who respects it (dispatcher filter + claim gate), lifecycle.
   - Run `fleet-labels --check` (must pass) then `fleet-labels` to sync both repos.

2. **Scout annotation** -- `fleet-state-scout` `fetch_task_queue()` task dict (`:429-450`): add `"needs_gl_host": "fleet:needs-gl-host" in labels,` next to `"blocked"` (`:444`). Flows into the worker slice (`dict(task)`) and `state.json` `tasks.open[]`. No other scout change.

3. **Dispatcher claimability filter (the fix)** -- `scripts/fleet/fleet_task_class.py`:
   - Inline a tiny host helper (do **not** add a new imported shared module -- module resolution across the scout's `~/bin` symlink vs the dispatcher's `FLEET_LIB_DIR` is fragile, #1750/#1578; `fleet-claim`'s `host_from_uname` is bash and can't be imported): `_current_host()` = `os.environ.get("FLEET_TEST_HOST")` else `platform.system()` mapped (`Darwin->mac`, `Linux->linux`, `Windows->windows`, else `unknown`) -- mirror `fleet-claim:207-225` exactly, including the `FLEET_TEST_HOST` seam.
   - `GL_CAPABLE_HOSTS = {"linux", "windows"}`.
   - Thread host through: `resolve()` computes `host = _current_host()` once; `_candidates(slice, lane_default, host)`; `_task_claimable(task, host)` gains `... and not (task.get("needs_gl_host") and host not in GL_CAPABLE_HOSTS)`. (Fail-closed on `unknown` -- an unrecognized host shouldn't be trusted to run GL; the real dispatch host is always one of mac/linux/windows.)
   - **Generalize the defer condition** so a host-incompatible task is *terminal* like `inflight_pr`: extend `_only_inflight_pr_tasks()` (rename to `_only_unclaimable_tasks(slice, host)`) to return True when every `tasks_open` item is `inflight_pr` **or** host-incompatible. This makes a mac slice whose only open task is GL-only return **`defer`** (go quiet -- no churn) instead of `''` (which falls through to a lane-default dispatch -> the claim->refuse->release churn we're killing). A plain `blocked`-but-GL-compatible task stays NON-terminal -> keeps the `''` stackable-tier fallthrough unchanged.

4. **`fleet-claim` backstop gate** -- `check_host_capability()` mirroring `check_model_tag()` (`:589-656`): read labels via `fetch_issue_info` (already fetched), refuse when `fleet:needs-gl-host` present and `derive_host` not in `{linux, windows}`, with a host-capability diagnostic to stderr. Slot it in `cmd_claim` **immediately after the model-tag gate** (`:792-796`), before the reservation gate (`:798`). Runs unconditionally, so it also covers `--stackable-on` GL claims on a Metal host.

5. **Tests** (mirror existing harnesses):
   - `tests/test_fleet_task_class.py` -- GL-only task is skipped under `FLEET_TEST_HOST=mac` (alone -> `defer`; mixed with a claimable task -> that task is chosen, no defer); claimable under `linux`/`windows`.
   - `tests/test_fleet_claim_host_gate.sh` (new, clone `test_fleet_claim_model_gate.sh`) -- `FLEET_TEST_HOST=mac` refuses a `fleet:needs-gl-host` claim (exit 1); `linux`/`windows` pass the host gate.
   - `tests/test_worker_projection.py` -- assert `needs_gl_host` is present on the labeled task in the worker slice.

### Affected files
- `scripts/fleet/fleet-labels` -- catalog entry
- `docs/agents/fleet-state-machine.json` -- label node (drift-guard parity)
- `docs/agents/fleet-labels-reference.md` -- prose entry
- `scripts/fleet/fleet-state-scout` -- `needs_gl_host` annotation in `fetch_task_queue` (~`:444`)
- `scripts/fleet/fleet_task_class.py` -- inline host detection + `_task_claimable(task, host)` + defer generalization
- `scripts/fleet/fleet-claim` -- `check_host_capability()` + gate slot after the model gate (~`:796`)
- `scripts/fleet/tests/test_fleet_task_class.py`, `tests/test_fleet_claim_host_gate.sh` (new), `tests/test_worker_projection.py`

### Consumer audit (label is *added*, nothing removed)
`fleet:needs-gl-host` has exactly two respecters -- the dispatcher filter (#3) and the claim gate (#4) -- plus the scout annotation (#2) that feeds the filter. No existing consumer reads it; the new field on `tasks.open[]` is inert to every other projection (it's not in any role-trigger hash, so it adds no trigger churn -- same property #1726's `inflight_pr` relies on at `fleet-state-scout:1207-1209`).

### How #1937 (and future GL tasks) get the label
The label is a **human/architect triage signal**, applied like `human:approved`/model labels. The implementer must **not** edit #1937's labels (the worker out-of-scope rule forbids editing another issue's labels) -- instead, note for the human to stamp `fleet:needs-gl-host` on #1937 and the #1933 C-chain GL tasks once this lands. Verification uses the `FLEET_TEST_HOST` seam, not a live #1937 edit. Once #1937 carries the label, mac dispatchers go quiet on it and a Linux/Windows opus pane claims it; merging it unblocks #1938 (Metal port, mac-doable) / #1939 naturally -- **no special blocked-dependent handling is needed** (verified: dependents are correctly `blocked` and resolve when the parent merges on a GL host; the issue's part-2 "perpetually blocked" concern is a non-issue once routing works).

### Acceptance criteria
- `fleet-labels --check` passes with `fleet:needs-gl-host` present in `fleet-labels` **and** `fleet-state-machine.json`.
- `fleet_task_class.resolve()` with `FLEET_TEST_HOST=mac`: a slice whose only open task carries `needs_gl_host` -> `defer`; same slice with `FLEET_TEST_HOST=linux` -> `opus <effort> ...`.
- Mixed mac slice (GL-only #1937-shaped + a claimable opus task) -> dispatches the claimable task (no defer, no churn).
- `FLEET_TEST_HOST=mac fleet-claim claim <gl-labeled-issue> worker-N` -> refused (exit 1) with a host-capability message; `FLEET_TEST_HOST=linux`/`windows` pass the host gate.
- `needs_gl_host` appears on the labeled task in the worker slice + `state.json` `tasks.open[]`.
- **No edit to any `.claude/commands/role-*.md` or other gated self-config is required** for the fix to take effect (enforcement is entirely in scripts + label catalog).

### Gotchas
- **Drift guard:** the label name must appear in BOTH the `fleet-labels` catalog and `fleet-state-machine.json`, or `fleet-labels --check` fails -- update both + the reference doc in the same commit. Description <=100 chars (GitHub hard limit).
- **No new shared Python module** for host detection -- inline the mapping in `fleet_task_class.py` (#1750/#1578 module-resolution fragility). The bash side reuses `derive_host`.
- **Defer, not `''`:** if the host-only slice returns `''` instead of `defer`, the dispatcher's lane-default fallthrough still fires a no-op dispatch -- the churn isn't fixed. The `_only_unclaimable_tasks` generalization is load-bearing.
- **No GL host online => correct wait, not loss:** if no Linux/Windows pane is up, GL tasks stop churning and simply wait for a GL pane -- intended behavior, not a regression (a Metal-only host truly cannot do GL work).
- **`unknown` host is fail-closed** (GL tasks skipped) -- document the choice; real dispatch hosts are always mac/linux/windows.
- This is the GL-backend case only. The OS-pin generalization the body mentions parenthetically (`host:linux` for the game #202 Linux-only **package** smoke) and any ingest-stamp-from-`**Host:**`-body-field auto-application are **deferred** -- the same capability mechanism extends to them, but #202 isn't churning and shouldn't expand this PR. File as follow-ups if wanted.

### One task or subtasks?
**One task.** Five coordinated edits across tightly-coupled files (label + its two respecters + scout annotation + tests), one PR's worth. No epic decomposition.
