# Check 5 — missing final newline on non-clang-format text files

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff touches
`.cmake`, `.md`, `.lua`, `.txt`, or `CMakeLists.txt` files.

`.editorconfig` sets `insert_final_newline = true`, but the file-edit
tools don't honor it and clang-format only covers C++.

```bash
for f in $(git diff --name-only origin/master -- '*.cmake' '*.md' '*.lua' '*.txt'); do
  [ -s "$f" ] && [ -n "$(tail -c1 "$f")" ] && echo "MISSING final newline: $f"
done
```

Auto-fix: append one `\n`. Scope to files changed on this branch, not the
tree.

For each changed `.lua` file in the same set, also flag **dead locals** —
a `local x = ...` assigned once and never read (luacheck's
`unused-local`). Auto-fix: delete the assignment when the initializer is
side-effect-free; otherwise report.
