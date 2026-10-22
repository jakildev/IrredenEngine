# Tier-0 auto-merge lane

**Status:** shipped dormant; first wave (`rebless`, engine repo) enabled by
the human after the operator dry-run; `docs` and the game repo wait for the
one-week reading.

The fleet's merge authority has exactly two holders: the human's click, and
`scripts/fleet/fleet-automerge`, a deterministic script with a fixed
predicate that the dispatcher runs as the first rung of the merger ladder.
No LLM role merges: `gh pr merge` and every spelling of the executor are
forbidden for every role in both runtime adapters (Claude `deny` list,
generated Codex `forbidden` rules). This document is the contract;
`fleet_merge_class.py` is its executable form and the suites under
`scripts/fleet/tests/test_fleet_merge_class.py` and `test_fleet_automerge.py`
pin every arm.

## Path classes

A PR's **complete** live changed-file set (`pulls/<N>/files`, paginated, and
its count checked against `changedFiles`) classifies as one of
`auto:rebless`, `auto:docs`, `auto:mixed`, `human`:

| class | every path matches | status |
|---|---|---|
| `rebless` | `creations/demos/*/test/references/**`, `docs/pr-screenshots/**` | first wave |
| `docs` | `*.md`, excluding `.claude/**`, `docs/agents/**`, `docs/design/objectives/**` and every `CLAUDE.md` basename | dormant |

One path outside the union is `human`; a set drawing on both classes is
`auto:mixed` and needs both enabled. A non-Markdown file under
`docs/design/**` is never `docs`. The dormant class stays represented in the
fixture so enabling it is a configuration change, not a code change.

## Gates (all live, all required, read immediately before the merge)

1. Open, not a draft, base exactly `master` (stacked children wait for the
   server-side retarget), `mergeable == MERGEABLE`.
2. The canonical label predicate (`label_block_reason`): `fleet:approved`
   present; refuses every `human:*` label (`human:hold` included), every
   `fleet:design-*`, every live `fleet:claim-*` / `fleet:amending-*` /
   `fleet:resolving-*` / `fleet:reviewing-*` claim, and the exact set that
   unions `fleet-state-scout::_MERGER_PROJECTION_SKIP_LABELS`,
   `fleet-rebase::SKIP_LABEL_RE`, the scout's LLM skip set, and the lane's
   own holds (`fleet:has-nits`, `fleet:needs-opus-recheck`,
   `fleet:changes-made`, `fleet:awaiting-upstream-review`,
   `fleet:human-amending`, `fleet:stalled`, `fleet:merger-cooldown`,
   `fleet:sweep-cooldown`, `fleet:auto-merged`). A drift test fails when
   either existing park set gains a label the predicate lacks. Smoke debt
   (`fleet:needs-*-smoke`) and `fleet:human-deferred` do not block.
3. A non-empty check rollup with every `CheckRun` `COMPLETED` and `SUCCESS`
   or `SKIPPED` and every `StatusContext` `SUCCESS`; `NEUTRAL`, pending and
   failed refuse. This is the full live rollup, not the scout's reduced
   human-facing signal.
4. The path class is enabled (above).
5. Approval provenance: a non-empty review on the current head, or a
   completed `Auto re-review` workflow run on the head while `fleet:approved`
   is still live (the mechanical-rebase and docs-only pushes that workflow
   keeps the label across), plus the newest non-empty review.
6. The hold window: `fleet:approved`'s newest `labeled` event is at least
   `FLEET_AUTOMERGE_HOLD_SECS` old; no event in the history refuses. Default
   0.
7. The kill switches are absent: `~/.fleet/automerge.off` on the host, and
   `human:hold` on the PR (covered by gate 2; never removed by an agent).

A missing, empty or unreadable field refuses. A refusal is logged with the
decisive reason and the class; `--dry-run` prints the same partition for the
whole merge queue and writes nothing.

## Enablement

