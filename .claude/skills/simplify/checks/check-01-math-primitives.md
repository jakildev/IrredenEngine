# Check 1 — `glm::` and `std::` math calls outside the allowlist

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** any C++ change in the diff.

Run a grep against all in-scope C++ files (catches both new and existing
violations). The Grep tool is allowlisted; use it directly:

```
Grep tool with:
  pattern: '\b(glm::|std::(sin|cos|tan|sqrt|abs|min|max|clamp|floor|ceil|round|pow|atan2|asin|acos))\b'
  glob:    '**/*.{hpp,cpp,h,cc}'
  output_mode: 'files_with_matches'
```

For each hit, manually exclude paths in the allowlist (do not flag):

- `engine/math/**` — IRMath itself wraps these names internally.
- `engine/render/include/irreden/render/backend/**` — backend interop
  may pass raw glm types into graphics APIs.
- `*.glsl` / `*.metal` files (the grep glob excludes these, but
  double-check).

For everything else, flag with the IRMath equivalent. See [`.claude/rules/cpp-math.md`](../../../rules/cpp-math.md) for the full substitution table.

If the IRMath wrapper does not exist yet, **don't auto-substitute** —
flag with: "IRMath::<name> does not exist; add the wrapper to
`engine/math/` first, then call it."
