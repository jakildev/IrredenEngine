# Fleet stack mode

When the worktree has a stack claim under `~/.fleet/claims/_stack_<agent>/`,
open **one PR per task, chained by `--base`**. The cursor-flow variant is
[`cursor-stack.md`](cursor-stack.md); the two are mutually exclusive.

## Detection

```bash
fleet-claim stack-pr-state <your-worktree-name>
```

`no stack claim for agent: <name>` → normal single-PR flow. A
`task`/`branch`/`pr` table → stacked: the current task is the row whose PR
column is `(pending)` with every earlier row filled. Note its
**`<task-id>`** (T-NNN — what `fleet-claim` takes) and its **`<issue#>`**
(for the branch name).

## Deltas

- Step 2 branch name: `claude/<issue#>-<short-topic>`.
- Step 8 base: `fleet-claim stack-base <agent> <task-id>` — `master` for
  the first task, the previous task's branch after.
- After `gh pr create`: `fleet-claim stack-set-pr <agent> <task-id>
  <branch> <pr-url>`, then [native-stack-link.md](native-stack-link.md)
  with the `stack-base` value (skip the link for the first task). No
  `Stacked on:` / `Full chain:` body lines and no stack label —
  membership is the native stack object (`baseRefName != "master"` + the
  PR header's stack badge).
- Title carries a scope prefix; the issue number goes in the `Closes #N`
  line.

Afterwards invoke `start-next-task` as usual; the next iteration computes
its own base via `stack-base`. When the final task's PR merges,
`fleet-claim release-stack <agent>` cleans up the per-task claims and the
stack metadata.
