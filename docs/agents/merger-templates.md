# Merger comment templates

Verbatim `.merger-body.md` bodies for
[`role-merger.md`](../../.claude/commands/role-merger.md), which keeps the
trigger and the `Write` → `gh pr comment --body-file .merger-body.md` step
inline. Substitute the `<angle-bracket>` fields and keep the
`— fleet merger` sign-off (the audit trail identifies merger comments by
it). Short bodies stay inline in the role doc.

## § fork-of-other-pr (role-merger step 5a.6)

Post this only when `fleet-pr fork-of <N> --repo <engine|game>` exits 0
with a `fork-of:` line that is not `fork-of: none`; run it now if step
a.6 has not. A per-PR `git merge-base --is-ancestor` hit the tool does not
confirm is not a fork. `<fork-of-line>` is that line verbatim, and
`<upstream-N>`, `<upstream-headRefName>` and `<upstream-tip-sha>` are its
three fields.

```
Merger: this PR's branch was forked from open PR #<upstream-N>
(`<upstream-headRefName>`) but targets `<baseRefName>`. Its diff
carries inherited commits from that PR — rebasing onto
`<baseRefName>` would replay them and conflict.

`fleet-pr fork-of <N>` reports:
<fork-of-line>

Two clean resolutions:

1. Make the dependency explicit — turn this into a native stack
   (re-bases this PR onto #<upstream-N> so GitHub manages the
   chain):
     gh pr edit <N> --base <upstream-headRefName>
     gh stack link <upstream-N> <N>

2. Or drop the inherited commits and stay independent:
     git fetch origin
     git rebase --onto origin/<baseRefName> <upstream-tip-sha> <this-headRefName>
     git push --force-with-lease

Labeled `fleet:needs-info` — the human picks the resolution.

— fleet merger
```

## § semantic-conflict (role-merger step 5d case ii)

The `SHA pair:` line is parsed by the step 5d dedup check (same master-tip
× PR-head pair → comment skipped; new pair → re-posted), so it stays the
last line before the sign-off. Cap the file list at 5 and append
`… and N more`.

When `fleet-pr fork-of <N> --repo <engine|game>` printed a `merged-parent:`
line that is not `merged-parent: none` (run it now if step a.6 has not),
insert the **merged-parent paragraph** below, followed by one blank line,
immediately above the `SHA pair:` line. `<merged-parent-line>` is that line
verbatim; `<merged-N>` and `<boundary-sha>` are its two fields. Omit the
paragraph on `merged-parent: none`.

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

Merged-parent paragraph:

```
Merged parent: this branch still carries commits from merged
PR #<merged-N>, which a plain rebase replays. `fleet-pr fork-of <N>`
reports:
<merged-parent-line>

Rebase only this PR's own commits:
  git fetch origin
  git rebase --onto origin/<baseRefName> <boundary-sha> <this-headRefName>
```
