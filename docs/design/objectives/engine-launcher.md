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

## Non-goals
Fleet status in the launcher (jobs, PRs, claims); multi-root or worktree
switching in v1; a package/export button (cross-host-shippability owns it);
embedding a running demo inside the launcher window; renaming existing
targets.

## Current state
Design: [`docs/design/engine-launcher.md`](../engine-launcher.md). Nothing
in the tree yet: no creation carries a manifest, the engine has no JSON
reader, no tool-spawn service, and no image widget. Hand-off
(`IREngine.handOff`, #4473) and the widget set are the pieces the launcher
builds on.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-10-17 | — | objective seeded with the design doc |
