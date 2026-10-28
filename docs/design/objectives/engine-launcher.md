# Objective: the launcher is how a human builds, runs, and browses creations

**Status:** active

## Outcome
A human on any supported host opens `IRLauncher` and never needs a terminal
to find a creation, read what it demonstrates, build it, run it with a named
preset, see whether the run was clean, open its source in the IDE, or look
at the voxel entities, prefabs and screenshots the engine has produced.

## Done means
- [ ] Every tracked creation ships a `creation.json` whose description,
  group and targets the launcher shows; the manifest lint is green in CI.
- [ ] Build from the launcher streams the `ir-build` log into the window and
  reports the exit; a broken `build/` never prevents the launcher from
  starting (`ir-launcher` runs the installed copy under `build-launcher/`).
- [ ] Run from the launcher hands off through `ir-run`, and the child's
  `RESULT=` verdict is shown on the target row when the launcher resumes.
- [ ] Open in IDE opens the creation directory in the configured editor on
  macOS, native Windows, and Linux/WSL.
- [ ] A `.vxs` or prefab selected in the asset panel previews live in a
  viewport; a screenshot previews in an image widget.
- [ ] The hand check (open, build `shape_debug`, run it, close it, see the
  verdict, open in IDE) is recorded on each of the three hosts.
- [ ] (v2) The launcher catalogs the checkout it was built from plus the
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
Design: [`docs/design/engine-launcher.md`](../engine-launcher.md). The v1
launcher has landed through epic #4525: manifests, the catalog, the
tool-spawn service, hand-off with verdicts, previews, the dockspace and the
Package button. The manifest lint covers the launcher alongside every tracked
CMake creation. v2's first slice, worktree roots and the picker (§D7-v2),
lands with #4675; the per-host hand checks remain open.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-10-17 | — | objective seeded with the design doc |
| 2026-10-22 | #4675 | v2 Done-means row; worktree roots, the picker and per-root Build / Run (§D7-v2) |
