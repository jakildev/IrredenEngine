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

reconciled-through: **2026-09-10 close-out criteria audit** (second steward
claim, 59 days after the first). No child moved — all four were already merged
and verified 2026-07-13. What moved is the *park*: its gate had never been
measured, only asserted. All three umbrella acceptance criteria now carry
evidence (**F1**–**F3**), and the one item that is genuinely unmet is the
plan's own extra, not an acceptance criterion (**F4**). Prior: 2026-07-06 (all
four code children merged), 2026-07-13 (rollup).
proposal-pending: **`## Steward close-out audit — 2026-09-10`** on the umbrella,
`fleet:steward-proposal` applied. One question: the plan's *Closing criteria*
adds "the App created/configured" on top of the umbrella's three acceptance
criteria — does that extra still gate an umbrella whose own three criteria are
now evidenced met? Options (a) close now and re-file App adoption free-standing
/ (b) hold open for the App / (c) close and drop the App. **Recommendation:
(a)** — post the close-out summary with the F1–F3 evidence, close #1394, and
file App adoption as a free-standing issue (which is how `docs/agents/FLEET.md`
already describes it — "Once the fleet moves to a GitHub App token
(**tracked separately**)"). That is advisory only: #1394 stays open and the
plan's *Closing criteria* line stays as written until a human or architect
answers on the umbrella thread and removes `fleet:steward-proposal`. That
removal is the sole re-fire edge (protocol §Flow a) — elapsed time is not one,
and no steward may take a branch on its own.

### Children

All four verified shipped: closing PR `state=MERGED` **and** the PR carrying an
implementation artifact, not a docs-only file list (the #2583 check — re-run
2026-09-10, file counts below are non-`.fleet/plans/` paths).

| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2219 | merged (9 impl files) | #2227 | `## Plan` comment | 2026-09-10 |
| #2220 | merged (11 impl files) | #2231 | `issue-2220.md` + `## Plan` comment | 2026-09-10 |
| #2221 | merged (7 impl files) | #2234 | `## Plan` comment | 2026-09-10 |
| #2222 | merged (9 impl files) | #2232 | `## Plan` comment | 2026-09-10 |

### Decisions
D1 (2026-07-03): identity model = GitHub App; machine accounts last-resort; webhooks long-term.
D2 (2026-07-03): all four mitigations approved; ETag+REST-routing merged into Q1 (one poller change).
D3 (2026-07-03): Q3 gates ON by default at 0.90 for core/graphql; search surface-only.
- D4 (2026-09-10): **the umbrella does not close on the steward's authority.**
  Its three acceptance criteria are evidenced met (F1–F3), but the plan's
  *Closing criteria* line adds a fourth item the steward cannot satisfy or
  retire — retiring a stated closing criterion is an umbrella-goal change and
  belongs to the human (protocol §Escalation rules). Recorded as a pending
  proposal with a recommendation, and left for the responder — the steward
  neither acts on it nor schedules an action for a later steward to take on
  silence.

### Events
- 2026-07-04: children #2219–#2222 filed with plans; `fleet-validate-stack 1394` PASS (4/4); all children human:approved.
- 2026-07-06: all four code children merged in-scope — Q1 #2219→PR #2227 (conditional-REST scout polling), Q2 #2220→PR #2231 (leader/follower centralized polling), Q3 #2221→PR #2234 (quota headroom + dispatcher gate), Q4 #2222→PR #2232 (GitHub App token plumbing). Scope-drift audit: each PR matches its Q-scope; no contradiction of D1–D3.
- 2026-07-13: rollup — all four checklist boxes ticked. **Close-out remains blocked on the human-owned remainder** (create/install the `irreden-fleet` GitHub App + one sustained multi-device session with headroom visible in `fleet-gate-status`, per Closing criteria + umbrella acceptance #1/#3). Umbrella stays open with `fleet:needs-human`; no code touched.
- 2026-09-10: second steward claim. The 2026-07-13 park bundled two very
  different things under one "human-owned remainder": an **acceptance
  criterion** (A1, sustained multi-device operation without hitting the limit)
  and a **setup task** (create the App). A1 is measurable from the fleet's own
  logs and had never been measured; measuring it (F1) shows it met. Criteria
  audit posted to the umbrella; `fleet:steward-proposal` applied so the ask has
  a re-fire edge (the 2026-07-13 park used `fleet:needs-human` alone, which has
  none — that is why it sat 59 days). No child moved, no code touched.

### Findings (close-out gate — beyond the checklist)

- **F1 (2026-09-10) — acceptance #1 is MET, measured, not assumed.**
  `~/.fleet/logs/state-scout.log` covers **2026-04-25T19:00:51Z →
  2026-09-10T06:26:48Z** continuously (142,518 lines) and contains **1,796**
  `GraphQL: API rate limit already exceeded` failures, on exactly seven days:
  2026-05-30 (72), 06-10 (216), 06-12 (70), 06-13 (396), 06-14 (266), 06-15
  (585), 06-27 (191). **The last one is 2026-06-27T20:00:29Z.** Q1 (PR #2227)
  merged 2026-07-05T23:26Z and Q2 (PR #2231) 2026-07-06T01:28Z — so there are
  **zero exhaustion events in the 75 days since the last one, 66 of them after
  the mitigations landed**, against a longest pre-fix quiet interval of **12
  days** (06-15→06-27). Corroborating: `~/.fleet/logs/dispatcher.log` carries
  **243** `usage gate closed:` transitions and **not one** names a GitHub pool —
  every closure is `five_hour` / `seven_day` (the Anthropic usage gate). The
  13 lines that do name `github_*` are all `usage gate re-opened (open:github_…
  util=N% (< 90%))`, i.e. the re-open observation, and the last is
  **2026-08-08**.
  *Method note:* the first pass grepped `API rate limit exceeded` and returned
  **0** — the live string is `API rate limit **already** exceeded`. The zero was
  a false clean; it was caught by controlling the grep against the log's own
  vocabulary (1,796 `rate limit` / 2,047 `exceeded` occurrences) before the
  result was recorded. Do not re-derive this without the control.
- **F2 (2026-09-10) — acceptance #2 is MET.** Identity model chosen and
  documented: D1 above (umbrella decisions comment 2026-07-03) and
  `docs/agents/FLEET.md:1129`. Polling architecture wired into `scripts/fleet`
  on `origin/master`, each with tests: Q1 `fleet_gh_poll.py` (41 ETag/
  `If-None-Match` sites) + `tests/test_gh_poll_conditional.py`; Q2
  `fleet_poll_topology.py` leader/follower + `tests/test_poll_topology.py`;
  Q3 `fleet-gate-status` + `tests/test_dispatcher_github_gate.sh`; Q4
  `fleet-gh-token` + `tests/test_fleet_gh_token.sh`.
- **F3 (2026-09-10) — acceptance #3 is MET, verified live.** `fleet-gate-status`
  run on this host at 2026-09-10T06:2xZ prints a `GitHub API quota:` block:
  `core remaining=4978/5000 (< 90%) resets=…(in 30m 48s)`, `graphql
  remaining=4713/5000`, `search remaining=30/30`, sourced from the scout's
  `sample_github_rate_limit` observations with a staleness annotation.
- **F4 (2026-09-10) — the App is NOT configured, and that is the only unmet
  item.** `~/.fleet/fleet-up.conf` on this host (mac): **0 of 303 lines** match
  `FLEET_GH_APP` (grep rc=1, positive-controlled against the file's own line
  count). `FLEET_GH_APP_ID` / `_INSTALLATION_ID` / `_KEY_PATH` exist only as
  commented samples in `scripts/fleet/fleet-up.conf.sample:32-34`, and
  `docs/agents/FLEET.md:1129` still phrases the App in the future tense. Q4's
  code ships with the documented personal-auth fallback, so nothing is broken —
  the App is unadopted, not failed. **This is a plan-level closing item, not one
  of the umbrella's three acceptance criteria**, which is the distinction the
  2026-07-13 park collapsed.
- **F5 (2026-09-10) — the epic's own recommendation predicted F1.** Umbrella
  body §Recommendation: "Do the cheap reductions first (ETags + centralized
  cross-device polling + REST-for-reads); that likely makes a single quota pool
  sufficient." The plan §Cross-child notes says it more sharply: "One App
  installation = **one shared pool**, not per-device multiplication — the Q1/Q2
  reductions are what make a single pool sufficient." F1 is that prediction
  coming true, which is why option (a) is the recommendation: on the epic's own
  reasoning the App was the *headroom-and-hygiene* half, never the load-bearing
  half.
