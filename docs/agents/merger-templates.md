# Merger comment templates

Verbatim `.merger-body.md` bodies for [`role-merger.md`](../../.claude/commands/role-merger.md).
The role doc keeps each trigger and the `Write` → `gh pr comment
--body-file .merger-body.md` step inline; the template text lives here so
it isn't reloaded into every merger dispatch. Substitute the
`<angle-bracket>` fields; always keep the `— fleet merger` sign-off (the
human-visible audit trail identifies merger comments by it). Short
one-or-two-sentence bodies (awaiting-base, clean rebase, orphaned base)
stay inline in the role doc — only the long templates live here.

## § cascade-rebase-conflict (role-merger step 2.6 c, conflict branch)

```
Merger: cannot cascade-rebase onto updated base PR
#<base-pr-number>. Its head ref `<baseRefName>` was force-
pushed and the new tip conflicts with this PR's own
commits.

Resolution: the author of this PR (or the upstream author)
rebases manually onto the new upstream tip:

  git fetch origin
  git rebase origin/<baseRefName>
  # resolve conflicts, then:
  git push --force-with-lease

Labeled `fleet:needs-base-update` — the merger and the
cascade-rebase pass skip this PR until the label clears.
It clears automatically when the upstream merges (step
2.5 re-targets to master and removes the label) or
closes; otherwise the human or an opus+-class worker clears it
after a manual rebase.

— fleet merger
```

## § fork-of-other-pr (role-merger step 5a.6)

`<upstream-tip-sha>` is the output of
`git rev-parse origin/<upstream-headRefName>` captured at detection time.

```
Merger: this PR's branch was forked from open PR #<upstream-N>
(`<upstream-headRefName>`). Its diff carries inherited commits
from that PR and cannot be cleanly rebased onto master until
#<upstream-N> merges.

Resolution after #<upstream-N> merges:
  git fetch origin
  git rebase --onto origin/master <upstream-tip-sha> <this-headRefName>
  git push --force-with-lease

This drops #<upstream-N>'s inherited commits and leaves only
this PR's own changes on top of master.

Labeled `fleet:fork-of-other-pr` — the merger and worker
skip this PR in their conflict-resolution sweeps.

— fleet merger
```

## § semantic-conflict (role-merger step 5d case ii)

The `SHA pair:` line is load-bearing: the step 5d dedup check parses it
from the most recent merger comment to decide whether the conflict
recurred on the same master-tip × PR-head pair (comment skipped) or on a
new pair (full comment re-posted). Cap the file list at 5; if more files
conflict, append `… and N more`.

```
Merger: cannot auto-resolve mechanically. The PR has
semantic conflicts with current master that need
judgement-level resolution.

Conflicted files:
- `<file1>` — master: `<sha> <subj>`; PR: `<sha> <subj>`
- `<file2>` — ...

Labeled `fleet:semantic-conflict` — a worker will
attempt resolution on its next iteration (rebase,
manually resolve, build, push). If the worker also
can't resolve (truly ambiguous, design decision needed),
it will escalate to `human:needs-fix`.

The `fleet:approved` label has been removed if it was set
— the PR no longer represents a reviewed state.

SHA pair: master=<master-tip-sha> × PR=<pr-head-sha>

— fleet merger
```
