# Check 6 — hand-rolled demo asset-copy blocks instead of `irreden_bundle_assets`

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff touches
`creations/demos/*/CMakeLists.txt`.

`creations/CLAUDE.md` §"CMake boilerplate" mandates
`irreden_bundle_assets(...)` for demo asset staging. The smell is a
hand-rolled `add_custom_target(*Assets ...)` /
`add_custom_command(... copy_directory ...)` block:

```bash
git diff --name-only origin/master -- 'creations/demos/*/CMakeLists.txt' | while read -r f; do
  grep -qE 'add_custom_(target|command)' "$f" \
    && grep -qE 'copy_directory|copy_if_different' "$f" \
    && echo "HAND-ROLLED asset copy (use irreden_bundle_assets): $f"
done
```

Fix: `irreden_bundle_assets(<target> SCRIPTS <files>)` (+
`irreden_package_target` if a bundle is wanted). Report, don't auto-fix —
the asset list needs a human eye.
