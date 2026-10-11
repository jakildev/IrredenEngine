# Stacked PR review

Shared-flow step 1c. `baseRefName != master` is the signal; the chain
lives in GitHub's stack object (the PR header's stack badge), and a legacy
`Stacked on:` body line is confirmation only. A `master`-based PR that
still carries a `Stacked on:` line was un-stacked and missed the strip —
review it as standalone and flag the stale line as a nit.

## Merged-parent gate

Before approving a non-`master` child, run this check with its live base and
number. Exit 1 means needs-fix: the child must be replayed, pushed, and only
then retargeted.

```bash
base=<the child's baseRefName>
child_pr=<the child PR number>
parent_rows=$(gh pr list --head "$base" --state all \
    --json number,state,headRefOid)
open_parent=$(jq -r '[.[] | select(.state == "OPEN")][0].number // empty' \
    <<< "$parent_rows")
merged_parent=$(jq -r '[.[] | select(.state == "MERGED")][0].number // empty' \
    <<< "$parent_rows")
stack_pages=$(gh api --paginate --slurp "repos/{owner}/{repo}/stacks")
stacked=$(jq -r "any(.[][] | select(.open) | .pull_requests[]; .number == ${child_pr})" \
    <<< "$stack_pages")
if [[ -z "$open_parent" && -n "$merged_parent" && "$stacked" != "true" ]]; then
    parent_sha=$(jq -r ".[] | select(.number == ${merged_parent}) | .headRefOid" \
        <<< "$parent_rows")
    echo "not approvable: merged parent #${merged_parent}; run git rebase --onto origin/master ${parent_sha}, push, then gh pr edit --base master" >&2
    exit 1
fi
echo "approvable"
```

For a stacked PR:

- Review only this PR's own diff — `gh pr diff <N>` already scopes to its
  changes on top of the base.
- Note the context in the body: "Stacked on #<parent>; approval assumes
  #<parent> lands first."
- Never read, cite, or re-verify the parent's diff; it has its own review
  and label.
- Flag upstream interface fragility: for each new symbol depending on an
  upstream one, "if `<upstream-symbol>` changes between approval and
  merge, this downstream needs a rebase."
- Verdict and label are for this PR alone. The step-5b swap also clears
  `fleet:awaiting-upstream-review`, which keeps an approved child from
  pulling an unapproved parent in via a coupled native-stack merge.

Then return to step 1d.
