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
fleet-rules-sweep --pattern 'cmake --preset.*(-B |\$ENG)|(-B |\$ENG).*cmake --preset' --glob '*.md'
```

One command, because the canonical Bash rules
([`CLAUDE-BASELINE.md`](../../../../docs/agents/CLAUDE-BASELINE.md) § "Bash
tool rules") forbid both `cmd1 | cmd2` pipelines and Bash `grep` — an
`rg | grep | grep` recipe is not executable by the agents this check governs.
The regex carries both flag orders (`--preset` before or after the
engine-rooted flag). Apply the `-S` exclusion by **reading the returned
lines**: a hit whose command already carries `-S` is clean, one without it is
the finding. Prefer `fleet-rules-sweep` over a bare `rg` — it resolves the
file set through `git ls-files` and prints its coverage, so a zero-hit result
is reportable as evidence rather than a possible false clean
([`.claude/rules/README.md`](../../../rules/README.md), #2739).

At HEAD the sweep returns exactly one raw hit — **this file's own recipe line
above**, which necessarily contains the pattern it searches for. Discard it:
the check is diff-triggered on a fenced block *added by the subject diff*, and
this line is the detector, not a snippet anyone runs `cmake` from. That leaves
**zero findings tree-wide**.

The positive control is `38bfadb52^`: the same pattern returns the
`build-game` recipe (`BUILD.md:116`), which carries no `-S` and no engine-root
`cd` — a true finding. Run it before trusting a zero-hit pass, so a clean
result is distinguishable from a pattern that matches nothing.

A bare `cmake --preset <host>-debug` in a fence with no `cd` and no
engine-rooted path is **not** a finding — its implied CWD is the engine
root, the documented default. Nine such snippets stand at HEAD (`README.md`,
the top-level `CLAUDE.md`, `tools/img_diff/README.md`, `docs/perf/`), so a
rule keyed on "no `-S` and no `cd`" is nine false positives, not a check.

Report: which of `-S` or a `cd` fits is the author's call. The convention
extends to downstream creation/game repos, so this applies to any changed
`.md`, not only `docs/`. (#1679 documented this; #2995 escalated it here
after the doc that states the rule shipped the counter-example.)
