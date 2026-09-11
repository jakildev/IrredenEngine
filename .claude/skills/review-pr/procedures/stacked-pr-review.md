# Stacked PR review

Shared-flow step 1c. `baseRefName != master` is the signal; the chain
lives in GitHub's stack object (the PR header's stack badge), and a legacy
`Stacked on:` body line is confirmation only. A `master`-based PR that
still carries a `Stacked on:` line was un-stacked and missed the strip —
review it as standalone and flag the stale line as a nit.

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
