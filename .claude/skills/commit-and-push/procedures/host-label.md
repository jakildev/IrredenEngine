# Host-stamp label

After `gh pr create`, stamp `fleet:authored-on-<host>` — the author's host
fact, not a state label. Render-PR authors build and run the demo on their
host before opening (`engine/render/CLAUDE.md` "Verifying render
changes"), so the reviewer's
[cross-host-smoke procedure](../../review-pr/procedures/cross-host-smoke.md)
subtracts this host's tier and tags only the others.

```bash
host_kernel=$(uname -s)
case "$host_kernel" in
    Linux)                host_label="fleet:authored-on-linux" ;;
    Darwin)               host_label="fleet:authored-on-macos" ;;
    MINGW*|MSYS*|CYGWIN*) host_label="fleet:authored-on-windows" ;;
    *)                    host_label="" ;;
esac
if [[ -n "$host_label" ]]; then
    gh pr edit <N> --add-label "$host_label"
fi
```

Applies to every PR, engine and game, render-touching or not; game PRs
get no smoke labels from the engine-side flow, but the host label is still
informational there. An unrecognized host gets no label.
