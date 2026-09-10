# Check 21 — `cmake --preset` snippets must name their source dir

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff adds a fenced block containing `cmake --preset` to any `.md`.

[`docs/agents/BUILD.md`](../../../../docs/agents/BUILD.md)'s **"Convention
for doc snippets that cite a preset"** block requires an explicit `-S
<engine-root>` **when the recipe's expected CWD is not the engine root** —
`cmake --preset macos-debug` alone fails anywhere without a
`CMakePresets.json`, and `role-worker.md` step 1c routes a worker into the
*game* worktree before sending them here to build.

Grep `cmake --preset` over `**/*.md`. For each hit on an added (`+`) line
inside a fence, pass if the command carries `-S`; otherwise the only
question is whether the recipe's CWD is the engine root, and two signals
answer it mechanically:

- the fence `cd`s before the command and the target is **not** an engine
  root (`cd "$GAME_WT"`, `cd creations/…`) → flag;
- the command already reaches the engine through another flag — `-B
  "$ENG/build-game"`, `-D…="$ENG/…"`, any absolute engine path — while
  leaving `--preset` to resolve against the cwd → flag. This is the shape
  #2993 shipped, and the one arm that fires with no `cd` in the fence at
  all, because the routing lives in the surrounding prose:

```bash
rg -n -g '*.md' 'cmake --preset' . | grep -E '\-B |\$ENG' | grep -v -- '-S '
```

Zero hits tree-wide at HEAD; one at `38bfadb52^`, the `build-game` recipe.
A bare `cmake --preset <host>-debug` in a fence with no `cd` and no
engine-rooted path is **not** a finding — its implied CWD is the engine
root, the documented default. Nine such snippets stand at HEAD (`README.md`,
the top-level `CLAUDE.md`, `tools/img_diff/README.md`, `docs/perf/`), so a
rule keyed on "no `-S` and no `cd`" is nine false positives, not a check.

Report: which of `-S` or a `cd` fits is the author's call. The convention
extends to downstream creation/game repos, so this applies to any changed
`.md`, not only `docs/`. (#1679 documented this; #2995 escalated it here
after the doc that states the rule shipped the counter-example.)
