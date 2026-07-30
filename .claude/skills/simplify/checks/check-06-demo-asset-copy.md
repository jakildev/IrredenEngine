# Check 6 — hand-rolled demo asset-copy blocks instead of `irreden_bundle_assets`

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff touches `creations/demos/*/CMakeLists.txt`.

`creations/CLAUDE.md` §"CMake boilerplate" mandates `irreden_bundle_assets(...)`
for demo asset staging, but the rule is easy to miss when copy-pasting an older
demo's `CMakeLists.txt`. The smell is a hand-rolled `add_custom_target(*Assets
...)` / `add_custom_command(... copy_directory ...)` block in a changed
`creations/demos/*/CMakeLists.txt` (#1870's fog_demo shipped 38 such lines):

```bash
git diff --name-only origin/master -- 'creations/demos/*/CMakeLists.txt' | while read -r f; do
  grep -qE 'add_custom_(target|command)' "$f" \
    && grep -qE 'copy_directory|copy_if_different' "$f" \
    && echo "HAND-ROLLED asset copy (use irreden_bundle_assets): $f"
done
```

Fix: replace the block with `irreden_bundle_assets(<target> SCRIPTS <files>)`
(+ `irreden_package_target` if a bundle is wanted). Report, don't auto-fix —
the SCRIPTS / asset list needs a human eye.
