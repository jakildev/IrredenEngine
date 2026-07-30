# Check 5 — missing final newline on non-clang-format text files

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff touches `.cmake`, `.md`, `.lua`, `.txt`, or `CMakeLists.txt` files.

`.editorconfig` sets `insert_final_newline = true` (globally, and again for
`[*.{lua,cmake,txt,md}]` / `[CMakeLists.txt]`), but the agent file-edit tools
don't honor it and clang-format only enforces it for the C++ files in its
scope — so `.cmake`, `.md`, `.lua`, `.txt`, and `CMakeLists.txt` fall through
to a human/reviewer eyeball (the #1861 nit on `cmake/ir_functions.cmake`). For
each changed file of those types, flag a missing trailing newline — a non-empty
last byte (i.e. not `\n`) is the violation:

```bash
for f in $(git diff --name-only origin/master -- '*.cmake' '*.md' '*.lua' '*.txt'); do
  [ -s "$f" ] && [ -n "$(tail -c1 "$f")" ] && echo "MISSING final newline: $f"
done
```

Auto-fix: append a single `\n`. Scope to files changed on this branch (the
§10 `format-changed` set), not the whole tree.

For each changed `.lua` file the same loop already collects, also flag
**dead locals** — a `local x = ...` assigned once and never read again in
the file (luacheck's `unused-local` class; game PR #323's dead
`local playerId` cost a review round-trip). Auto-fix: delete the
assignment when the initializer is side-effect-free; otherwise report.
(#2574)
