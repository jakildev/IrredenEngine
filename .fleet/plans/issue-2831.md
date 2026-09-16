## Plan: fleet: the opus-reviewer 30-min cooldown floor is unwired — OPUS_MIN_BACKOFF has zero readers and FLEET_MIN_BACKOFF is never set

- **Issue:** #2831
- **Model:** sonnet — the approach below is committed and the edit set is a bounded deletion + one small test; no design judgment remains at implementation time
- **Date:** 2026-08-08

### Scope

Remove the dead cooldown-floor machinery (issue option 3): delete
`OPUS_MIN_BACKOFF` from `fleet-up`, delete `MIN_BACKOFF_SECONDS` and
`interruptible_sleep`'s `min_floor` parameter from `fleet-babysit`, fix the
two comments that assert a wiring that does not exist, and pin the surviving
trigger-wake behavior with a test. **No behavior change anywhere** — the floor
has evaluated to `0` on every path since 2026-05-06.

### Verified current state (re-verified @ `5a1fdc9ec`, 2026-08-08)

The issue's evidence (pinned @ `0570f3b3d`) still holds exactly:

- `git grep 'OPUS_MIN_BACKOFF\|FLEET_MIN_BACKOFF' origin/master` tree-wide →
  5 hits, all in the two files, none a producer: `fleet-up:117` (assignment,
  zero readers), `fleet-up:116` + `fleet-babysit:129` (the two false
  comments), `fleet-babysit:131` (the defaulted-0 read),
  `fleet-babysit:195` (function-doc mention).
- `~/.fleet/fleet.conf` on the live fleet host sets neither variable
  (checked 2026-08-08).
- `interruptible_sleep` is defined and called **only** inside
  `fleet-babysit` (tree-wide grep). Four call sites: `:631`, `:662`, `:674`
  pass one arg; **only `:645`** passes the trigger path and
  `$MIN_BACKOFF_SECONDS` as the optional third arg (`min_floor`, declared
  `${3:-0}` at `:199`, guard at `:208-213`).

