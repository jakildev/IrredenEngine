# Native stack link

After opening a PR whose base is a feature branch (any stack mode),
register the parent→child relationship as a native GitHub stack. GitHub
then owns base management: when the parent merges the child retargets and
rebases server-side, cascade rebases are one `gh stack sync`, and merges
couple bottom-up so a child never lands on a stale base. Design:
[`docs/design/native-stacked-prs-migration.md`](../../../../docs/design/native-stacked-prs-migration.md).

Run after the PR exists, from inside the repo the PR belongs to (the
`{owner}/{repo}` placeholders resolve from the working directory's
remote). Idempotent:

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

- Requires the `gh-stack` extension (`gh extension install
  github/gh-stack`; `scripts/fleet/install.sh` bootstraps it). Exit code 9
  means Stacked PRs is not enabled for the repo — surface to the human,
  don't retry.
- A skipped link (parent PR missing, extension unavailable) leaves an
  ordinary branch-based PR the merger's legacy stacked handling services;
  prefer fixing the link, and surface the skip in your report.
- Never write `Stacked on:` / `Full chain:` body lines and never add a
  stack label — membership is the server object (`baseRefName !=
  "master"` + the PR header's stack badge).
- Never run the legacy re-stack (`gh pr edit --base master` after a parent
  merge) on a linked PR — GitHub already did it with the merge.
