# Plan — Epic #1394: fleet — GitHub API rate-limit exhaustion under multi-device polling

- **Umbrella:** #1394 (fleet:epic + fleet:needs-human — the App creation is human-owned)
- **Filed:** children filed 2026-07-04 from the 2026-07-03 architect triage session (human-ratified decisions)

## Decisions (human-ratified 2026-07-03)

- **Identity model: GitHub App** (`irreden-fleet`) — one clean bot identity with its
  own separate rate-limit pool. Machine accounts reserved as last-resort per-device
  multiplication; webhooks remain the unscheduled long-term target.
- **All four code-side mitigations approved and filed** (ETag conditional polling and
  REST-for-reads merged into one poller-core child — same code change).
- Measured basis (2026-07-03): `graphql 3518/5000` with `core` idle at `4999/5000` —
  the scout's 8 unconditional GraphQL list queries per repo per 30s tick are the
  multiplier; REST 304s are rate-limit-free.

## Decomposition

| Child | Issue | Model | Blocked by | Scope |
|---|---|---|---|---|
| Q1 | #2219 | opus | — | conditional-REST polling core: `fleet_gh_poll.py` ETag cache + REST-for-reads + gated/batched GraphQL |
| Q2 | #2220 | opus | #2219 | centralized cross-device polling: static leader serves `~/.fleet/state` over HTTP; followers conditional-GET |
| Q3 | #2221 | sonnet | — | `/rate_limit` headroom surfacing (usage-gate mirror) + pre-emptive dispatcher gate |
| Q4 | #2222 | sonnet | — | GitHub App token plumbing: `fleet-gh-token` (JWT → installation token, cached/locked) + guarded `GH_TOKEN` exports |

Each child's canonical plan is its issue's `## Plan` comment (file-with-plan path).

## Human-owned remainder (why the umbrella stays fleet:needs-human)

Create the `irreden-fleet` GitHub App and install it on both repos — the full
step-by-step lives in the umbrella's 2026-07-03 decisions comment: permissions
Contents/Issues/PRs read-write + Metadata read, webhook OFF; note App ID +
Installation ID; key at `~/.fleet/secrets/irreden-fleet.pem` (chmod 600); set
`FLEET_GH_APP_ID` / `FLEET_GH_APP_INSTALLATION_ID` / `FLEET_GH_APP_KEY_PATH` in
`~/.fleet/fleet-up.conf` per host. Q4's code lands regardless (personal-auth
fallback when unset); only its live verification is gated on the App existing.

## Cross-child notes

- Q4's `GH_TOKEN` export composes with Q1 automatically (the poller reads
  `gh auth token`, which honors `GH_TOKEN`) and with Q3 (the `/rate_limit` sampler
  reports whatever identity gh is authed as — numbers jump to the App pool when Q4
  lands).
- Q2 switches dispatcher scout-staleness from file mtime to in-file `generated_at`;
  follower hosts must never restamp the leader's `generated_at`.
- One App installation = **one shared pool**, not per-device multiplication — the
  Q1/Q2 reductions are what make a single pool sufficient.

## Closing criteria

All four children merged + the App created/configured + a full multi-device fleet
session sustained without hitting either primary limit (umbrella acceptance #1),
with headroom visible in `fleet-gate-status` (acceptance #3). Then #1394 closes.

## Steward ledger

reconciled-through: 2026-07-06 (all four code children merged)
proposal-pending: none

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2219 | merged | #2227 | plan | 2026-07-13 |
| #2220 | merged | #2231 | plan | 2026-07-13 |
| #2221 | merged | #2234 | plan | 2026-07-13 |
| #2222 | merged | #2232 | plan | 2026-07-13 |

### Decisions
D1 (2026-07-03): identity model = GitHub App; machine accounts last-resort; webhooks long-term.
D2 (2026-07-03): all four mitigations approved; ETag+REST-routing merged into Q1 (one poller change).
D3 (2026-07-03): Q3 gates ON by default at 0.90 for core/graphql; search surface-only.

### Events
- 2026-07-04: children #2219–#2222 filed with plans; `fleet-validate-stack 1394` PASS (4/4); all children human:approved.
- 2026-07-06: all four code children merged in-scope — Q1 #2219→PR #2227 (conditional-REST scout polling), Q2 #2220→PR #2231 (leader/follower centralized polling), Q3 #2221→PR #2234 (quota headroom + dispatcher gate), Q4 #2222→PR #2232 (GitHub App token plumbing). Scope-drift audit: each PR matches its Q-scope; no contradiction of D1–D3.
- 2026-07-13: rollup — all four checklist boxes ticked. **Close-out remains blocked on the human-owned remainder** (create/install the `irreden-fleet` GitHub App + one sustained multi-device session with headroom visible in `fleet-gate-status`, per Closing criteria + umbrella acceptance #1/#3). Umbrella stays open with `fleet:needs-human`; no code touched.
