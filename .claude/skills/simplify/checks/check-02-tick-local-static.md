# Check 2 — function-local `static` in system tick files

Part of [`simplify`](../SKILL.md) §2b. **Trigger:** the diff touches
`engine/system/**` or `system_*` files (prefab/creation system headers).

```
Grep tool with:
  pattern: '\bstatic\b(?!\s+constexpr)(?!\s+const\b)'
  glob:    '{engine/prefabs/irreden/**/system_*.{hpp,cpp},engine/system/**/*.{hpp,cpp},creations/**/system_*.{hpp,cpp}}'
  output_mode: 'content'
  -n: true
```

Keep only hits that are `+` lines in `git diff --unified=0`; pre-existing
hits belong to the live-deviation list and are noted, not re-flagged.

For each hit: replace `static <T> name;` with a `SystemParams` field,
captured once at `create()` and passed into the lambdas by value
([`.claude/rules/cpp-systems.md`](../../../rules/cpp-systems.md)
"Canonical SystemParams pattern";
[`engine/system/CLAUDE.md`](../../../../engine/system/CLAUDE.md)).

Live deviations (note if touched; migrate when editing the file anyway):

- `engine/prefabs/irreden/render/systems/system_entity_canvas_to_framebuffer.hpp:41-43`
- `engine/prefabs/irreden/update/systems/system_gravity.hpp:17`
- `engine/prefabs/irreden/update/systems/system_animation_color.hpp:25-26`
