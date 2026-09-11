# Check 7 — PR/issue-reference comments and motivation-prose blocks

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff adds comments
in C++, shader, build, or tooling source.

Scope: `.hpp/.cpp/.h/.cc`, `.glsl/.metal`, `.cmake` / `CMakeLists.txt`,
`.sh`, `.py`, and the extensionless bash executables under `scripts/`
(most of the primary tool surface has no extension). Tests are in scope: a
regression test's header states the property it locks, not the change
history.

`CLAUDE-BASELINE.md` §Style bans comments referencing the current task
("added for the #NNN flow") and block-level motivation prose ("Before
this, every demo hand-rolled…") — both belong in the PR description.

```
Grep tool with:
  pattern: '(//|/\*|\*|#).*#[0-9]{3,}\b'
  glob:    '**/*.{hpp,cpp,h,cc,glsl,metal,cmake,sh,py,txt}'
  output_mode: 'content'
  -n: true
```

Run the pattern again with `glob: 'scripts/**'` — that arm reaches the
extensionless executables.

Flag only `+` lines. Any issue or PR number inside a comment is a defect —
there is no sanctioned backref form (`.claude/rules/comments.md`). The
executed ratchet `python3 scripts/lint_comment_refs.py` fails CI on a file
that gained one; this pass fixes it before the push. For motivation prose,
read each added comment block of 3+ lines: an origin story or pre-change
state, rather than a durable invariant, is cut.

Fix: delete the comment when narration was all it carried; when it also
states a durable contract or invariant, keep only that sentence. Same for a
prose block.
