# Stacked PR review

Procedure for reviewing a PR whose `--base` is another open PR's branch (a "stacked" PR), rather than `master`. This fires only when [`SKILL.md`](../SKILL.md) step 1 detects stacking; non-stacked PRs skip this entirely.

## Detection

Every fleet PR today is single-task. When workers claim a dependency chain via `fleet-claim stack`, they produce a sequence of single-task PRs where each one's `--base` points at the previous task's branch instead of `master` — GitHub calls these "stacked PRs". The review pass is still per-PR; you don't re-review the parent PR as part of reviewing its child.

Detect stacking from the metadata already fetched in `SKILL.md` step 1:

- **Base branch** (`baseRefName`) is not `master` → stacked on the PR whose head is that branch.
- **Body** contains a `Stacked on:` line → confirms it, and the line gives you the parent PR URL for the review-body callout.

If the base is `master` and there's no `Stacked on:` line, this is a standalone PR — return to `SKILL.md` step 1c (churn audit) and continue normally.

If the base is `master` but the body **still carries** a `Stacked on:` line, the marker is stale — the PR was un-stacked (parent merged, base re-targeted) and the strip step was missed. Review it as a standalone PR, and flag the stale line as a nit so it gets removed.

## What changes when the PR is stacked

- **Review only this PR's own diff.** `gh pr diff <N>` already scopes to the changes this PR introduces on top of its base branch — it does NOT include the parent's changes. Trust that output; don't manually expand the range.
- **Note the stack context in the review body**, e.g. "Stacked on #<parent>; approval assumes #<parent> lands first." Reviewers (and the human merger) use that line to sequence merges.
- **Do not read, cite, or re-verify the parent PR's diff.** It has its own independent review and label. Cross-contamination between stacked PRs' reviews is the main failure mode to avoid.
- **Verdict and label are set for this PR alone**, the same way as a non-stacked PR.
- **Flag upstream interface fragility.** For any new function, component, system, or shader this PR introduces that depends on an upstream symbol, note in the review body: "if `<upstream-symbol>` changes between approval and merge, this downstream will need a rebase." This alerts the human merger to sequence carefully.

After applying these adjustments, return to `SKILL.md` step 1c (churn audit) and continue the standard flow.

## Label-clearing implication

In `SKILL.md` step 5b, the verdict-label commands include `--remove-label "fleet:awaiting-upstream-review"` and `--remove-label "fleet:stacked-rebase"`. These removals matter for stacked PRs:

- `fleet:awaiting-upstream-review` — set by the merger when a stacked PR's parent isn't yet reviewed; cleared here as the review completes.
- `fleet:stacked-rebase` — set when a parent PR retargeted to `master` and a downstream needs a re-eval; cleared here as the re-eval completes.

Both removals are already in the standard verdict-label commands; no additional steps required for stacked PRs.