**History — the severing was an accident of the cutover, not a policy
decision.** `fece69f89` (2026-05-01, T-080/#396) introduced the floor and
wired it inline at the dedicated opus-reviewer pane launch
(`FLEET_MIN_BACKOFF=$OPUS_MIN_BACKOFF $(launch_cmd ... opus-reviewer 30m)`).
`4e338cbc7` (2026-05-06, the #273 dispatcher cutover) replaced that pane
launch with `TRANSIENT_PANE_CMD` (`exec $SHELL -i`; the dispatcher drives
transient iterations), deleting the producer line as a side effect. A
follow-up commit inside the same T-080 PR had *extracted* the constant with
the "keep the two in sync" comment — which is why the comments outlived the
wiring. The floor was functional for **five days**, three months ago.

**Who runs `fleet-babysit` today:** only the two architect panes
(`fleet-up:1743/:1779`), which scout has no projectors for
(`fleet-babysit:167-168` — their trigger file is never written, so the floor
branch is unreachable), plus manually-run fallback worker iterations
(`fleet-up:1715` comment; the `:1510` "fall back to fleet-babysit" message is
an operator instruction, not an automatic launch path — every pool pane gets
`TRANSIENT_PANE_CMD` unconditionally). No babysit launch site has passed
`FLEET_MIN_BACKOFF` since the cutover.

### Approach — delete the machinery (issue option 3), committed

Why 3 and not 2 (wire a floor into the dispatcher) or 1 (re-export on the
babysit path):

1. **The policy the floor implemented is obsolete.** It was designed
   (T-080, 2026-05-01) for a *dedicated, always-on* opus-reviewer pane in
   the babysit era, where a standing trigger meant immediate relaunch and
   the floor was the only spend control. The dispatcher replaced that world
   with layered, **work-gated** controls: triggers arm only on projection
   deltas; usage-limit + transient-429 cooldowns react to actual spend
   (`fleet-dispatcher:1014-1043`); the empty-exit streak kills no-work
   thrash (`:428-465`); claimable-count and role caps bound fan-out
   (`:467-486`); `DISPATCH_MIN_GAP_SECONDS` paces launches (`:252`). Every
   relaunch-rate problem observed since the cutover was fixed with a
   targeted work-gated mechanism (#2698 empty-exit marker, #2700 per-kind
   wake suppression) — never a time floor.
2. **Reinstating the floor would throttle the fleet's current bottleneck.**
   25 of 30 open engine PRs sit `fleet:approved` right now — the
   review/merge lane is the constraint, not opus spend. A hard 30-min
   relaunch floor adds up to 30 min of latency per opus-recheck round while
   saving nothing when work is standing (the same reviews run, later).
   Option 1 is strictly worse (the issue already notes it converts a
   visibly dead variable into an invisibly half-wired one), and option 2
   would land a *behavior-changing* throttle on the production path plus
   new per-role state in a daemon that three open PRs (#2961, #2964,
   #2968) are already touching.
3. **Deletion is the no-behavior-change option.** The floor has been 0 on
   every path for three months; the system the human has been operating and
   tuning is the floor-less one. Deleting makes the docs true and removes a
   dead parameter; it forecloses nothing (a future dispatcher-side budget
   floor would be new work against `fleet-dispatcher`, which this plan
   deliberately does not touch).

**Phase 0 (one-line premise probe, implementer runs first):** in the fleet
host's tmux server environment, `tmux show-environment -g 2>/dev/null | grep
-i MIN_BACKOFF; env | grep -i MIN_BACKOFF` → expect empty (the repo tree and
`~/.fleet/fleet.conf` are already verified clean above). A hit means an
out-of-tree producer exists and the deletion WOULD change behavior for that
operator: stop, comment the finding on this issue, and design-block instead
of proceeding.

**Steps:**

1. `scripts/fleet/fleet-up` — delete the three-line block `:115-117`
   (comment pair + `OPUS_MIN_BACKOFF=1800`). Nothing else in the file
   references it.
2. `scripts/fleet/fleet-babysit`:
   - delete `:126-131` (the `MIN_BACKOFF_SECONDS` comment block +
     assignment). Do **not** touch `:122-124` (`LONG_BACKOFF_SECONDS` — that
     is the real, working sleep *duration*; its "30 min" comments at `:18`
     and `:122` are true and stay);
   - `interruptible_sleep`: drop the `min_floor` sentence from the function
     doc (`:192-195`), the `local min_floor="${3:-0}"` (`:199`), and the
     floor comparison — the guard at `:208-213` collapses to
     `if [[ -n "$trigger_path" && -f "$trigger_path" ]]; then return 2; fi`.
     The `elapsed` local at `:209` exists only for the comparison — delete
     it too, don't leave an unused local;
   - call site `:645` — drop the third argument:
     `interruptible_sleep "$sleep_dur" "$TRIGGER_FILE" || rc=$?`.
3. New test `scripts/fleet/tests/test_babysit_interruptible_sleep.sh`,
   following `test_babysit_launch.sh`'s conventions (`set -euo pipefail`,
   `source lib_assert.sh`, sandbox `$TMPROOT`). Extract the function body
   from the script under test (`sed -n '/^interruptible_sleep()/,/^}/p'
   "$BABYSIT"` into a sourced temp file, with `SHUTDOWN_FLAG` and
   `HEARTBEAT_FILE` set to sandbox paths) so the test exercises the shipped
   text, not a copy. Three arms:
   - **positive-fire (trigger immediacy):** pre-create the trigger file,
     call `interruptible_sleep 30 "$trigger"`; assert **rc=2** and wall time
     < 10s — the wake observably fires, and this is the exact behavior the
     dead floor used to suppress;
   - **shutdown sentinel:** pre-create `$SHUTDOWN_FLAG`, call with a 30s
     duration; assert rc=1, wall time < 10s;
   - **plain elapse:** no trigger, no sentinel, duration 1; assert rc=0.
4. Verify: `git grep -n 'OPUS_MIN_BACKOFF\|FLEET_MIN_BACKOFF\|MIN_BACKOFF_SECONDS\|min_floor' -- scripts/` → zero
   hits; run the new suite plus `test_babysit_effort.sh` and
   `test_babysit_launch.sh`.

### Affected files

- `scripts/fleet/fleet-up` — delete `:115-117` (`OPUS_MIN_BACKOFF` block)
- `scripts/fleet/fleet-babysit` — delete `:126-131`; simplify
  `interruptible_sleep` (doc, signature, guard); 3-arg call at `:645` → 2-arg
- `scripts/fleet/tests/test_babysit_interruptible_sleep.sh` — **new**

### Acceptance criteria

1. `git grep -c 'OPUS_MIN_BACKOFF'` → zero (the issue's first criterion,
   drop form), and `git grep 'FLEET_MIN_BACKOFF\|MIN_BACKOFF_SECONDS\|min_floor' -- scripts/`
   → zero, which simultaneously discharges the issue's second criterion (no
   comment asserts an undemonstrable wiring — the comments are gone).
2. The issue's drop-path criterion: `min_floor` is gone from
   `interruptible_sleep` and all four remaining call sites (`:631`, `:645`,
   `:662`, `:674`) pass an arity the function accepts (1 or 2 args).
3. Positive-fire: the new test's trigger-immediacy arm **observably fires**
   (rc=2 inside a 30s nominal sleep) on the shipped function text; sentinel
   and plain-elapse arms green. Fixture: the test file itself creates its
   trigger/sentinel files — no external fixture needed, so the
   fixture-must-exist rule is satisfied by construction.
4. `test_babysit_effort.sh` and `test_babysit_launch.sh` still green
   (guards against an accidental syntax/flow break in the edited script).

### Gotchas

- **Do not touch `fleet-dispatcher`.** Three approved PRs are in flight
  against it (#2961, #2964, #2968); this plan's whole conflict-avoidance is
  that the edit set is `fleet-up` + `fleet-babysit` + one new test file.
  PR #2964 also touches `test_role_trigger_empty.py` /
  `test_fleet_debug_triggers.py` — don't edit those either.
- `LONG_BACKOFF_SECONDS` (`fleet-babysit:124`) looks adjacent and also says
  "30 min" — it is the *working* sleep-duration ceiling, not the dead floor.
  Leave it and its comments (`:18`, `:122`) alone.
- The `while now=$(date +%s); [[ $now -lt $end ]]` loop head (`:204`) reads
  oddly but is correct (command-then-test); don't "fix" it while editing the
  guard.
- One suite in the fleet bash test set is known RED on master
  (cross-repo-parity); don't let that pre-existing failure be attributed to
  this change — run the three named suites individually.
- If phase 0 finds an out-of-tree `FLEET_MIN_BACKOFF` producer: stop and
  design-block (see phase 0) — do not "fix" by keeping the read.