`FLEET_AUTOMERGE_CLASSES` and `FLEET_AUTOMERGE_HOLD_SECS` in
`~/.fleet/fleet-up.conf` are the only inputs. The executor parses the file
itself; it reads no environment variable and takes no class flag, and an
unknown class, a malformed hold, or a knob value that is not one quoted or
bare shell word (an unterminated quote, text glued to a closing quote, a
second word) enables nothing. The dispatcher spawns
`fleet-automerge --auto --repo jakildev/IrredenEngine` ahead of
`fleet-rebase` in one background chain joined by `;`, so a refusal, failure
or disabled lane still hands the tick to tier-0's rebase pass; a state-dir
lock serializes runs. A disabled lane logs once (`.automerge-disabled-logged`)
until a class is enabled.

## Merge and audit

The merge is `gh pr merge --squash --delete-branch --match-head-commit
<classified head> --subject <PR title> --body-file <PR body>`; at most one
per run. On success the lane appends a `merge` event to
`~/.fleet/logs/automerge-audit.log` **before** any audit step, then stamps
`fleet:auto-merged`, posts one comment (`Auto-merged (class=…): approved by
<login> on <sha>[, auto-rereview kept fleet:approved on <head>], checks
green, hold <N>s elapsed.`) and runs `fleet-nit-close-for-pr`. A failed step
appends an `audit-error`; the next run completes the missing steps
idempotently (label present? comment present?) and appends `audit-complete`,
never merging again; three failed attempts leave the audit to a human. A
refusal by GitHub appends `merge-failed` and is not retried that run.

Ledger events (JSONL, one object per line, `ts` first): `merge` (`repo`,
`pr`, `class`, `head`, `merge_sha`, `title`, `provenance`, `hold_secs`),
`audit-complete` (`steps`, `nit_closed`), `audit-error` (`step`, `error`),
`merge-failed`, `rollback` (`pr`, `revert_pr`, `merge_sha`,
`revert_merge_sha`).

## Rollback oracle

A rollback is a merged, master-based GitHub revert PR whose title begins
`Revert "` and whose body contains `Reverts <slug>#<auto-merged PR>`.
`fleet-automerge --reconcile` joins such PRs to merge events and appends one
`rollback` per auto-merged PR, idempotently (a second revert of the same PR
adds nothing); a lookalike title without the backlink,
or a backlink without the title, does not count. A manual rollback must carry
the backlink to count.

## Reporting

`fleet-digest-tick` runs the reconcile pass, pins the ledger end, runs
`fleet-decisions` (which renders `## Auto-merged since last digest` from the
tick's cursor to that end and moves nothing), and advances
`~/.fleet/digest/automerge-cursor` only after the digest lands, so every
merge and rollback renders in exactly one successful digest. `fleet-health`
reports `enabled`, runs, refusals, merges, rollbacks and partial audits for
its window and warns on a rollback or an incomplete audit.

## Rollout and widening

Merging the implementation enables nothing. The lane's `gh` calls run under
the dispatcher's identity, the fleet's GitHub App where one is configured
([`FLEET-RUNTIME.md`](../agents/FLEET-RUNTIME.md) § GitHub App), so before
the first enablement the App needs **Contents: read & write** (the squash
merge and the branch delete); without it every attempt lands as a
`merge-failed` event and nothing merges. The human reviews the operator
dry-run, then sets `FLEET_AUTOMERGE_CLASSES="rebless"`. After one enabled
week, `fleet-health --since 7d` must read zero rollbacks and zero partial
audits before `docs` or the game repo is considered; `docs` additionally
needs the instruction-tier exclusions above re-confirmed. The labels are
`fleet:auto-merged` (permanent audit trail; refuses a second pass) and
`human:hold`.

## Consumers

`scripts/fleet/fleet-automerge`, `fleet_merge_class.py`, `fleet-dispatcher`
(merger tick), `fleet-digest-tick`, `fleet-decisions`, `fleet-health`,
`fleet-labels` / `fleet-state-machine.json`, `.claude/settings.json` and
`fleet_codex_policy.py` (denials), `docs/agents/FLEET.md` § Who merges,
`REVIEWER-PROTOCOL.md` (approval is the merge decision for an enabled class),
`CODEX.md` (adapter boundary), `CLAUDE-BASELINE.md` (hard rule).
