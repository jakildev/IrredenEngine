# Cross-lane claim mutex for amending and reviewing

- **Issue:** #3107
- **Model:** opus

## Outcome

Make `fleet:amending-*` and `fleet:reviewing-*` a real cross-lane mutex. The first sequential claimant keeps the PR, a simultaneous dead heat resolves deterministically, and an incumbent re-acquiring its own label succeeds without mutating GitHub state.

## Locked decisions

1. Add a symmetric `FLEET_CLAIM_EXCLUDES` table beside the lane vocabulary in `scripts/fleet/fleet-common.sh`. This issue registers only `amending` x `reviewing`; the resolving lane remains #3001's scope.
2. Keep the pre-acquire live-label check as the fast, no-mutation refusal path, generalized as `check_no_foreign_lane_claim`.
3. Make correctness independent of that GET by arbitrating over the POST response's union of same-prefix contenders and excluded-prefix contenders with a different host/agent suffix. Untabled prefixes retain their existing behavior.
4. Keep stale-holder sweeping same-prefix only. Cross-lane stale labels remain `cleanup --gh`'s responsibility.
5. If the caller's own lane label is already present, return success before the gate and POST, refresh its liveness marker, and warn if a foreign excluded-lane label also exists.
6. Preserve the same-agent carve-out: two excluded prefixes with the same host/agent suffix may coexist. Different-agent amending/reviewing pairs are unreachable through the fixed claim commands.
7. The exclusion table must be symmetric, enforced by test. A dead heat uses the existing lexicographic arbitration, so `fleet:amending-` wins a true tie.

## Implementation

- Add the exclusion table and pure contender-filter helper.
- Route `_acquire_label_on` through the union filter while keeping exhaustion cleanup scoped to its own prefix.
- Generalize the foreign-lane gate and wire both `review-claim` and `amending-claim` through one live-label fetch, including incumbent pass-through.
- Add a hermetic `test_fleet_claim_cross_lane.sh` using the real CLI and a stateful `gh` stub. Its concurrent arms use a portable `mkdir` lock and barriers to release amend first, review first, and both together.
- Reconcile the claim-time and sole-holder guarantees in the reviewer, runtime, feedback, label-reference, fleet, and fleet-authoring docs.
- Preserve the disjoint edits from PRs #3081 and #3105 if they land before this branch rebases.
- Preserve `FLEET_TARGET_CLAIM`'s shared `review-claim` mapping: smoke claims should stand down for a foreign amend; plan-review issue claims are unaffected because amend claims target PRs.

## Acceptance

The new suite distinguishes fix arms from existing-behavior controls:

- **Fix:** foreign amend makes `review-claim` exit 1 without a POST or label mutation.
- **Control:** the same suffix across amend/review remains allowed.
- **Fix:** an incumbent amend re-acquire succeeds without POSTing even if a foreign review label appeared later, and refreshes its liveness marker.
- **Control:** a non-incumbent amend remains refused by a foreign review label.
- **Fix:** each barrier release order leaves exactly one successful claimant and exactly one different-agent lane label; pre-fix produces two successes and two labels.
- **Fix:** the pure union filter includes same-prefix labels and foreign-suffix excluded labels, excludes same-suffix excluded labels and unrelated prefixes, and preserves untabled-prefix behavior.
- **Fix:** shipped symmetry passes and an injected asymmetric table fails.

Run `fleet-positive-control scripts/fleet/tests/test_fleet_claim_cross_lane.sh origin/master`; it must report MEANINGFUL with only the new behavior arms red on master. Keep `test_fleet_claim_acquire.sh`, `test_fleet_claim_host_gate.sh`, and `test_fleet_claim_planning.sh` green. Run `bash scripts/fleet/tests/run_all.sh`; it must be no worse than master's known baseline.

## Gotchas

- The pre-acquire GET alone is a TOCTOU; POST-response arbitration over a symmetric lane union is load-bearing.
- Compare the excluded label's full host/agent suffix to preserve the same-agent carve-out.
- Pass-through must precede both the gate and POST, and must reuse the gate's single fetch.
- The concurrency fixture must be portable across macOS, Linux, and MSYS2: no `flock` or Python `fcntl`.
- Existing acquire-suite T9-T11 use tabled canonical prefixes but remain controls because their fixtures contain no excluded-prefix contender; only T1-T8 use a synthetic untabled prefix.
