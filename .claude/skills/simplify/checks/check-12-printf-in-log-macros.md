# Check 12 — printf-style conversions inside the fmt-based log macros

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff adds
`IR_LOG_*` / `IRE_LOG_*` / `IRE_GL_LOG_*` calls.

These macros format through `fmt::format(fmt::runtime(...))`, which
disables compile-time format checking — `IR_LOG_INFO("now %d / %d", a,
b)` compiles clean and prints the literal `%d`s. A green build proves
nothing about a format string on this path.

```
Grep tool with:
  pattern: '(IR_LOG_|IRE_LOG_|IRE_GL_LOG_)[A-Z]+\(\s*"[^"]*%[-+#0-9.]*(d|i|u|s|f|g|e|x|X|o|c|p|z[ud]|l[ud]|ll[ud])'
  glob:    '**/*.{hpp,cpp,h,cc,tpp}'
  output_mode: 'content'
  multiline: true
  -n: true
```

`\(\s*"` plus `multiline` is load-bearing: clang-format wraps long calls
so the format string lands on the next line, and a pattern requiring
`("` adjacent misses those. Shell equivalent: `rg -U '<pattern>'`. There
is no single-line fallback — Bash `grep` is banned (`CLAUDE-BASELINE.md`
§"Bash tool rules").

Discard hits whose only `%`s are `%%`; a line with both `%%` and a real
conversion is still a hit. Keep the flag class exactly as written — a
space in it (`[-+ #0-9.]*`) false-positives on prose percentages.
Auto-fix: replace each conversion with `{}` (the argument list is already
positional); an arity mismatch throws `fmt::format_error` at runtime, so
fix on sight.
