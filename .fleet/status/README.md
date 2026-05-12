# `.fleet/status/` — queue-manager-owned status roll-ups

Roll-up prose that tracks rapidly-changing project state across the
fleet (in-flight migrations, deferred follow-ups, API relocations).
Stored here rather than inline in CLAUDE.md sections so feature PRs
that touch the relevant module's CLAUDE.md don't conflict on the
roll-up paragraph.

## Ownership

These files follow the same bookkeeping pattern as `TASKS.md` and
`.fleet/plans/`:

- **Queue-manager** is the sole editor. Maintenance pass updates
  the contents as underlying state changes (PRs merge, task status
  flips, architect-gated decisions resolve).
- **Author / reviewer / worker / merger agents do not edit these
  files in feature PRs.** Read them when a CLAUDE.md pointer
  directs you to one; never include them in a feature PR's diff.
- **Cursor / human-in-the-loop edits** are fine — the human is the
  source of truth and may update any of these directly.

The bookkeeping carve-out in `role-queue-manager.md` "Hard rules"
covers `.fleet/status/*.md` so the queue-manager can push status
updates straight to master alongside `TASKS.md` and
`.fleet/plans/*.md` without going through PR review.

## Files

- `modifier-runtime-gaps.md` — modifier framework follow-ups
  (architect-gated runtime escape hatches, perf optimizations not
  yet justified by profile data).
- `render-api-relocations.md` — features still on the `IRRender::`
  surface that should move to feature-specific prefab namespaces.

## Adding a new status file

When a "list of in-flight items" pattern would otherwise grow inline
in some CLAUDE.md, add a new file here instead. Pick a kebab-case
filename keyed by area + topic. Reference it from the relevant
CLAUDE.md with the canonical pointer shape:

```
See `.fleet/status/<file>.md` (queue-manager-owned; feature PRs do
not edit) for <one-line summary of what it tracks>.
```

The pointer line itself is rate-stable: the file name doesn't
change, only the file's contents do, so feature PRs touching the
same module's CLAUDE.md don't collide on the pointer.
