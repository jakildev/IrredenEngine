# `.claude/rules/` — path-scoped rules, and how to sweep for violations

Each `cpp-*.md` in this directory is a path-scoped rule: the `paths:`
frontmatter tells Claude Code which files the rule applies to, and the harness
injects the rule when one of those files is opened. Several rules also carry a
**Detection** or **Audit hooks** block — a pattern a reviewer or a starved
worker can run tree-wide to find un-swept violations.

`paths:` is an *injection scope*, not a search root. Read the section below
before turning one into a sweep.

---

## Never root a tree sweep at `creations/` or `.claude/` (#2739)

`rg` rooted **at** a directory that owns an ignore-then-negate block in
`.gitignore` walks almost nothing and reports a **clean pass**:

```
$ rg --files creations | wc -l          #   2
$ git ls-files creations | wc -l        # 273
$ rg --files .claude   | wc -l          #   1
$ git ls-files .claude | wc -l          #  76
```

`git check-ignore` exits 1 on those files — git considers them perfectly
visible. Only ripgrep's walker drops them: the parent `.gitignore`'s
`creations/*` prunes each child directory, and the `!creations/demos/`
re-inclusion never gets applied to an already-pruned directory. Rooting one
level deeper (`creations/demos`) or at the repo top is fine, which is why this
went unnoticed for so long.

This is a **false-clean class, not a missing feature** — the sweep reports
success. Every rule here whose `paths:` names `creations/**` is exposed
(`cpp-ecs.md`, `cpp-ecs-smells.md`, `cpp-math.md`, `cpp-systems.md`,
`cpp-lua-enums.md`), and it was found when the `cpp-lua-enums` audit hook
returned 0 hits on a tree that had 4 matching files.

**The harness `Grep` tool has the same exposure.** On a host where Claude Code
supplies ripgrep, `rg` is a shell-snapshot function that re-execs the `claude`
binary — the measurements above were taken *through* the bundled ripgrep that
also backs `Grep`. `Grep(pattern, path="creations")` passes `path` as the
search root, so it inherits the walker behaviour. Scope `Grep` at the repo top
or at a child directory, never at `creations`/`.claude` themselves.

### Use `fleet-rules-sweep`

```
fleet-rules-sweep --pattern '<regex>' [--glob '<glob>']... [<scope>...]
```

It resolves the file set with `git ls-files -co --exclude-standard` — git's own
matcher, the one `check-ignore` answers from — so the walker is never involved.
Globs use `rg -g` semantics (path when the glob contains `/`, else basename;
`*.{hpp,h}` brace alternation; leading `!` negates), so a rule's `glob:` line
pastes in unchanged.

Exit codes are what make a clean pass reportable as evidence:

| code | meaning |
|---|---|
| 0 | matches found |
| 1 | no matches **and coverage was non-zero** — a real clean pass |
| 2 | usage error, or the scope resolved to **0 files** — the guard |

A scope that resolves to zero files can never come back as exit 1. That is the
regression check for #2739: the false clean is structurally unable to
masquerade as success. Every run also prints its coverage
(`-- swept N file(s) (M walked in scope) …`) to stderr, so a pasted result
carries its own proof of having looked at something.

Ignored paths are excluded, so the private `creations/game` clone is never
swept from the engine repo — sweep that repo from inside its own checkout
(see `docs/agents/CLAUDE-BASELINE.md` §"Cross-repo information isolation").

### If you must hand-roll it

Three forms that work, in preference order:

1. `fleet-rules-sweep --glob '*.cpp' creations` — guarded; prefer this.
2. Name the child directories: `rg -g '*.cpp' creations/demos creations/editors`.
3. Root at the repo top with a path glob: `rg -g 'creations/**/*.cpp' .`

Whatever you use, **cross-check coverage before reporting a clean pass** —
compare the files-searched count against `git ls-files <scope> | wc -l`. A
zero-hit sweep whose walk covered zero files is not evidence of anything.

---

## Adding a rule

A new `cpp-<topic>.md` carries `paths:` frontmatter, the rule statement, the
rationale, and — when the violation is greppable — a Detection block whose
pattern and glob are runnable through `fleet-rules-sweep` verbatim. Register it
in the canonical-home map in
[`docs/agents/CLAUDE-BASELINE.md`](../../docs/agents/CLAUDE-BASELINE.md).
