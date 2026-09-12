# Check 7 — PR/issue-reference comments and motivation-prose blocks

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff adds comments
in any source class the comment rule covers — C/C++/Objective-C++, shader,
Lua, Python/Starlark, shell, PowerShell, or CMake.

Scope is the population `comment_family()` in `scripts/lint_comment_refs.py`
selects, class for class — the executed ratchet and this judgment pass are
the two halves of one rule (`.claude/rules/comments.md`), so the globs below
carry every extension of the six comment families:
`.hpp/.cpp/.h/.hh/.hxx/.cc/.cxx/.c/.tpp/.inl/.mm/.m`, `.glsl/.metal`,
`.lua`, `.py/.bzl/.bazel`, `.sh/.bash/.zsh`, `.ps1`, `.cmake` /
`CMakeLists.txt`, plus the extensionless interpreter executables under
`scripts/` and `engine/tools/bin/` (most of the primary tool surface has no
extension). The `Check07Scope` suite in
`scripts/fleet/tests/test_lint_comment_refs.py` pins the two scopes
together. Tests are in scope: a regression test's header states the
property it locks, not the change history.

`CLAUDE-BASELINE.md` §Style bans comments referencing the current task
("added for the #NNN flow") and block-level motivation prose ("Before
this, every demo hand-rolled…") — both belong in the PR description.

```
Grep tool with:
  pattern: '(//|/\*|\*|--|#).*#[0-9]{3,}\b'
  glob:    '**/*.{hpp,cpp,h,hh,hxx,cc,cxx,c,tpp,inl,mm,m,glsl,metal,lua,py,bzl,bazel,sh,bash,zsh,ps1,cmake,txt}'
  output_mode: 'content'
  -n: true
```

The `--` alternative is Lua's comment marker. Run the pattern again with
`glob: 'scripts/**'` and with `glob: 'engine/tools/bin/**'` — those arms
reach the extensionless executables.

Flag only `+` lines. Any issue or PR number inside a comment is a defect —
there is no sanctioned backref form (`.claude/rules/comments.md`). The
executed ratchet `python3 scripts/lint_comment_refs.py` fails CI on a file
that gained one; this pass fixes it before the push. For motivation prose,
read each added comment block of 3+ lines: an origin story or pre-change
state, rather than a durable invariant, is cut.

Fix: delete the comment when narration was all it carried; when it also
states a durable contract or invariant, keep only that sentence. Same for a
prose block.
