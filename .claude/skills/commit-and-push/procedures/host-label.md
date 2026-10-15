# Post-open stamps: provenance and host

Right after `gh pr create` / `gh pr edit` returns, and after every later
push that precedes a review, stamp two author facts and read them back.
Neither is a state label.

- `fleet:author-<runtime>` — the provider that made the change. The
  cross-provider review router reads it to pick the opposite provider; a
  PR without it is refused on every tick and never dispatched for review.
  `fleet-runtime stamp` takes the runtime from `FLEET_RUNTIME` (the
  dispatcher exports it) and uses `claude` when it is unset, so an
  interactive Claude Code session needs no `--runtime`.
- `fleet:authored-on-<host>` — the author's host. Render-PR authors build
  and run the demo on their host before opening (`engine/render/CLAUDE.md`
  "Verifying render changes"), so the reviewer's
  [cross-host-smoke procedure](../../review-pr/procedures/cross-host-smoke.md)
  subtracts this host's tier and tags only the others.

```bash
fleet-runtime stamp <N> --repo <owner/name>

host_kernel=$(uname -s)
case "$host_kernel" in
    Linux)                host_label="fleet:authored-on-linux" ;;
    Darwin)               host_label="fleet:authored-on-macos" ;;
    MINGW*|MSYS*|CYGWIN*) host_label="fleet:authored-on-windows" ;;
    *)                    host_label="" ;;
esac
if [[ -n "$host_label" ]]; then
    gh pr edit <N> --repo <owner/name> --add-label "$host_label"
fi

gh pr view <N> --repo <owner/name> --json labels \
    --jq '[.labels[].name] | map(select(startswith("fleet:author")))'
```

The read-back is the gate. It must print exactly one of
`fleet:author-claude` / `fleet:author-codex` (never both, never neither)
and, on a recognized host, the host label — for example
`["fleet:author-claude","fleet:authored-on-macos"]`. If the provenance
entry is missing, run the stamp again and read back again before
reporting; a PR reported as open without that line is not open to the
fleet. Quote the line in the step-9 report.

Applies to every PR, engine and game, render-touching or not, in every
stack mode; game PRs get no smoke labels from the engine-side flow, but
both stamps are still read there. An unrecognized host gets no host label
and still gets the provenance stamp.
