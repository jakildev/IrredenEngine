# Objective: the launcher is how a human builds, runs, and browses creations

**Status:** active

## Outcome
A human on any supported host opens `IRLauncher` and never needs a terminal
to find a creation, read what it demonstrates, build it, run it with a named
preset, see whether the run was clean, open its source in the IDE, or look
at the voxel entities, prefabs and screenshots the engine has produced.

## Done means
- [x] Every tracked creation ships a `creation.json` whose description,
  group and targets the launcher shows; the manifest lint is green in CI.
- [x] Build from the launcher streams the `ir-build` log into the window and
  reports the exit; a broken `build/` never prevents the launcher from
  starting (`ir-launcher` runs the installed copy under `build-launcher/`).
- [x] Run from the launcher hands off through `ir-run`, and the child's
  `RESULT=` verdict is shown on the target row when the launcher resumes.
- [x] Open in IDE opens the creation directory in the configured editor on
  macOS, native Windows, and Linux/WSL.
- [x] A `.vxs` or prefab selected in the asset panel previews live in a
  viewport; a screenshot previews in an image widget.
- [ ] The hand check (open, build `shape_debug`, run it, close it, see the
  verdict, open in IDE) is recorded on each of the three hosts.
- [x] (v2) The launcher catalogs the checkout it was built from plus the
  worktrees configured beside it, shows each creation's root, switches the
  active worktree from a header picker without restarting, and builds and
  runs a worktree's creation in that worktree's own tree; the
  `multi_root` GUI session and the `ToolSpawn` / `CatalogScan` routing rows
  are green.

## Non-goals
Fleet status in the launcher (jobs, PRs, claims); multi-root or worktree
switching in v1 (v2 adds it, limited to the base checkout's immediate
`.claude/worktrees/*`; arbitrary paths and other checkouts stay out); a
package/export button (cross-host-shippability owns it); embedding a running
demo inside the launcher window; renaming existing targets.

## Current state
Design: [`docs/design/engine-launcher.md`](../engine-launcher.md). Rows 1–5
and 7 verify on master `ee85797c0`. Epic #4525's fourteen children merged
between 2026-10-18 and 2026-10-22: manifests and the lint
(`scripts/lint_creation_manifests.py`, run by `engine-tests.yml`; since PR
#4773 its enumeration covers the launcher's own manifest alongside every
tracked CMake creation), the catalog (`engine/catalog`), the streamed
`ir-build` log (`creations/launcher/main.lua`), hand-off through
`ir-run --result-file` with the verdict on the target row
(`creations/launcher/main.lua`, `creations/launcher/scripts/details.lua`),
per-OS Open in IDE (`engine/world/src/tool_spawn.cpp`, proven by
`test/world/tool_spawn_test.cpp`; WSL takes the Linux branch), live previews
(`creations/launcher/scripts/assets.lua`), the persistent dockspace, and `ir-launcher`
supervising the installed copy under `build-launcher/installed/<id>/`
(`engine/tools/bin/ir-launcher`). v2 (§D7-v2) landed with PR #4709 on
2026-10-25: worktree discovery, the header picker, per-root Build / Run /
Package; the `multi_root` session passes 37/37 on macOS and the
`CatalogScanTest` / `ToolSpawnTest` routing rows run under Engine Tests.
The Package button belongs to #4674 under cross-host-shippability (PR
#4703). Umbrella #4525 stays open under `fleet:needs-human`: no host report
is posted, so row 6 is open, and rows 4 and 7 are verified by test only —
CI runs no GUI session and `multi_root` has run only on macOS.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-10-17 | — | objective seeded with the design doc |
| 2026-10-18 | #4525 / #4526 | PR #4535: `creation.json` v1 for every demo and editor; manifest lint in Engine Tests |
| 2026-10-19 | #4525 / #4527, #4528, #4529, #4530 | PRs #4539 (JSON reader, manifest contract), #4542 (catalog scan and build-tree join), #4556 (`spawnTool`, hand-off verdict, `ir-run --result-file`), #4541 (`IRGui.makeImage`) |
| 2026-10-20 | #4525 / #4531, #4532, #4564, #4592, #4595, #4608, #4617, #4637 | PRs #4600 (IRLauncher v1, `ir-launcher`), #4614 (thumbnails, asset browser, live previews), #4609, #4598, #4599, #4633 (editor and open-folder as hand-offs), #4649 (supervisor suite on Windows), #4639 |
| 2026-10-22 | #4675 | v2 Done-means row; worktree roots, the picker and per-root Build / Run (§D7-v2) |
| 2026-10-22 | #4525 / #4601 | PR #4654: `IRLayout` binding, persistent dockspace; steward close-out, 14/14 merged, umbrella parked for the three host reports |
| 2026-10-25 | #4675 / PR #4709 | worktree roots, header picker, per-root actions (§D7-v2); `multi_root` 37/37 on macOS, routing rows green. Row 7 verifies. |
| 2026-10-27 | sweep | re-verified against master `ee85797c0`; rows 1–5 and 7 ticked; row 6 waits on the host reports |
| 2026-10-27 | #4768 / PR #4773 | the manifest lint enumerates the launcher's own manifest beside every tracked CMake creation (sweep finding) |
