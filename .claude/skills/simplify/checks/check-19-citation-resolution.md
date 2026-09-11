# Check 19 — citations must resolve — at the PR's base, via the right resolver

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** added lines carry
`docs/**.md` paths, `§<id>` section citations, or bare `#<N>` GitHub
references (any changed file type).

Two citation classes a worktree grep cannot verify, in every changed file
type (source comments and runtime strings carry the most durable
citations; §9a's markdown gate never reaches them):

- **`docs/**.md` paths and `§<id>` section citations** on `+` lines must
  resolve at the PR's own base (`git show origin/master:<path>`), not in
  the worktree — a citation that resolves only because a sibling *open*
  PR adds the heading is a merge-order fuse. A true forward reference is a
  stacking decision; surface it as one.
- **Bare `#<N>` GitHub citations** on `+` lines of changed markdown:
  resolve with `gh issue view <N> --json title,state` (fall back to `gh pr
  view`). A 404 is a dead link, and the resolved title must match the
  claim the surrounding prose makes about it — print the title beside the
  citing sentence and check the subject.
