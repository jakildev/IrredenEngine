## Plan: the smoke lane has no host gate in code

- **Issue:** #2839
- **Model:** opus
- **Date:** 2026-08-04

### Verified current state

Source-checked and repro'd against `origin/master` @ `34c7f7f46` (full evidence
in the issue body). The control run — same live `state.json`, same Darwin host,
only `_SMOKE_PENDING_LABELS` differing — returns `smoke_worker_actionable ==
True` post-#2809 and `False` on the pre-#2809 control, with the projection
holding 3 `fleet:needs-windows-smoke` PRs (2659, 2683, 2812) and zero macOS
smoke work. The live dispatcher log shows 4 no-op smoke dispatches in 43
minutes and 2 empty-exit backoff cap-hits.

Give the smoke lane the code-level host gate that #1998 (tasks) and #2695
(feedback PRs) already gave the other two lanes, so a host is only woken for
smoke work it can actually perform. #2809's addition of
`fleet:needs-windows-smoke` to `_SMOKE_PENDING_LABELS` stays — this supplies
the host dimension that its "routing stays the role's job" rationale assumed
would exist.

### Approach

The gate is one predicate — *does this PR's smoke label name my host?* — placed
dispatch-side. Take **(A)**; **(B)** is recorded only as the rejected
alternative, so the implementer does not re-derive the comparison.

**(A) Dispatch-side — this is the plan.** Keep the scout host-agnostic; filter
where the existing host gate already lives. This mirrors both precedents:
`fleet_task_class.py:_host_incompatible` gates at *election*, not in the scout,
and `fleet-dispatcher`'s `worker_rearm_should_fire()` already exists precisely
to stop re-arming a lane for work this host cannot claim. Add the symmetric
`smoke_worker_should_fire()` and host-filter `smoke_worker_actionable`.

**(B) Scout-side — rejected.** Host-filtering `project_smoke_worker` /
`slice_smoke_worker` directly is a smaller diff, but it makes a scout
projection **host-dependent** for the first time — two hosts' scouts would
derive different projections from identical GitHub state. That new precedent
for `state.json`, and the loss of the projection as the cross-host record of
outstanding smoke debt, is why (A) wins.

Steps:

