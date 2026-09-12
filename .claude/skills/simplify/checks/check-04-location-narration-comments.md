# Check 4 — location-reference comment narration

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** any C++ change in the diff.

Comments that point at *other code* ("set above", "see below", "called
from") narrate WHERE, not WHY — the grep-able instance of
`CLAUDE-BASELINE.md` §Style's WHY-not-WHAT rule.

```
Grep tool with:
  pattern: '//.*\b(set above|see below|see above|defined above|declared below|called from)\b'
  glob:    '**/*.{hpp,cpp,h,cc}'
  output_mode: 'content'
  -n: true
```

Flag only `+` lines in `git diff --unified=0`. Fix: delete the
cross-reference; move any real WHY to the site it points at.
