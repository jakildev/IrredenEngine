---
paths:
  - "**/*.{hpp,cpp,h,hh,hxx,cc,cxx,c,tpp,inl,mm,m,glsl,metal,lua,py,sh,bash,zsh,ps1,cmake,bzl,bazel}"
  - "**/CMakeLists.txt"
  - "scripts/**"
  - "engine/tools/bin/**"
---

# Comments explain the code, never its history

Code is self-documenting: names carry intent, types and asserts carry
contracts, and the commit and PR carry the story of how the code got here.
A comment exists only where the code cannot say something the reader needs.

## Write a comment for

- A contract the signature cannot express: a precondition, an ordering
  requirement, units, a valid range, ownership the type does not encode.
- A non-obvious invariant the code relies on, stated as a fact about the
  code as it stands.
- A gotcha: why the obvious alternative is wrong here (a platform quirk, an
  aliasing hazard, a driver bug), so the next reader does not "fix" it.
- A deliberate cross-file coupling, at the declaration site: "this ordering
  has N consumers".

## Never write

- What the code does. If a line needs restating, rename or restructure it.
- How it got here: what changed, what it replaced, what the bug was, who
  decided. That belongs in the commit message, the PR body, or a
  `docs/design/` document.
- Issue, PR, or task numbers. `#1234` is a pointer into a tracker the source
  tree does not own; it tells the reader nothing and rots when the issue
  closes. There is no sanctioned backref form.
- Narration of location: `// set above`, `// see below`, `// called from X`.
- Motivation prose for a module's existence.

The keeper test: if this code had always existed exactly as it is, would the
sentence still be true and still be needed? If not, cut it.

## Detection

Executed: `python3 scripts/lint_comment_refs.py`, run by the `comment-refs`
workflow on every push and PR. It tokenizes each file in its own comment
syntax — a string literal, a shell here-document, a CMake bracket argument
and a PowerShell here-string are values, not comments — and counts
`#` followed by three or more digits inside comments per file against
`scripts/lint_comment_refs_baseline.json`. Three is a floor against ordinals
(`Rule #5`, `invariant #1`), and there is no ceiling, so the count stays
honest once the tracker passes four digits. A file may not gain references,
and a pull request is checked against the base branch's baseline, so editing
the baseline buys nothing. After removing references from a file, run it
with `--update-baseline` (it only lowers counts). The baseline is the sweep
backlog and reaches zero when the tree is clean.

Judgment-side: `simplify` §7 and its Check 7 review every added comment for
narration, motivation prose, and location references. The two layers run over
the **same population, class for class** — Check 7's globs carry every
extension `comment_family()` selects plus the extensionless interpreter
executables, and the `Check07Scope` suite in
`scripts/fleet/tests/test_lint_comment_refs.py` fails when one side gains a
class the other lacks. A class only one layer reaches would get the numeric
gate without the narration gate, or the reverse.
