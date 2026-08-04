# Check 2 — function-local `static` in system tick files

Part of the [`simplify`](../SKILL.md) skill's §2b mechanical checks —
run from the index there when the trigger matches. Section references
(§6, §7, §9a, §10) resolve against `../SKILL.md`.

**Trigger:** the diff touches `engine/system/**` or `system_*` files (prefab/creation system headers).

Use Grep to scan system files for `static` declarations that are NOT
`static constexpr` or `static const`, then cross-reference the hits
against `git diff` added lines to confirm the match is newly introduced.

```
Grep tool with:
  pattern: '\bstatic\b(?!\s+constexpr)(?!\s+const\b)'
  glob:    '{engine/prefabs/irreden/**/system_*.{hpp,cpp},engine/system/**/*.{hpp,cpp},creations/**/system_*.{hpp,cpp}}'
  output_mode: 'content'
  -n: true
```

Filter the Grep results to lines that also appear as `+` lines in
`git diff --unified=0` for the same file — those are the newly added
violations. Lines that exist on both sides (pre-existing code) belong
to the live-deviation list and should only be noted, not re-flagged.

For each hit, suggest the canonical `SystemParams` migration pattern:

> Replace `static <T> name;` with a `SystemParams` field. Capture the
> params pointer once at `create()` time and pass into the lambdas by
> value. See [`.claude/rules/cpp-systems.md`](../../../rules/cpp-systems.md)
> "Canonical SystemParams pattern" or
> [`engine/system/CLAUDE.md`](../../../../engine/system/CLAUDE.md) for the
> canonical example.

Live deviations already on the list (don't re-flag, but note in the
report if touched):

- `engine/prefabs/irreden/render/systems/system_entity_canvas_to_framebuffer.hpp:41-43`
- `engine/prefabs/irreden/update/systems/system_gravity.hpp:17`
- `engine/prefabs/irreden/update/systems/system_animation_color.hpp:25-26`

Don't add new violations; do migrate when touching one of the
deviation files for other reasons.
