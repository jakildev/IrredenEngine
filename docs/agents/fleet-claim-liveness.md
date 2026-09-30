# Claim liveness

The detail behind the liveness summary in [`FLEET.md` § Claims](FLEET.md#claims).

Liveness is heartbeat- and dispatch-derived, and it is judged only on the host
that owns the claim. Every sweep that removes a `fleet:amending-*` or
`fleet:claim-*` label past its TTL asks one predicate first
(`scripts/fleet/fleet_claim_liveness.py`): `cleanup --gh`'s PR-label and
issue-claim passes, the claim-time force-sweep, and `reconcile` R1. The owner
is live when a dispatch record in `~/.fleet/state/dispatch/` names it on this
item, or when its `~/.fleet/heartbeats/<agent>` is younger than the sweep's
own TTL and the claim's stamped dispatch id is the pane's current one (or
either id is absent, or the pane's reservation names the item). A heartbeat
never vouches alone, since every role refreshes it. The branch name is the
ownership check (a PR matching the issue keeps the claim as active work), never
the liveness check, so a pane on a detached HEAD is as live as its heartbeat.
Heartbeats refresh only when a role re-runs `fleet-heartbeat`, so a long
stretch of evidence work with no build step leans on the dispatch record.

| Knob | Default | What it bounds |
|---|---|---|
| `FLEET_CLAIM_STALE_SECS` | 1800 | PR-label TTL (review/amend/resolve), `reconcile` R1, and the heartbeat window those sweeps accept |
| `FLEET_CLAIM_STALE_SECS_ISSUES` | 7200 | issue-claim TTL and its heartbeat window |
| `FLEET_CLAIM_CROSSHOST_STALE_SECS` | 43200 | backstop for another host's amending/issue claim |
| `FLEET_CLAIM_SWEPT_COOLDOWN_SECS` | 1800 | dispatcher deferral after an age-only sweep |

The authoritative poller runs the full `cleanup --gh`; every follower runs
`cleanup --gh --own-host` on the same cadence, over labels naming its own
host. On the full run, another host's `fleet:amending-*` / `fleet:claim-*`
label is removed only past the 12 h backstop. A host that dies without
`fleet-down` therefore strands its amending PRs and claimed issues for up to
12 h rather than the 30-min / 2-h TTLs; recover with `fleet-up` on that host
(`reset-sweep-host-claims`) or by removing the label by hand. Cross-host
`fleet:reviewing-*` / `fleet:resolving-*` stay on the 30-min TTL.

When the sweep removes an amending or resolving claim on age alone, not on
proof of death, it stamps `fleet:sweep-cooldown` on the PR, and the dispatcher
withholds that PR's `feedback` / `conflict` target until its `updatedAt` is
older than the cooldown, logging `deferring <target>: claim swept <age>s ago`
once. A won claim clears the label
([`fleet-labels-reference.md`](fleet-labels-reference.md) § `fleet:sweep-cooldown`).

## Why an amending claim's marker is the dispatch, not the heartbeat

The heartbeat is pane-scoped, so any later dispatch of any role in the pane
renews it: as the `fleet:amending-*` orphan marker it would keep a dead claim
live indefinitely, leaving the PR at once un-reapable and un-claimable. The
dispatcher's `preclaim` sentinel skips the heartbeat for the same reason in
reverse: the pane it launches into is idle and its heartbeat still belongs to
the previous iteration, so deferring to it would reap the dispatcher's own
fresh claim off a carried-over past-TTL label and admit a second feedback
worker ([`fleet-labels-reference.md § Claims`](fleet-labels-reference.md#claims-dynamic-script-owned)).
