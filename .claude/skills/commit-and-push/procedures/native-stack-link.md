# Native stack link (commit-and-push procedure)

After opening a PR whose base is a feature branch (any stack mode), register
the parent→child relationship as a **native GitHub stack**. From then on
GitHub owns base management: when the parent merges the child retargets and
rebases server-side, cascade rebases are one `gh stack sync` (or the UI's
"Rebase stack" button), and merges couple bottom-up — merging a child pulls
its unmerged parents in, so a child can never land on a stale base. Design +
evaluation evidence:
[`docs/design/native-stacked-prs-migration.md`](../../../../docs/design/native-stacked-prs-migration.md).

## The link step (idempotent)

Run after the PR exists (fresh `gh pr create` or the idempotent
`gh pr edit` reconcile), from inside the repo the PR belongs to — the
`{owner}/{repo}` placeholders resolve from the working directory's remote:

```bash
base=<the PR's base branch>            # already resolved by the stack mode
child_pr=<the just-opened PR number>
parent_pr=$(gh pr list --head "$base" --state open --json number -q '.[0].number')
if [[ -z "$parent_pr" ]]; then
    echo "native-stack-link: no open PR for base $base — skipping link" >&2
else
    stack=$(gh api "repos/{owner}/{repo}/stacks" \
        --jq "[.[] | select(.open) | select(any(.pull_requests[]; .number == ${parent_pr}))][0].number")
    if [[ -n "$stack" && "$stack" != "null" ]]; then
        gh stack link "$stack" "$child_pr"      # append to the parent's existing stack
    else
        gh stack link "$parent_pr" "$child_pr"  # create a new two-PR stack
    fi
fi
```

Skipping the link (parent PR missing, extension unavailable) is safe but
second-best: the PR still works as an ordinary branch-based PR and the
merger's legacy stacked handling services it. Prefer fixing the link over
falling back — surface the skip in your report.

## Notes

- `gh stack link` accepts PR numbers or branch names and auto-corrects
  mismatched bases; the child's base (the parent's branch) is already
  correct here, so the link is pure registration.
- Requires the `gh-stack` CLI extension (`gh extension install
  github/gh-stack` — `scripts/fleet/install.sh` bootstraps it). Exit code 9
  means Stacked PRs isn't enabled for this repo; surface to the human, don't
  retry.
- Do **not** write `Stacked on:` body lines or `Full chain:` lists — stack
  membership is a server object and the PR header's stack badge shows the
  chain. Body markers were the legacy mechanism and produced stale-marker
  misrouting (#2231).
- No stack label: membership is the stack object itself (`baseRefName !=
  "master"` + the PR header's stack badge). The legacy `fleet:stacked` label
  retired with the self-built machinery.
- Never run the legacy re-stack dance (`gh pr edit --base master` after a
  parent merge) on a linked PR — GitHub already did it, synchronously with
  the merge.
