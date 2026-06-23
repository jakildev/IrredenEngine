# Cross-host smoke tagging

Procedure for tagging an engine PR for cross-host (OpenGL ↔ Metal, plus Windows-native build) validation after the verdict label is set. Fires when [`SKILL.md`](../SKILL.md) step 5c determines the diff touches code that can silently break on a host the author didn't build against; otherwise the PR skips this entirely.

## When this fires

Engine PRs get built and run on whichever host the author happened to be using (OpenGL on Linux, Metal on macOS, or Windows-native via MSYS2). The other hosts' build + smoke paths are not exercised until a fleet agent on that host picks the PR up. The `fleet:needs-<host>-smoke` labels surface that outstanding work so no PR with cross-host risk merges unvalidated on any host.

After setting the verdict label in `SKILL.md` step 5b, check the diff's file paths:

```bash
gh pr diff <N> --name-only
```

If any path matches **any** of these, the PR needs cross-host smoke:

- `engine/render/`
- `engine/prefabs/irreden/render/`
- `engine/render/src/shaders/`
- any `*.glsl` file
- any `*.metal` file
- `engine/system/**` — platform-conditional blocks (`#ifdef _WIN32`, `__APPLE__`, etc.) can silently work on one host and break on another
- any `CMakeLists.txt` and `CMakePresets.json` — build configuration is the most common silent-cross-host failure mode (a flag gcc accepts but clang rejects, a target that exists only on Metal, etc.)

The working principle is "narrow + caught-as-we-go", not "smoke everything that might break." Lua-binding headers (`*_lua.hpp`), `engine/ecs/**`, and other "could theoretically break" categories are deliberately excluded.

If none of these match, return to `SKILL.md` step 6 — no smoke tagging needed.

## Two verification tiers

Cross-host smoke is organized into two tiers, not three independent hosts:

- **OpenGL tier = {linux, windows}.** Both build the same OpenGL backend; a clean
  build + `IRShapeDebug` smoke on **either one** is sufficient evidence the OpenGL
  path is healthy — you do NOT need both. The engine ships on Windows, so
  `windows` is the tier's canonical representative.
- **Metal tier = {macos}.** Its own backend; verified independently.

`commit-and-push` stamps `fleet:authored-on-linux`, `fleet:authored-on-macos`, or
`fleet:authored-on-windows` at PR-create time based on the author's `uname -s`.
Per [`engine/render/CLAUDE.md`](../../../../engine/render/CLAUDE.md) "Verifying
render changes", render-PR authors build + run the demo on their host before
opening, so authoring on a host baseline-validates that host's **tier**. Only the
OTHER tier(s) need cross-host validation.

## Subtract the author's tier before tagging

Read the candidate's labels (already in the cache as `repos.engine.prs[].labels`)
and add a smoke label only for a tier the author did NOT cover:

| Author label present | Author's tier | Add smoke labels |
|---|---|---|
| `fleet:authored-on-linux` | OpenGL ✓ | `fleet:needs-macos-smoke` |
| `fleet:authored-on-windows` | OpenGL ✓ | `fleet:needs-macos-smoke` |
| `fleet:authored-on-macos` | Metal ✓ | `fleet:needs-windows-smoke` |
| None (unrecognized host or pre-fix PR) | — | `fleet:needs-windows-smoke`, `fleet:needs-macos-smoke` |

An OpenGL author (linux **or** windows) already covers the OpenGL tier, so the only
outstanding work is the Metal smoke. A Metal author needs one OpenGL verification —
routed to `windows`, the ship platform — and nothing more.

```bash
# Linux author (OpenGL covered) → Metal smoke only:
gh pr edit <N> --add-label "fleet:needs-macos-smoke"

# Windows author (OpenGL covered) → Metal smoke only:
gh pr edit <N> --add-label "fleet:needs-macos-smoke"

# macOS author (Metal covered) → one OpenGL smoke, routed to the ship platform:
gh pr edit <N> --add-label "fleet:needs-windows-smoke"

# None (pre-fix PR) → one OpenGL + Metal (keep separate; each is idempotent):
gh pr edit <N> --add-label "fleet:needs-windows-smoke"
gh pr edit <N> --add-label "fleet:needs-macos-smoke"
```

Each host's worker iterations poll for the label matching their host, run a
clean-checkout build + `IRShapeDebug` smoke, and remove the label on success.
While any smoke label persists, the human should hold the merge.

**Merge gate (OpenGL tier).** The OpenGL requirement is satisfied by **either**
`fleet:verified-windows` **or** `fleet:verified-linux` (or by an OpenGL author,
who needs no smoke) — never both. Because the engine ships on Windows, the OpenGL
representative is routed to `windows` above; if you instead want every render PR
to carry a Windows verification rather than relying on a linux author's own build,
add `fleet:needs-windows-smoke` to linux-authored render PRs too — that is stricter
than "either is enough" and a deliberate per-repo choice.

## Skip the tagging step for

- **Game-repo PRs** — tagged by the game-repo `review-pr` procedure (`creations/game/.claude/skills/review-pr/procedures/cross-host-smoke.md`); the engine-side procedure does not touch them.
- **Engine PRs that match none of the path filters above** — tooling, docs, `.claude/`-only PRs, etc. don't exercise host-divergent code and don't benefit from cross-host smoke.

If the labels are already present from a prior reviewer pass, no action needed — `--add-label` is a no-op when the label is already set.

After tagging (or skipping), return to `SKILL.md` step 6 to report.
