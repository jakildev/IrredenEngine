# `.claude/rules/` — path-scoped rules, and how to sweep for violations

Each `cpp-*.md` here is a path-scoped rule: the `paths:` frontmatter is the
**injection scope** (the harness loads the rule when a matching file is
opened), not a search root. Most rules also carry a **Detection** block — a
pattern a reviewer or a starved worker runs tree-wide.

This README is the one deliberately frontmatter-less file here — it is the
directory index and the sweep-trap warning, loads in every session, and is
not a rule; every `cpp-*.md` is path-scoped.

## Never root a tree sweep at `creations/` or `.claude/`

`rg` rooted **at** a directory that owns an ignore-then-negate block in
`.gitignore` prunes the children before the `!creations/demos/` re-inclusion
can apply, walks almost nothing, and reports a **clean pass**:

```
$ rg --files creations | wc -l          #   2
$ git ls-files creations | wc -l        # 273
$ rg --files .claude   | wc -l          #   1
$ git ls-files .claude | wc -l          #  76
```

The harness `Grep` tool is the same walker and has the same exposure. Every
rule whose `paths:` names `creations/**` is affected — `cpp-globals.md`
included, now that it carries one. Root a sweep at the repo top or at a child
directory, never at `creations` / `.claude` themselves.

### Use `fleet-rules-sweep`

```
fleet-rules-sweep --pattern '<regex>' [--glob '<glob>']... [<scope>...]
```

The file set comes from `git ls-files -co --exclude-standard`, so the walker
is never involved. Globs use `rg -g` semantics (path when the glob contains
`/`, else basename; `*.{hpp,h}` brace alternation; leading `!` negates), so
a rule's `glob:` line pastes in unchanged. The pattern is Python `re`, not
POSIX.

| exit | meaning |
|---|---|
| 0 | matches found |
| 1 | no matches **and coverage was non-zero** — a real clean pass |
| 2 | usage error, or the scope resolved to **0 files** |

A zero-file scope can never come back as exit 1, and every run prints its
coverage (`-- swept N file(s) (M walked in scope)`) to stderr. Ignored paths
are excluded, so the private `creations/game` clone is never swept from the
engine repo — sweep it from its own checkout
(`docs/agents/CLAUDE-BASELINE.md` §"Cross-repo information isolation").

### If you must hand-roll it

1. `fleet-rules-sweep --glob '*.cpp' creations` — guarded; prefer this.
2. Name the child directories: `rg -g '*.cpp' creations/demos creations/editors`.
3. Root at the repo top with a path glob: `rg -g 'creations/**/*.cpp' .`

Compare the files-searched count against `git ls-files <scope> | wc -l`
before reporting a clean pass.

## Adding a rule

A new `<topic>.md` (C++-only rules take the `cpp-` prefix; a rule that spans
languages, like `comments.md`, does not) carries `paths:` frontmatter, the
rule statement, the sanctioned patterns, a Detection block whose pattern and glob run through
`fleet-rules-sweep` verbatim, and a Live deviations register (the only place
issue numbers belong). Register it in the canonical-home map in
[`docs/agents/CLAUDE-BASELINE.md`](../../docs/agents/CLAUDE-BASELINE.md).
Fenced `fleet-*` commands in these files must resolve to a tracked script
(`scripts/fleet/lint_rules_commands.py` gates it).

Both obligations above are **executed**: `scripts/fleet/lint_rules_registry.py`
(suite `scripts/fleet/tests/test_lint_rules_registry.py`, run by
`fleet-tests.yml` on every PR touching this directory or `docs/agents/`) fails
on any `cpp-*.md` with no `paths:` frontmatter or no canonical-home row.
