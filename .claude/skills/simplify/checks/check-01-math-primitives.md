# Check 1 — `glm::` and `std::` math calls outside the allowlist

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** any C++ change in the diff.

```
Grep tool with:
  pattern: '\b(glm::|std::(sin|cos|tan|sqrt|abs|min|max|clamp|floor|ceil|round|pow|atan2|asin|acos))\b'
  glob:    '**/*.{hpp,cpp,h,cc}'
  output_mode: 'files_with_matches'
```

Allowlist (do not flag): `engine/math/**` (IRMath wraps these names);
`engine/render/include/irreden/render/backend/**` (backend interop passes
raw glm types); `*.glsl` / `*.metal`.

Flag everything else with the IRMath equivalent
([`.claude/rules/cpp-math.md`](../../../rules/cpp-math.md) has the
substitution table). If the wrapper does not exist, don't substitute —
flag "IRMath::<name> does not exist; add the wrapper to `engine/math/`
first, then call it."
