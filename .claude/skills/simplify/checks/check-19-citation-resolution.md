# Check 19 — citations must resolve — at the PR's base, via the right resolver

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** added lines carry `docs/**.md` paths, `§<id>` section citations, or bare `#<N>` GitHub references (any changed file type).

Two citation classes grep-against-the-worktree cannot verify, and both
apply to **every** changed file type (`.hpp/.cpp/.glsl/.metal/.lua/.py`,
not just `.md` — source comments and runtime strings carry the most
durable citations, and §9a's markdown gate never reaches them):

- **`docs/**.md` paths and `§<id>` section citations** on added (`+`)
  lines must resolve **at the PR's own base**
  (`git show origin/master:<path>`), not in the worktree — a citation that
  resolves only because a sibling *open* PR adds the heading is the same
  failure with a merge-order fuse (PR #2584 shipped six `§M-2` citations,
  one in a runtime error string, against a section only open PR #2579
  adds; an author who rebased locally would grep the worktree and conclude
  it fine). A true forward-reference is a stacking decision, not a typo —
  surface it as one.
- **Bare `#<N>` GitHub citations** on added lines of changed markdown name
  objects outside the tree: resolve with
  `gh issue view <N> --json title,state` (fall back to `gh pr view`). A
  404 is a dead link, and the resolved **title must match the claim the
  surrounding prose makes about it** — PR #2649 cited an unrelated polling
  PR for a stale-marker fix, and a second number resolved to nothing at
  all. Print the title beside the citing sentence and eyeball the subject
  match. (#2587, #2655)
