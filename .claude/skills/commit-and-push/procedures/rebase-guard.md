# Rebase guard

Git's 3-way merge can silently drop hunks from a non-conflicting region of
a file whose other region conflicts — no markers, no warning. Before `git
rebase origin/master`, run `git diff origin/master` and keep the output in
your conversation context (no `>` redirects — the Bash tool blocks them;
a large diff auto-persists to a `<persisted-output>` you can Read).
Rebase, resolve conflicts, then run `git diff origin/master` again: any
`+` line present before and absent after is a dropped hunk — re-apply it
before committing.

If the pre-capture was skipped: confirm a rebase happened with `git reflog
--since=2.hours.ago`, compare `git diff origin/<branch-name>` against the
last pushed state, and look for blocks missing relative to the PR's commit
messages and description.
