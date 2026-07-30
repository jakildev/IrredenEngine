# Native stacked PRs — migration design

Adopt GitHub's native Stacked PRs for stack *mechanics* (base management,
retarget-on-merge, cascade rebases, coupled merges) and retire the fleet's
self-built retargeting machinery, keeping only the fleet-owned *policy* layer
(which task may stack on which PR, claims, review gating).

Companion to [`fleet-queue-stacking.md`](fleet-queue-stacking.md) (the
self-built system this supersedes the mechanics half of) and
[`../agents/FLEET.md`](../agents/FLEET.md). This doc owns the migration plan
and the evaluation evidence; once Phase 2 lands, `fleet-queue-stacking.md`
remains as history with a superseded header.

## Why

The self-built system needs agents to retarget child PRs after a parent
merges, workers must wait for those retargets, and the mechanics produced a
recurring failure class — most seriously a child staying `MERGEABLE` against
a merged base so a human merge lands content on an unreachable branch
(PR #558), plus the inherited-prefix rebase bugs (#1791/#1690/#1824), the
collapsed-merged-ref churn loop (#2447), and stale `Stacked on:` body markers
misrouting review (#2231).

GitHub's native Stacked PRs (private preview, April 2026) make that whole
class structurally impossible:

- **Coupled merges** — merging any PR merges it and every unmerged PR below
  it, bottom-up. A child can never merge into a stale parent branch, and
  `Closes #N` always fires because children only ever merge into `master`.
- **Automatic retargeting** — after a partial merge, remaining PRs rebase
  onto trunk server-side, synchronously with the merge.
- **Server-side cascade rebase** — UI "Rebase stack" button, `gh stack
  rebase` / `gh stack sync` from the CLI, with first-party squash replay
  (`git rebase --onto` semantics), replacing the inherited-prefix drop.
- **Stack state is a server object** — `GET repos/<slug>/stacks`; no body
  markers, no labels needed to mark membership.

## Evaluation evidence (2026-07-30)

Verified live on this account before adoption:

- The preview is **enabled on both repos** (engine and game): the stacks
  REST endpoint responds on each, and a probe stack (#2644, from draft PRs
  #2642/#2643, fully cleaned up afterward) was created with `gh stack link`,
  rendered the stack badge in the PR UI even for signed-out visitors, and
  exposed `{number, base.ref, open, pull_requests[…]}` via the API.
- **Trunk is always the repo default branch** — `gh stack link`
  force-corrected a bottom PR that targeted a non-default branch to
  `master`. Stacks cannot target arbitrary branches.
- Draft PRs are accepted as stack members.
- The `gh stack` CLI is automation-grade: `--yes` / `--auto` non-interactive
  modes, `view --json`, documented exit codes (3 = rebase conflict, 8 =
  stack locked, 9 = not enabled). `gh stack link` builds a stack from
  *existing* branches/PRs with no local tracking state — the right shape for
  stateless fleet worktrees (avoid the `init`/`add` flow, which keeps state
  in `.git/gh-stack`).

Constraints that matter here: preview status (Team/Enterprise SKU on the
roadmap — the rollback argument, see Phase 3), auto-merge and rule bypass
unavailable for stacked merges (no impact: human merges only), same-repo
branches only (no impact: stacks were always single-repo), 100-PR cap per
stack (no impact), and the behavior when a mid-stack PR is **closed
unmerged** is undocumented (confirm on the first real stack; keep a slim
merger policy note either way).

## What is replaced vs. kept

| Concern | Self-built | After migration |
|---|---|---|
| Stack membership | `Stacked on:` PR-body line + `fleet:stacked` label | GitHub stack object (`gh stack link`) |
| Retarget on parent merge | merger steps 2.5/5a.5, `fleet-rebase` retarget block | native, synchronous with merge |
| Cascade rebase | merger step 2.6, `cmd_molecule_rebase_downstream` | `gh stack sync` / UI button |
| Inherited-prefix drop | `fleet-rebase` `--onto` machinery | native squash replay |
| Merge safety | coupled-merge guard rails, `fleet:awaiting-base` parking | native bottom-up coupled merges |
| Base-ancestry gate (#2447) | `missing_ancestor_reason()` + memo | retired — native sync self-heals |
| Stack *eligibility* (may task N stack on PR P?) | scout `enrich_stackable_blocker_prs`: file-area overlap + `unsafe_base_reason` label safety | **kept** (minus the ancestry filter) |
| Claim bookkeeping | `fleet-claim --stackable-on`, `.meta` sidecar, `claim-base`, molecules | **kept** |
| Review gating (child waits for parent review) | `fleet:awaiting-upstream-review` protocol | **kept as policy** (native UI already scopes per-PR diffs) |
| Epic `**Blocked by:**` issue-graph validation | `fleet_validate_stack.py` et al. | **kept untouched** (issue graphs, not PR mechanics) |

Labels: `fleet:awaiting-base`, `fleet:needs-base-update`, `fleet:stacked-rebase`,
and `fleet:stacked` retire with the mechanics. `fleet:fork-of-other-pr` is
mostly obsolete (decide during drain). `fleet:awaiting-upstream-review` stays.

## Migration phases

**Phase 0 — pilot.** Extension installed on the migration host; the other
fleet hosts get it via `scripts/fleet/install.sh` (Phase 1). The migration
PR chain itself is the pilot: it is linked as a native stack, so the first
human merge of it confirms coupled-merge + auto-retarget behavior live.

**Phase 1 — creation switches to native; legacy becomes fallback.** The
`commit-and-push` stacking procedures and `start-next-task` create stacks
via `gh stack link` (idempotent append) instead of `Stacked on:` body lines
+ `fleet:stacked`. The merger's stacked steps gain a scoping guard — skip
any PR that belongs to a native stack (one stacks-API call) — so the two
systems cannot fight; legacy machinery keeps serving only pre-migration
stacks. `classify-auto-rereview.sh` learns to recognize native cascade
force-pushes (content-identical rebases) so reviewers don't re-review every
cascade — this must land before the first native cascade fires.

**Phase 2 — retire the mechanics.** Once in-flight legacy stacks drain
(at time of writing: one per repo), remove the merger's stacked steps, the
`fleet-rebase` retarget/prefix-drop block, `cmd_molecule_rebase_downstream`,
the ancestry gate + memo, and the four dead labels; swap the scout's
stacked signals to the stacks API.

**Phase 3 — archive, don't delete.** The retired *scripts* move to
`scripts/fleet/legacy/stacked-prs/` with a README recording why they were
retired, the `pre-native-stacks` tag, and the exact re-enable steps
(labels are one `fleet-labels` run; merger steps and skills restore from
the tag). Doc sections are deleted outright — `fleet-queue-stacking.md`
survives as the historical *why* with a superseded header. Rollback trigger:
the preview being pulled or paywalled.

## Risks

1. **Preview status** — works on this personal account today; roadmap
   suggests Team/Enterprise SKU at GA. Mitigated by Phase 3's archive +
   tag; nothing in the native flow writes fleet-side state that a revert
   would corrupt.
2. **Auto-rereview churn** — server-side cascades force-push children with
   unchanged content; the old recognizer keyed on `fleet:stacked-rebase`,
   which dies. The Phase 1 classifier guard replaces it (actor +
   changed-file-set drift check, the same verification `fleet-rebase` used).
3. **Heuristic inversion** — "`base != master` ⇒ stacked/shipped-into-parent"
   reasoning in scout signals (and operator lore) inverts once children
   always retarget to `master` before merging. The scout swap is Phase 2;
   until then the merger scoping guard keeps legacy interpretation away
   from native stacks.
