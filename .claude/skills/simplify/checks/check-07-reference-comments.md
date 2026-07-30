# Check 7 — PR/issue-reference comments and motivation-prose blocks

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff adds comments in C++ or shader files.

`CLAUDE-BASELINE.md` §Style bans comments that reference the current task or
fix ("Reference adoption for #2044", "added for the #NNN flow") and
block-level motivation prose explaining why a module was created ("Before
this, every demo hand-rolled…") — both belong in the PR description and rot
in source. §7's judgment pass missed this twice (PR #2045 C++, PR #2087
GLSL), so grep the diff mechanically — **shaders included**:

```
Grep tool with:
  pattern: '(//|/\*|\*).*#[0-9]{3,}\b'
  glob:    '**/*.{hpp,cpp,h,cc,glsl,metal}'
  output_mode: 'content'
  -n: true
```

Cross-reference hits against added (`+`) lines only. A bare durable backref
(`// see #N`, the §7-sanctioned form) is fine; anything narrating the task
("for #N", "adoption for #N", "fix for #N", "added in #N") is the smell.
For motivation prose, eyeball each added comment block of 3+ lines in the
diff: if it explains the module's origin story or pre-change state rather
than a durable invariant, cut it (keep at most a one-line WHY + `// see #N`).
