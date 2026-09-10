# Check 7 — PR/issue-reference comments and motivation-prose blocks

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff adds comments in C++, shader, build, or tooling source.

Scope is every file class the fleet authors — `.hpp/.cpp/.h/.cc`,
`.glsl/.metal`, `.cmake` / `CMakeLists.txt`, `.sh`, `.py`, **and the
extensionless bash executables under `scripts/`**: 42 of the 48 bash tools
at the top of `scripts/fleet/` have no extension, so an extension-keyed glob
reaches the `tests/*.sh` suites and misses the primary tool surface (#2961).
Tests are in scope: a regression test's header states the property it locks
in the present tense, not the change history.

`CLAUDE-BASELINE.md` §Style bans comments that reference the current task or
fix ("Reference adoption for #2044", "added for the #NNN flow") and
block-level motivation prose explaining why a module was created ("Before
this, every demo hand-rolled…") — both belong in the PR description and rot
in source. §7's judgment pass missed this twice (PR #2045 C++, PR #2087
GLSL), so grep the diff mechanically — **shaders included**:

```
Grep tool with:
  pattern: '(//|/\*|\*|#).*#[0-9]{3,}\b'
  glob:    '**/*.{hpp,cpp,h,cc,glsl,metal,cmake,sh,py,txt}'
  output_mode: 'content'
  -n: true
```

Run the pattern again with `glob: 'scripts/**'` — that arm is what reaches
the extensionless executables; the extension list alone cannot.

Cross-reference hits against added (`+`) lines only. A bare durable backref
(`// see #N`, the §7-sanctioned form) is fine; anything narrating the task
("for #N", "adoption for #N", "fix for #N", "added in #N") is the smell.
For motivation prose, eyeball each added comment block of 3+ lines in the
diff: if it explains the module's origin story or pre-change state rather
than a durable invariant, cut it (keep at most a one-line WHY + `// see #N`).

Fix: delete the comment when the task narration was all it carried; reduce
it to the bare `// see #N` backref when it also states a durable WHY. Same
disposition for a motivation-prose block.
