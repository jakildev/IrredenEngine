# Merger comment templates

Verbatim `.merger-body.md` bodies for
[`role-merger.md`](../../.claude/commands/role-merger.md), which keeps the
trigger and the `Write` → `gh pr comment --body-file .merger-body.md` step
inline. Substitute the `<angle-bracket>` fields and keep the
`— fleet merger` sign-off (the audit trail identifies merger comments by
it). Short bodies stay inline in the role doc.

## § fork-of-other-pr (role-merger step 5a.6)

`<upstream-tip-sha>` is `git rev-parse origin/<upstream-headRefName>` at
detection time.

```
Merger: this PR's branch was forked from open PR #<upstream-N>
(`<upstream-headRefName>`) but targets master. Its diff carries
inherited commits from that PR — rebasing onto master would
replay them and conflict.

Two clean resolutions:

1. Make the dependency explicit — turn this into a native stack
   (re-bases this PR onto #<upstream-N> so GitHub manages the
   chain):
     gh pr edit <N> --base <upstream-headRefName>
     gh stack link <upstream-N> <N>

2. Or drop the inherited commits and stay independent:
     git fetch origin
     git rebase --onto origin/master <upstream-tip-sha> <this-headRefName>
     git push --force-with-lease

Labeled `fleet:needs-info` — the human picks the resolution.

— fleet merger
```

## § semantic-conflict (role-merger step 5d case ii)

The `SHA pair:` line is parsed by the step 5d dedup check (same master-tip
× PR-head pair → comment skipped; new pair → re-posted). Cap the file list
at 5 and append `… and N more`.

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
