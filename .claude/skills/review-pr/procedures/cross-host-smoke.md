# Cross-host smoke tagging (render PRs)

Procedure for tagging an engine render PR for OpenGL ↔ Metal cross-host validation after the verdict label is set. Fires only when [`SKILL.md`](../SKILL.md) step 5c determines the diff touches render code; non-render PRs skip this entirely.

## When this fires

Engine render PRs get built and run on whichever backend the author happened to be using (OpenGL on Linux, or Metal on macOS). The other backend's build + smoke path is not exercised until a fleet agent on that host picks the PR up. The `fleet:needs-<host>-smoke` labels surface that outstanding work so no render PR merges unvalidated on either backend.

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

If none of these match, return to `SKILL.md` step 6 — no smoke tagging needed.

## Subtract the author's host before tagging

`commit-and-push` stamps `fleet:authored-on-linux` or `fleet:authored-on-macos` at PR-create time based on the author's `uname -s`. Per [`engine/render/CLAUDE.md`](../../../../engine/render/CLAUDE.md) "Verifying render changes", render-PR authors build + run the demo on their host before opening, so authoring on a host is reasonable evidence its smoke is baseline-validated. Only the OTHER host actually needs cross-host validation.

Read the candidate's labels (already in the cache as `repos.engine.prs[].labels`) and add only the smoke label for the host the author was NOT on:

| Author label present | Add smoke label |
|---|---|
| `fleet:authored-on-linux` | `fleet:needs-macos-smoke` only |
| `fleet:authored-on-macos` | `fleet:needs-linux-smoke` only |
| Neither (Windows-native author, or pre-fix PR) | Both labels |

```bash
# Linux author  → one call:
gh pr edit <N> --add-label "fleet:needs-macos-smoke"

# macOS author  → one call:
gh pr edit <N> --add-label "fleet:needs-linux-smoke"

# Neither (Windows-native or pre-fix PR) → two calls, one per label
# (keep them separate so each is independently safe and idempotent):
gh pr edit <N> --add-label "fleet:needs-linux-smoke"
gh pr edit <N> --add-label "fleet:needs-macos-smoke"
```

Each host's author agents (opus-worker, sonnet-author) poll for the label matching their host, run a clean-checkout build + `IRShapeDebug` smoke, and remove the label on success. While either label persists, the human should hold the merge — that's the whole point of the tally.

## Skip the tagging step for

- **Game-repo PRs** — the game's render pipeline uses the engine's backend, so cross-host applies at engine level only.
- **Non-render engine PRs** (tooling, docs, `.claude/`, non-render modules like `engine/system/`) — these don't exercise backends and don't benefit from cross-host smoke.

If both labels are already present from a prior reviewer pass, no action needed — the `--add-label` call is a no-op when the label is already set.

After tagging (or skipping), return to `SKILL.md` step 6 to report.
