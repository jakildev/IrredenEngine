# Check 21 — `cmake --preset` snippets must name their source dir

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff adds a fenced
block containing `cmake --preset` to any `.md`.

[`docs/agents/BUILD.md`](../../../../docs/agents/BUILD.md) "Convention for
doc snippets that cite a preset" requires an explicit `-S <engine-root>`
when the recipe's expected CWD is not the engine root — `cmake --preset
macos-debug` alone fails anywhere without a `CMakePresets.json`.

For each `cmake --preset` on a `+` line inside a fence: pass if it carries
`-S`. Otherwise two signals answer whether the CWD is the engine root:

- the fence `cd`s to a non-engine root (`cd "$GAME_WT"`, `cd creations/…`)
  before the command → flag;
- the command reaches the engine through another flag (`-B
  "$ENG/build-game"`, `-D…="$ENG/…"`, any absolute engine path) while
  `--preset` resolves against the cwd → flag. This arm fires with no `cd`
  in the fence at all.

```bash
fleet-rules-sweep --pattern 'cmake --preset.*(-B |\$ENG)|(-B |\$ENG).*cmake --preset' --glob '*.md'
```

One command (the Bash rules forbid pipelines and Bash `grep`); the regex
carries both flag orders. Apply the `-S` exclusion by reading the returned
lines. `fleet-rules-sweep` resolves the file set through `git ls-files`
and prints coverage, so a zero-hit result is evidence
([`.claude/rules/README.md`](../../../rules/README.md)). At HEAD the sweep
returns exactly one raw hit — this file's own recipe line — which is the
detector, not a finding; the positive control is `38bfadb52^`, where the
same pattern returns a true `build-game` recipe without `-S`.

A bare `cmake --preset <host>-debug` with no `cd` and no engine-rooted
path is **not** a finding — its implied CWD is the engine root, the
documented default. Report: which of `-S` or a `cd` fits is the author's
call. Applies to any changed `.md`, downstream repos included.