1. Add a canonical host→smoke-label map beside `GL_CAPABLE_HOSTS` in
   `fleet_task_class.py`. **Mind the vocabulary split** (#1383): `_current_host()`
   returns `mac`, while the label and `role-smoke-worker.md` step 4 both use
   `macos`. The map is the single place that reconciles them:
   `{"linux": "fleet:needs-linux-smoke", "mac": "fleet:needs-macos-smoke",
   "windows": "fleet:needs-windows-smoke"}`. Fail-closed on `unknown` (no label
   ⇒ never actionable), matching `_host_incompatible`'s existing stance.
2. Export a `smoke_pr_for_host(labels, host)` predicate next to it.
3. `fleet-up` — `smoke_worker_actionable` filters `smoke_pending_prs` by that
   predicate before the `bool()`. This kills the boot-dispatch, which is the
   level-triggered half and the one the empty-exit backoff cannot durably
   suppress.
4. `fleet-dispatcher` — add `smoke_worker_should_fire()` modelled on
   `worker_rearm_should_fire()` and consult it before dispatching on a standing
   smoke trigger, so a projection-hash flip driven purely by another host's
   label does not spend a pane.
5. Leave `_SMOKE_PENDING_LABELS` and both scout functions **unchanged** — the
   projection stays the host-agnostic record of all outstanding smoke debt,
   which is what `platform-catchup` and any cross-host reporting want.
6. Extend `scripts/fleet/tests/test_smoke_worker_projection.py` with the
   host-routing cases (it is already the harness #2809 added for exactly these
   two functions).

### Affected files

- `scripts/fleet/fleet_task_class.py` — host→smoke-label map + `smoke_pr_for_host`
- `scripts/fleet/fleet-up` — `smoke_worker_actionable` host filter
- `scripts/fleet/fleet-dispatcher` — `smoke_worker_should_fire()` gate on the
  standing smoke trigger
- `scripts/fleet/tests/test_smoke_worker_projection.py` — host-routing cases

`fleet-state-scout` is deliberately **not** in this list — under (A) it does
not change.

### Acceptance criteria

Inherits the five in the issue body. The decisive one is the control run
already recorded there — it must **invert**: on Darwin with only
`fleet:needs-windows-smoke` pending, `smoke_worker_actionable` goes `True` →
`False`, while a `FLEET_TEST_HOST=windows` run on the same `state.json` stays
`True` and still slices PRs 2659 / 2683 / 2812. Use the existing
`FLEET_TEST_HOST` seam (`fleet_task_class.py:_current_host`) rather than
mocking `platform.system`.

Plus: `scripts/fleet/tests/run_all.sh` green.

### Plan-review constraints (binding — opus-reviewer, pool-5)

**Constraint 1 — how bash/heredoc reaches the predicate is not free.**
`smoke_worker_actionable` lives inside `fleet-up`'s standalone bootstrap
heredoc (`python3 - <<'PY'`, imports `json` + `pathlib` only) with no path to
`FLEET_LIB_DIR`, and the heredoc runs under `|| true` — an `ImportError` from
sys.path plumbing is swallowed and the bootstrap trigger dies **for all six
roles at once**, silently. `fleet-dispatcher` reaches the module only by
*executing* it, and `main()` hard-fails `len(argv) not in (4, 5)`, so a bare
exported function is not callable from step 4 either. Resolution:

- `fleet-up` — **inline** the host key + label map in the heredoc, mirroring
  `_current_host`'s documented inlining precedent (#1750/#1578): same
  `FLEET_TEST_HOST` override, same `Darwin/Linux/MINGW|MSYS|CYGWIN|Windows`
  mapping, same fail-closed `unknown`.
- `fleet-dispatcher` — add an explicit **CLI arm** to `fleet_task_class.py`
  (placed before the arity check, like the existing `--plan-pick` arm) so
  `smoke_worker_should_fire()` shells out the way `resolve_worker_class` does.
- `fleet_task_class.py` stays the canonical map; the heredoc copy carries a
  `keep in sync` comment naming it, and a pinned per-host test covers **both**
  copies.

**Constraint 2 — the gate is dispatch-side only (disclosure, not scope).**
#1998 / #2695 enforce at two layers: the election predicate *and*
`fleet-claim`'s outright refusal. This change adds dispatch/boot only —
`fleet-claim review-claim` has no host term, and widening it would break the
reviewer lanes it is shared with (a macOS opus reviewer reviewing a
`needs-windows-smoke` PR is legitimate). Correctly out of scope; state the
residual explicitly in the PR body rather than claiming full parity.

### Gotchas

- **`env -u FLEET_PLAN_ISSUE` before running `run_all.sh`** — an ambient
  `FLEET_PLAN_ISSUE` reddens the dispatch-wrap suite (#2836), and the suite
  needs a ~10-minute timeout.
- **Run the changed suite alone as well as via the whole-dir runner.** The
  shared-process runner can launder a red suite green (#2825).
- **Do not "fix" this by reverting #2809.** Removing
  `fleet:needs-windows-smoke` from the pending set re-opens #2804 (the native-
  Windows lane loses its trigger entirely). The label belongs in the set; the
  host filter belongs downstream of it.
- **Two host vocabularies** (`mac` vs `macos`) — #1383 reconciled the
  *detectors*, not the spelling. Getting this wrong means no label ever matches
  ⇒ the host is never actionable ⇒ the smoke lane silently dies, the same
  failure mode as #2804. Pin it with an explicit test per host key.
- `smoke_worker_actionable`'s current form is quoted verbatim in
  `test_smoke_worker_projection.py` as the fleet-up predicate; that assertion
  must move in lock-step or it will encode the pre-fix behaviour.
