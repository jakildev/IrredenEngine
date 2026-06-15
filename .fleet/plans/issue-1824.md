# Plan: fleet-rebase — derive inherited-prefix boundary from child fork point when parent edited during review

- **Issue:** #1824
- **Model:** sonnet
- **Date:** 2026-06-15

## Plan (opus, 2026-06-15)

**Files:** `scripts/fleet/fleet-rebase` (the inherited-prefix drop gate, ~lines 347–352) + a new test case in `scripts/fleet/tests/test_fleet_rebase_inherited_drop.sh`.

### Verified current state + repro
Read `fleet-rebase:337–402`. On the retarget path, `rebase_boundary` is the base PR's recorded `headRefOid` (gh reports it post merge+delete; fetched at L284–294). The inherited-prefix drop is gated at **L348–349** on `merge-base --is-ancestor "$rebase_boundary" "$head_sha"`. When the parent was edited/amended **during review after the child forked**, `headRefOid` (the parent's *final* pre-merge head) is a **sibling** of — not an ancestor of — the child head, so the gate is false, `drop_boundary` stays empty, and control falls through to the plain `git rebase "origin/$target"` at **L373**, which conflicts on the stale inherited files and bails to the LLM (L375). This is exactly the #1789-on-#1782 shape: the manual fix used `git rebase --onto origin/master 34cb29e6`, where `34cb29e6` is the top inherited commit on the child branch — i.e. the fork point.

### Approach (single, committed)
Add a fork-point **fallback** to the existing gate. When `rebase_boundary` is non-empty but NOT an ancestor of `$head_sha`, derive the boundary from `merge-base(child-head, recorded-parent-head)` — the child's fork point, an ancestor of the child head by definition and the top of the inherited prefix on the child branch:

```bash
local drop_boundary=""
if [[ -n "$rebase_boundary" ]]; then
    if git -C "$wt" merge-base --is-ancestor "$rebase_boundary" "$head_sha" 2>/dev/null; then
        drop_boundary="$rebase_boundary"
        log "$repo_key#$pr: dropping inherited prefix at ${drop_boundary:0:12} (rebase --onto origin/$target)"
    else
        # Parent edited/amended during review after the child forked: the
        # recorded headRefOid is a sibling of the child head, not an
        # ancestor, so the gate above fails. Fall back to the fork point —
        # merge-base(child-head, recorded parent head) — which IS an
        # ancestor of the child head and is the top of the inherited
        # prefix on the child branch (#1824 / #1789-on-#1782 shape).
        local fork_point
        fork_point=$(git -C "$wt" merge-base "$head_sha" "$rebase_boundary" 2>/dev/null) || fork_point=""
        if [[ -n "$fork_point" ]]; then
            drop_boundary="$fork_point"
            log "$repo_key#$pr: parent edited post-fork; dropping inherited prefix at fork point ${drop_boundary:0:12} (rebase --onto origin/$target)"
        fi
    fi
fi
```

Everything downstream is **unchanged** and already consumes `drop_boundary`: the `file_base` snapshot (L363–365), the `rebase --onto "origin/$target" "$drop_boundary"` (L367–372), and the changed-file-set drift guard (L379–383).

### Why this is safe (no regression)
The fork-point boundary only ever *adds* a successful tier-0 drop where today the script bails to the LLM. If the parent's commits were **rewritten** (not merely appended) so the fork point lands below the inherited prefix, the `--onto` replay of the stale inherited commit either conflicts (→ existing bail L370) or trips the changed-file-set drift guard (→ existing bail L381). It can never convert a today-clean push into a bad one — it's a strict superset of cleared cases.

### Test (extend the existing harness)
Add **T3** to `test_fleet_rebase_inherited_drop.sh` reproducing the #1789 shape (mirrors the T1/T2 sandbox):
- master: init → squash `S` holding the parent's *amended* content (`base.txt` "v2" + `base2.txt`).
- Parent branch (never pushed → "merged+deleted"): init → `P1` (add `base.txt` "v1") → `P2` (add `base2.txt`). Stub `merged/<head>.tsv` records `headRefOid = P2`.
- Child: init → `P1` → `C1` (add `child.txt`). So `P2` is a **sibling** of `C1` (gate fails); `merge-base(C1, P2) = P1` (fork point).
- Assert: retarget path taken; logs `parent edited post-fork; dropping inherited prefix at fork point ${P1:0:12}`; `attempted=1 cleared=1 llm_remaining=0`; **absent** "changed-file set drifted" and "conflicts; leaving for LLM".
- Run `bash scripts/fleet/tests/test_fleet_rebase_inherited_drop.sh`; T1/T2 must stay green.

### Sibling / in-flight reconciliation
No open PR touches `fleet-rebase`. This is the script half of #1791's triage; the worker-facing manual `rebase --onto` fallback already landed in `role-worker.md`. Purely an added fallback branch on the same #1690 gate — no contradiction with existing behavior.

### Scope, model, acceptance
- **One task, `[sonnet]`** — bounded (~12-line fallback branch + one templated test); the plan pins the merge-base semantics and the correctness argument, and the existing conflict/drift guards bound the downside.
- **Acceptance:** new T3 passes; T1 + T2 still pass; the additive-edit (#1690) path keeps logging the original `dropping inherited prefix at <boundary>` line; no behavior change when `headRefOid` IS already an ancestor.
- Reviewer should give the gate logic a focused read (the body's correctness-stakes ask); the T3 test is the durable guard.

### Gotchas
- **merge-base must be `(child head, recorded parent headRefOid=$rebase_boundary)` — NOT `(child head, origin/master)`.** master holds the *squash* (not on the child branch), so merge-base against it returns `init` and would replay the whole stale inherited prefix. Use `$rebase_boundary`.
- Keep the existing `is-ancestor` fast path **first** so additive-edit parents (the #1690 case) keep the original log line — T1 depends on it.
- `local fork_point` stays inside the per-PR function (it already is).
- `fleet-rebase` is installed to `~/bin` by `install.sh`; the running fleet picks up the new logic only after the main clone's master advances + reinstall — out of scope for the PR.

— worker (opus planning)
