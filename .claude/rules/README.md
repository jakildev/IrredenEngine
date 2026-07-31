# Running a rule's Detection sweep

Every file in this directory states a rule. Several carry a `## Detection`
block — a pattern you run tree-wide to prove the rule holds. **Run it with
`fleet-sweep`, not with `rg` or the Grep tool:**

```
fleet-sweep -P '<pattern>' -- '<git-pathspec>'...
```

```
# a creations-scoped rule (cpp-math.md, cpp-ecs.md, cpp-lua-enums.md, ...):
fleet-sweep -E 'glm::' -- 'creations/*.cpp' 'creations/*.hpp'

# a lookahead pattern over every tracked header:
fleet-sweep -P '^\s*(inline|extern)\s+(?!(constexpr|const|void)\b)[^(]*[;={]' -- '*.hpp' '*.h'
```

Not every rule's detector is a sweep — `cpp-globals.md`'s is moving to an
executed build target (#2727). Read the rule's own Detection block first;
`fleet-sweep` is for the ones that are still a grep.

`-P` is required for the `(?!...)` lookahead the Detection patterns use.
Exit status is the contract: **0** = found, **1** = PROVEN clean, **2** =
the sweep did not run. Only 1 is a clean result.

## Why not `rg` / the Grep tool

A sweep's product is the *negative* result — "zero hits, the rule holds".
That makes it the one kind of command whose every failure mode looks
exactly like success. #2739 found four independent ways a ripgrep-rooted
sweep of this repo reads a false clean, all measured on this tree:

| # | vector | measured |
|---|---|---|
| 1 | **Ignore-negation pruning.** `.gitignore` has two `dir/*` + `!dir/child/` blocks (`creations/*`, `.claude/*`). Rooting the walk *at* that directory prunes every child before the negation applies. | `rg --files creations` → **2** (git tracks **273**); `rg --files .claude` → **1** (git tracks **76**) |
| 2 | **Hidden directories.** `.claude` is a dotdir, so repo-top-rooted forms skip it without `--hidden`. | `rg --files -g '**/.claude/**' .` → **0**; with `--hidden` → 70 |
| 3 | **Symlinks.** `.claude/agents/role-*.md` are tracked symlinks (git mode 120000); the walker never follows them without `-L`. | **7** tracked paths invisible to every rooted form |
| 4 | **Tracked-but-ignored files.** A file can be in the index *and* match an ignore rule. ripgrep honors the rule; the index still carries the file. | `creations/bazel_test/main.cpp` (`.gitignore:40`), `creations/editors/font_maker/main.cpp` (`.gitignore:45`) |

Vector 1 is the one that bit: one level down (`creations/demos`) walks
correctly, which is why it sat undetected — and why the `creations/**`
scopes named in five of these rules' `paths:` were never actually swept.

There is **no ripgrep spelling that reads the full tracked set** — vectors
3 and 4 survive every root and every `--hidden`/`-g` combination. That is
why the fix is not a better `rg` incantation: `fleet-sweep` asks **git**,
which is the ground truth the walker was approximating. `git ls-files`
establishes the denominator (a sweep that read zero files has not run, and
`fleet-sweep` refuses to call that clean — exit 2), and `git grep` does the
search, immune to all four vectors and needing no `rg` binary at all.

Using the index also *keeps* the cross-repo isolation rule: unlike
`--no-ignore-parent`, it will not pull the gitignored private
`creations/game` clone into an engine-side sweep.

## The Grep-tool verdict (#2739 acceptance)

**Confirmed, same exposure.** The harness Grep tool is ripgrep-backed with
the same ignore defaults; in a fleet pane `rg` is a shell function that
re-execs the `claude` binary with `ARGV0=rg`, i.e. *the same engine*, and it
reproduces vectors 1–4 above verbatim. `Grep(pattern, path="creations")` and
`Grep(pattern, path=".claude")` read a false clean. Use `fleet-sweep` for any
sweep whose conclusion is "no hits".

One correction to the original report: the `-g 'creations/**'` "trap" it
described does **not** reproduce. Both `-g 'creations/**/*.cpp'` and
`-g '**/creations/**/*.cpp'` return the same 4 files here — the repo-top
root works with either spelling. The leading-`**` form has the opposite
hazard: `**/creations/**` also matches the unrelated `docs/creations/`.
Neither spelling escapes vectors 3 and 4, so neither is a working form.

## Scoping note for the `paths:` frontmatter

The `paths:` block at the top of a rule file is an **attach matcher** — it
tells the harness which edited files should surface the rule. It is *not* a
sweep invocation. Copying a `creations/**/...` glob out of `paths:` into an
`rg` root or `-g` is exactly how a detector scopes itself into vector 1.
For a sweep, translate it to a git pathspec and hand it to `fleet-sweep`:
`creations/**/*.{hpp,cpp}` → `-- 'creations/*.hpp' 'creations/*.cpp'`
(a git pathspec `*` already crosses `/`).

Pin the expected scope size with `--min N` when you know it, so a silently
narrowed pathspec fails instead of reading clean.
