# Irreden Engine

Isometric voxel game engine built on an archetype-based ECS. C++ handles the
engine, systems, and pipelines. LuaJIT 2.1 (via sol2) drives entity creation
and game logic in creations.

[`docs/agents/AGENTS-ARCHITECTURE.md`](docs/agents/AGENTS-ARCHITECTURE.md) has
the full long-form architecture reference. Module-specific details live in
`CLAUDE.md` files nested under `engine/`, `engine/prefabs/`, and `creations/`
— read the most specific one for whatever directory you're working in.

Private implementations (games, editors, experiments) layered on top of the
engine may live in their own gitignored subdirectories under `creations/` and
may define their own conventions, review criteria, workflows, or even their
own repo. The skills in `.claude/skills/` are engine-level and generic; when
working inside such a subdirectory, always read that subdirectory's own
`CLAUDE.md` first — its rules override the engine baseline for that scope.

> **Cross-cutting baseline** — naming, style, the ECS footgun, the rules
> for what belongs in a `CLAUDE.md`, Bash tool restrictions, and cross-repo
> information isolation live in
> [`docs/agents/CLAUDE-BASELINE.md`](docs/agents/CLAUDE-BASELINE.md). Read
> that file alongside this one and treat its sections as if inlined here.
> Creations inherit the same baseline by reference; see
> [`docs/design/claude-md-sharing.md`](docs/design/claude-md-sharing.md) for
> the mechanism and opt-out form.

---

## Where to find things

| You need… | Read… |
|---|---|
| Coding conventions (ECS, naming, IRMath, ownership, comment style) | [`docs/agents/CLAUDE-BASELINE.md`](docs/agents/CLAUDE-BASELINE.md) |
| Module-specific patterns (ECS, prefabs, render, math, system, etc.) | the nearest `CLAUDE.md` (loaded automatically when you open a file in that subtree) |
| Long-form architecture reference (ECS internals, render pipeline, coordinate systems, Lua integration) | [`docs/agents/AGENTS-ARCHITECTURE.md`](docs/agents/AGENTS-ARCHITECTURE.md) |
| Build commands and environment setup (Linux/WSL, Windows, macOS) | [`docs/agents/BUILD.md`](docs/agents/BUILD.md) |
| Fleet workflow (parallel agents, PRs, cursor cues, design escalation, model split, labels, clean-exit policy, fix-forward, feedback) | [`docs/agents/FLEET.md`](docs/agents/FLEET.md) |
| Skills (named workflows like `simplify`, `commit-and-push`, `review-pr`) | [`.claude/skills/`](.claude/skills/) — each has its own `SKILL.md` |
| Shared skill flows (fleet skills factored for cross-repo reuse) | [`docs/agents/skills/`](docs/agents/skills/) — canonical flows; each repo's `SKILL.md` is a thin wrapper (mechanism: [`docs/design/skill-sharing.md`](docs/design/skill-sharing.md)) |
| Roles (autonomous personas — worker, reviewer, merger, architect) | [`.claude/commands/role-*.md`](.claude/commands/) |
| Standing objectives (the human-owned "what" above epics) | [`docs/design/objectives/`](docs/design/objectives/README.md) |
| Task queue (when running fleet roles) | `fleet-queue-list` or `gh issue list --label fleet:queued --repo jakildev/IrredenEngine` |
| Cross-repo info isolation rule (engine repo public, game repo private) | [`docs/agents/CLAUDE-BASELINE.md`](docs/agents/CLAUDE-BASELINE.md) §"Cross-repo information isolation" |

---

## Build quick reference

Three supported environments, each with its own preset:

| Preset          | Target                                | Role                       |
|-----------------|---------------------------------------|----------------------------|
| `linux-debug`   | Linux / WSL2 Ubuntu, gcc-13+, OpenGL  | **Fleet environment**      |
| `windows-debug` | Windows native, MSYS2 mingw64, OpenGL | Ship-it + fleet host       |
| `macos-debug`   | macOS native, Metal backend           | Matured on demand          |

```bash
# Linux / WSL (the fleet env):
cmake --preset linux-debug          # one-time configure
fleet-build --target IRShapeDebug   # build (auto-detects worktree)
fleet-run IRShapeDebug              # run from the build dir
```

`fleet-help` prints the index of `fleet-*` tools. **Use `fleet-build` and `fleet-run` rather than raw `cmake --build` / `./<exe>`** — the wrappers avoid Claude Code security gates (`$(nproc)` substitution, `cd && exec`).

For Windows-native PATH gotchas, the cc1plus silent-crash root cause, macOS Metal preset details, and the full troubleshooting guide, see [`docs/agents/BUILD.md`](docs/agents/BUILD.md).

---

## Workflow at a glance

This repo runs a parallel-agent workflow with PRs (never commit to `master` directly). When a human is in the loop (Cursor IDE), you iterate freely and the human cues `commit-and-push` when ready. When fleet roles run autonomously, they pick from the GitHub issue queue (`fleet:queued` label), open PRs via `commit-and-push`, and start fresh with `start-next-task`.

The two safety rules that apply everywhere:

- **Never commit to `master` directly.** Always work on a short-lived feature branch via the `commit-and-push` skill.
- **Never `--force` push to `master`.** Never use `--no-verify` to skip hooks unless the user explicitly asks.

For the full workflow — fleet rules, cursor cues, stacking, design escalation, label state machine, model split, cross-platform parity, feedback channel — see [`docs/agents/FLEET.md`](docs/agents/FLEET.md).

---

## Project layout

Each directory has its own `CLAUDE.md` with module-specific patterns,
gotchas, and file maps. Read the most specific one for whatever you're
touching. Creations layered on top of the engine (including gitignored
private implementations) define their own conventions in their own
`CLAUDE.md` — when working there, those override the engine baseline.
