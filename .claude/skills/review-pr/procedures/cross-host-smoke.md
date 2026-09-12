# Cross-host smoke tagging

Shared-flow step 5c. After the verdict label, `gh pr diff <N> --name-only`;
the PR needs cross-host smoke if any path matches `engine/render/`,
`engine/prefabs/irreden/render/`, `engine/render/src/shaders/`, any
`*.glsl` / `*.metal`, `engine/system/**` (platform-conditional blocks), or
any `CMakeLists.txt` / `CMakePresets.json`. Lua-binding headers,
`engine/ecs/**`, and other "could theoretically break" categories are
deliberately excluded. No match → step 6.

## Tiers

- **OpenGL tier = {linux, windows}** — one clean build + `IRShapeDebug`
  smoke on either is sufficient; `windows` (the ship platform) is the
  canonical representative.
- **Metal tier = {macos}** — verified independently.

`commit-and-push` stamps `fleet:authored-on-<host>` from the author's
`uname -s`; authoring on a host baseline-validates that tier. Tag only the
other tier(s):

| Author label | Add |
|---|---|
| `fleet:authored-on-linux` / `fleet:authored-on-windows` | `fleet:needs-macos-smoke` |
| `fleet:authored-on-macos` | `fleet:needs-windows-smoke` |
| none | `fleet:needs-windows-smoke` and `fleet:needs-macos-smoke` (separate calls; each is idempotent) |

```bash
gh pr edit <N> --add-label "fleet:needs-macos-smoke"
gh pr edit <N> --add-label "fleet:needs-windows-smoke"
```

Each host's worker polls for its label, runs a clean-checkout build +
`IRShapeDebug` smoke, and removes the label on success; the human holds
the merge while any smoke label persists. The OpenGL requirement is
satisfied by either `fleet:verified-windows` or `fleet:verified-linux` (or
an OpenGL author), never both.

Skip game-repo PRs (the game repo's own `review-pr` procedure tags them)
and engine PRs matching none of the paths. Then return to step 6.
