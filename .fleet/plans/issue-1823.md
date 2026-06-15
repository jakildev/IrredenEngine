# Plan: render/style — sweep `_HPP` include-guard outliers back to `_H`

- **Issue:** #1823
- **Model:** sonnet
- **Date:** 2026-06-15

## Scope
Pure-mechanical rename of the engine's first-party `<NAME>_HPP` include guards
back to `<NAME>_H`, matching the engine-wide convention (the `simplify-check-naming`
enforcement rule landed in the #1780 triage batch). No logic change.

## Verified current state (premise corrected — 2 files, not 3)
Authoritative enumeration —
`grep -rEl '^#ifndef [A-Z0-9_]+_HPP[[:space:]]*$' engine | grep -v third_party` —
returns **exactly two** first-party outliers on current master:
- `engine/prefabs/irreden/demo/components/component_example.hpp` — guard
  `COMPONENT_EXAMPLE_HPP` (lines 1 `#ifndef`, 2 `#define`, 20 `#endif // COMPONENT_EXAMPLE_HPP`).
- `engine/render/include/irreden/render/voxel_pool_config.hpp` — guard
  `IR_RENDER_VOXEL_POOL_CONFIG_HPP` (lines 1, 2, 48 `#endif //`).

**The issue's third file, `engine/prefabs/irreden/render/gui_text_batch.hpp`,
does NOT exist on master** — no file at that path, no `gui_text*` header anywhere,
no git history for the path, and no `GUI_TEXT_BATCH_*` guard macro in the tree.
PR #1774 (merged 2026-06-13) landed the GUI-text batching without that header
under that name, so its `_HPP` guard is already moot. **Sweep the 2 extant
outliers; do not recreate the absent file.**

## One task or a stack? — ONE sonnet PR
Trivial mechanical rename of 2 files. No decomposition.

## Approach (ordered)
1. `component_example.hpp`: rename `COMPONENT_EXAMPLE_HPP` → `COMPONENT_EXAMPLE_H`
   in the `#ifndef` (l1), `#define` (l2), and the trailing `// COMPONENT_EXAMPLE_HPP`
   comment on `#endif` (l20). Three token edits, nothing else.
2. `voxel_pool_config.hpp`: rename `IR_RENDER_VOXEL_POOL_CONFIG_HPP` →
   `IR_RENDER_VOXEL_POOL_CONFIG_H` in `#ifndef` (l1), `#define` (l2), and the
   trailing `#endif //` comment (l48).
3. Sanity-grep each renamed macro name tree-wide before finishing — include guards
   should be referenced nowhere else; confirm zero stray references so the rename
   is self-contained.
4. Build an affected target (`fleet-build --target IRShapeDebug`) to confirm the
   guard rename compiles clean (it is mechanical; this is just a safety net).

## Affected files
- `engine/prefabs/irreden/demo/components/component_example.hpp` — guard token rename.
- `engine/render/include/irreden/render/voxel_pool_config.hpp` — guard token rename.

## Acceptance criteria
- `grep -rEl '^#ifndef [A-Z0-9_]+_HPP[[:space:]]*$' engine | grep -v third_party`
  returns **empty** (zero first-party `_HPP` guard outliers remain).
- Each renamed file's `#ifndef`/`#define`/`#endif`-comment tokens all agree.
- Build green; `simplify-check-naming` passes.

## Out of scope
- Metal/OpenGL backend headers and vendored `third_party/` headers that use
  `#pragma once` — exempt by the enforcement rule; do not touch.
- `gui_text_batch.hpp` — absent from master; no action.

## Gotchas
- Rename **only** the guard tokens (and the matching `#endif` comment) — do not
  touch any other identifier that happens to end in `_HPP`.
- Keep the `#endif` trailing comment in sync with the new token.
- This is render/prefab style work; no behavioural or render-output change, so no
  screenshot/render-verify pass is needed (guard-macro rename only).
