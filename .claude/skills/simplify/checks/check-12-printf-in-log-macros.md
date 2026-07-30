# Check 12 — printf-style conversions inside the fmt-based log macros

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff adds `IR_LOG_*` / `IRE_LOG_*` / `IRE_GL_LOG_*` calls.

`IR_LOG_*` / `IRE_LOG_*` / `IRE_GL_LOG_*` format through
`fmt::format(fmt::runtime(...))`, which disables fmt's compile-time format
checking — so `IR_LOG_INFO("now %d / %d", a, b)` compiles clean and prints
the literal `%d`s with the arguments dropped (three voxel-editor PRs
shipped exactly this one-liner: #2491, #2625, #2633). A green build proves
nothing about a log format string on this path.

```
Grep tool with:
  pattern: '(IR_LOG_|IRE_LOG_|IRE_GL_LOG_)[A-Z]+\("[^"]*%[-+#0-9.]*(d|i|u|s|f|g|e|x|X|o|c|p|z[ud]|l[ud]|ll[ud])'
  glob:    '**/*.{hpp,cpp,h,cc,tpp}'
  output_mode: 'content'
  -n: true
```

Discard hits whose only `%`s are `%%` (literal percent) — a line carrying
both `%%` and a real conversion is still a hit. Keep the conversion-flag class
exactly as written — adding a space (`[-+ #0-9.]*`) false-positives on
prose percentages ("50% complete"). Auto-fix: replace each conversion with
`{}` (the argument list is already positional); an arity mismatch throws
`fmt::format_error` at runtime, so fix on sight. (#2637)
