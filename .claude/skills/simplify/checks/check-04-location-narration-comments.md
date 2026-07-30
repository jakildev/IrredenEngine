# Check 4 — location-reference comment narration

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** any C++ change in the diff.

Comments that point the reader at *other code* — "set above", "see below",
"see above", "defined above", "declared below", "called from" — narrate
WHERE rather than WHY. They are a specific, grep-able instance of the
WHY-not-WHAT rule (`CLAUDE-BASELINE.md` §Style: "'Set above' is code
narration, not a WHY"): the location is already visible in the code, and any
real rationale belongs at the referenced site, not cross-referenced from
here.

```
Grep tool with:
  pattern: '//.*\b(set above|see below|see above|defined above|declared below|called from)\b'
  glob:    '**/*.{hpp,cpp,h,cc}'
  output_mode: 'content'
  -n: true
```

Cross-reference hits against `git diff --unified=0` added (`+`) lines —
only flag newly introduced narration, not pre-existing comments in
untouched code. Fix: delete the cross-reference; if it was carrying a real
WHY, move that WHY to the site it points at.
