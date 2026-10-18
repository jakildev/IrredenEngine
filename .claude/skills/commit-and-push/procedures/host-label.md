# Post-open stamps: provenance and host

Right after `gh pr create` / `gh pr edit` returns, and after every later push
that precedes review, run the executable gate:

```bash
fleet-pr-stamp <N> --repo <engine|game|owner/repo>
```

Exit 0 plus the printed label line means the PR is open to the fleet; quote
that line in the step-9 report. A non-zero exit means the PR is not open, and
the printed line shows what is missing.

`fleet:author-<runtime>` records the provider that made the change for
[provider routing](../../../../docs/agents/fleet-labels-reference.md#provider-routing).
`fleet:authored-on-<host>` records each author host for cross-host smoke.
Both apply to every engine and game PR, in every stack mode.

`command not found` means this host's `~/bin` predates the tool; run
`scripts/fleet/install.sh` from the engine clone.
