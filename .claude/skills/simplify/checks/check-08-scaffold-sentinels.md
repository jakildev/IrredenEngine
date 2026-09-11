# Check 8 — unreplaced scaffold placeholder sentinels

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff touches
`creations/**` (especially a new creation).

`create-creation` templates require hand-replacing `YourCreation` /
`YOUR_CREATION`; older scaffolds emitted `YOUR_CREATION_NAME_HERE`. A
forgotten replacement compiles and runs silently.

```
Grep tool with:
  pattern: 'YOUR_CREATION_NAME_HERE|\bYourCreation\b|\bYOUR_CREATION\b'
  glob:    '{engine,creations,test}/**'
  output_mode: 'content'
  -n: true
```

Any hit in real source is a leftover (the tokens belong only inside the
`create-creation` skill's templates). Auto-fix: substitute the real
creation name.
