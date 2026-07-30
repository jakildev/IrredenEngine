# Legacy self-built stacked-PR machinery — archive + re-enable guide

The fleet's self-built stacked-PR *mechanics* (agent-driven retargeting,
cascade rebases, coupled-merge guard rails, the base-ancestry gate) were
retired in favor of GitHub's native Stacked PRs. Policy surfaces survived:
`unsafe_base_reason` stack-base eligibility, `fleet-claim --stackable-on` /
`claim-base` / molecule bookkeeping, the reviewer's
`fleet:awaiting-upstream-review` gate, and the epic `Blocked by:` graph
validation. Why + evaluation evidence:
[`docs/design/native-stacked-prs-migration.md`](../../../../docs/design/native-stacked-prs-migration.md);
history of the self-built design:
[`docs/design/fleet-queue-stacking.md`](../../../../docs/design/fleet-queue-stacking.md).

**The archive is the git tag `pre-native-stacks`** — the last commit where
every retired block was live. Nothing here is dead code kept on the tree;
this README is the map back to it.

## What was retired, and where it lived at the tag

| Block | Home at `pre-native-stacks` | Native replacement |
|---|---|---|
| Tier-0 retarget-to-master + inherited-prefix drop (#1690/#1824) | `scripts/fleet/fleet-rebase` (base-resolution + `rebase --onto` blocks) | server-side retarget+rebase on parent merge |
| Merger label-independent base reconcile (PR #558 guard) | `.claude/commands/role-merger.md` step 2.5 | coupled merges make the failure impossible |
| Merger cascade rebase on upstream tip move | `.claude/commands/role-merger.md` step 2.6 | `gh stack sync` / UI "Rebase stack" |
| Merger base-merged retarget + `Stacked on:` strip (#1149/#2231) | `.claude/commands/role-merger.md` step 5a.5 ii | server-side; no body markers exist |
| Merger fork-of-other-PR detection | `.claude/commands/role-merger.md` step 5a.6 | stack membership is explicit (stack object) |
| In-molecule downstream cascade | `scripts/fleet/fleet-claim` `cmd_molecule_rebase_downstream` | `gh stack sync` |
| Base-ancestry containment gate (#2447) | `fleet_stack_base.missing_ancestor_reason` + scout `_ancestry_ref_graph`/`_fetch_contains`/memo + `fleet-claim` inline gate | `gh stack sync` self-heals a base missing a merged blocker |
| Scout "stacked-pending" merger signal | `scripts/fleet/fleet-state-scout` `_merger_action_signal` | none needed (merger has no stacked action) |
| Labels `fleet:stacked`, `fleet:awaiting-base`, `fleet:needs-base-update`, `fleet:stacked-rebase`, `fleet:fork-of-other-pr` | `scripts/fleet/fleet-labels`, `docs/agents/fleet-labels-reference.md`, `docs/agents/fleet-state-machine.json` | stack object + `fleet:awaiting-upstream-review` (kept) |
| `Stacked on:` / `Full chain:` PR-body markers | `commit-and-push` procedures + `docs/agents/FLEET.md` | stack badge / stacks API |

## Re-enable (rollback) steps

Trigger: the preview is pulled, paywalled, or changes incompatibly.

1. `git diff pre-native-stacks..<retirement-merge> -- <file>` shows exactly
   what to restore per file above; revert the retirement commits or
   cherry-pick the blocks back.
2. Labels are one run: restore the five entries in
   `scripts/fleet/fleet-labels` (+ the reference doc and state-machine JSON —
   three-way sync) and run `fleet-labels` against both repos.
3. Restore the merger steps + procedures from the tag; the skills' stack
   modes revert from the same diff.
4. Nothing in the native flow writes fleet-side state that a revert
   corrupts: stack objects on GitHub can be dropped per stack with
   `gh stack unstack <n>`; open native-stacked PRs keep working as ordinary
   branch-based PRs that the restored legacy machinery services.
