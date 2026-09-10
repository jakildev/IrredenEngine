# Plan: guard reviewer verdict stamps with the landed review body

- **Issue:** #2843
- **Model:** opus
- **Outcome:** Every documented reviewer verdict uses `fleet-review-verdict`, and that wrapper refuses to mutate labels unless a submitted review pins the PR's current head.

## Locked decisions

- Qualify a review by `reviews[].commit_id == headRefOid`, excluding `PENDING`; timestamp ordering is diagnostic only because author/committer clock skew and old-authored commits make it an unsafe coverage predicate.
- Re-fetch reviews once after a short delay to tolerate GitHub read-after-write lag, then exit 5 without delegating when no qualifying review exists.
- Fail closed with exit 1 when the head or reviews query fails.
- Add `--no-review-check` only for the protocol's fenced label-fixup lane. Reviewer role docs do not advertise that escape; exit 5 tells reviewers to post the missing body and retry.
- Apply the gate with and without `--agent`; retain the existing claim guard and worktree-scope behavior.
- Replace raw reviewer verdict-label mutations in both reviewer roles and `.claude/skills/review-pr/SKILL.md`; leave smoke-worker and historical quotations alone.

## Implementation

1. Extend `scripts/fleet/fleet-review-verdict` with current-head lookup, paginated review lookup, retry, explicit bypass, and documented exit status 5.
2. Extend `scripts/fleet/tests/test_fleet_review_verdict.sh`'s argument-faithful `gh` stub and refresh existing positive fixtures. Preserve T1-T12 and add T13-T17 for current-head success, stale/pending refusal, empty refusal, bypass, and API failure.
3. Update `docs/agents/REVIEWER-PROTOCOL.md` and `docs/agents/skills/review-pr.md` to route the verdict edge through the wrapper.
4. Update both reviewer role docs and `.claude/skills/review-pr/SKILL.md` consistently. Commit gated role files and skill files separately so a commit-time self-config gate can be handed off precisely if it blocks.
5. Re-run the live head-pinning census for verdict-labeled open PRs in both repositories and report it in the PR body.

## Acceptance criteria

1. No reviewer-verdict instance of `--add-label "fleet:approved"` remains under `.claude/commands/` or `.claude/skills/review-pr/`; sanctioned smoke-lane and historical-quotation matches are identified separately.
2. Without `--no-review-check`, the wrapper refuses with exit 5 and performs no delegation or writes when no submitted review pins the current head, on both fleet and interactive paths. Review-read errors fail closed with exit 1, and the bypass restores the prior fenced behavior.
3. T1-T12 remain green and T13-T17 cover the new gate, including a positive control showing stale-head fixtures fail against the pre-change implementation.
4. `bash scripts/fleet/tests/test_fleet_review_verdict.sh` and `env -u FLEET_PLAN_ISSUE bash scripts/fleet/tests/run_all.sh` produce no new failures compared with a same-host master baseline.

## Escalation conditions

- If a `.claude/commands/role-*.md` or `.claude/skills/**/SKILL.md` commit is rejected by the self-config gate, retain the committed script/docs work, change `Closes #2843` to `Refs #2843`, post the exact residual patch to the issue, park it as `fleet:needs-human`, and release the claim.
- Escalate for design only if GitHub's review payload cannot reliably expose current-head commit identity; do not weaken the gate to timestamp-only matching.

## Verification notes

- Run `fleet-positive-control scripts/fleet/tests/test_fleet_review_verdict.sh <pre-change-ref>`.
- Run the focused suite alone, then the full fleet suite with `FLEET_PLAN_ISSUE` unset.
- Compare the full-suite changed failure set against a baseline from `origin/master` on this host.
