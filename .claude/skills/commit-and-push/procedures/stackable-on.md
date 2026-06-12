# Single-task base resolution + `--stackable-on` (commit-and-push procedure)

**Single-task mode** is the default — it applies when neither
[fleet stack mode](fleet-stack.md) (an active `fleet-claim stack` chain) nor
[cursor stack mode](cursor-stack.md) (a `cursor-stack-base` git config) is in
effect. It covers two cases through **one** path:

- a **normal** fleet claim or a plain human PR → base is `master` (unchanged
  from the historic default);
- an opportunistic **`--stackable-on`** claim (the idle-worker fallback tier —
  a task whose single `**Blocked by:**` blocker has an open PR) → base is the
  blocker's branch, and the PR carries `fleet:stacked`.

The `--stackable-on` behavior lives entirely in what `fleet-claim claim-base`
returns, so there is nothing stackable-specific to remember: always resolve
the base the same way and let the value decide.

## Resolve the issue number, then the base

1. **Issue number** (needed for `claim-base`):
   - Primary: `fleet-claim reservation-of "<your-worktree-name>"` — the claim
     auto-reserves the worktree↔task binding, so this prints the issue number
     with no parsing.
   - Fallback: parse `<N>` from a `claude/<N>-<topic>` branch name.
   - Neither yields a numeric `<N>` (a plain human / ad-hoc PR with no fleet
     claim) → base is `master`, no label; proceed with the standard `SKILL.md`
     step-8 `master` flow.
2. **Base:**
   ```bash
   base=$(fleet-claim claim-base "<N>")   # "master" for a normal claim; the blocker's branch for --stackable-on
   ```
   `claim-base` is a no-op for normal claims (no `.meta` sidecar → prints
   `master`), so this is safe to run unconditionally once you have `<N>`.

## Open (or reconcile) the PR — idempotent

A fleet-worker WIP PR is usually **already open** (created at claim time, see
`docs/agents/FLEET.md` §"Single-task base resolution (`claim-base`)"). So
*ensure* the invariant rather than assume creation — repair base/label if the
claim-time open missed them:

```bash
branch=$(git branch --show-current)
labels=(--label "fleet:wip")
[[ "$base" != "master" ]] && labels+=(--label "fleet:stacked")

existing=$(gh pr list --head "$branch" --state open --json url -q '.[0].url')
if [[ -n "$existing" ]]; then
    gh pr edit "$existing" --base "$base"                       # no-op when already correct
    [[ "$base" != "master" ]] && gh pr edit "$existing" --add-label "fleet:stacked"
else
    gh pr create --base "$base" "${labels[@]}" \
        --title "<scope>: <title> (#<N>)" \
        --body "$(cat <<EOF
<single-task body — procedures/pr-body.md; include the Stacked on: line below when base != master>
EOF
)"
fi
```

When `base != master`, add a `Stacked on:` line to the body, pointing at the
upstream PR (look it up once — same pattern as cursor-stack.md):

```bash
upstream_pr=$(gh pr list --head "$base" --state all --json url -q '.[0].url')
# body: "Stacked on: ${upstream_pr:-$base (no PR yet)}"
```

## Notes / non-goals

- **No `stack-set-pr`.** That bookkeeping is molecule-only ([fleet-stack.md](fleet-stack.md)).
  Single-task mode writes no claim state — it only *reads* `claim-base`.
- **`fleet:wip` is unchanged.** The claim-time PR is WIP; the finalize step
  removes `fleet:wip` for reviewer pickup, leaving `fleet:stacked` in place.
- **Don't fight the merger.** When the upstream PR merges, the merger
  re-targets this PR to `master` and removes `fleet:stacked`. Only set the
  base/label from the *current* `claim-base` value at push time; never
  re-stack a PR the merger has already re-targeted.
- The merger routes stacked PRs by `baseRefName != "master"` regardless of the
  label; `fleet:stacked` is the human-visibility / cheap-filter convenience
  this procedure guarantees is present.
