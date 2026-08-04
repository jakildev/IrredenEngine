# Check 8 — unreplaced scaffold placeholder sentinels

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff touches `creations/**` (especially a new creation).

`create-creation` templates require hand-replacing `YourCreation` /
`YOUR_CREATION`, and older scaffolds emitted a `YOUR_CREATION_NAME_HERE` log
string — a forgotten replacement compiles and runs silently (#2078's
`font_maker` shipped one):

```
Grep tool with:
  pattern: 'YOUR_CREATION_NAME_HERE|\bYourCreation\b|\bYOUR_CREATION\b'
  glob:    '{engine,creations,test}/**'
  output_mode: 'content'
  -n: true
```

Any hit in real source is a leftover (the tokens only belong inside the
`create-creation` skill's own template files). Auto-fix: substitute the real
creation name.
