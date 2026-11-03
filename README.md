# media — PR screenshot and evidence store

Orphan branch, never merged into `master`. `scripts/fleet/fleet-media-push
<dir>` (on master) appends one commit per publish under
`docs/pr-screenshots/<branch>/`, and PR bodies embed raw URLs pinned to that
commit's SHA. Never force-push, rewrite or delete this branch: every pinned
URL depends on its history.

The root commit carries master's `docs/pr-screenshots/` tree as it stood when
that folder left master, so this branch is the one browsable gallery of every
PR's captures.
