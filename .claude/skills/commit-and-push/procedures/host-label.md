# Host-stamp label

After `gh pr create` succeeds, stamp a `fleet:authored-on-<host>` label so the
reviewer knows which backend the author already implicitly smoke-tested. Per the
engine `CLAUDE.md` "Verifying render changes" section, render-PR authors are
expected to build and run the demo on their host before opening — so authoring
on a host is reasonable evidence that host's smoke is at least baseline-validated.

This is the **author's host fact**, not a state label. Reviewers read it to skip
the matching `fleet:needs-<host>-smoke` label so the author's host doesn't get
tagged for redundant validation. The OTHER host's smoke label (if needed per the
diff) still gets added — backend drift between OpenGL and Metal is the whole point
of cross-host validation.

For the reviewer-side of this workflow — reading the host label and deciding which
smoke tag to add lives in
[`review-pr/procedures/cross-host-smoke.md`](../../review-pr/procedures/cross-host-smoke.md).

## Shell snippet

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

## Scope

Applies to **all** PRs (engine and game, render-touching or not). The label is
cheap and consistent — having it always present makes the reviewer's logic simple
("subtract author host from smoke labels"). Game PRs don't currently get smoke
labels in the engine-side flow, but the host label is still informational there;
that's fine.

The cross-host smoke flow now covers Linux (OpenGL), macOS (Metal), and Windows
(MSYS2 mingw64 / OpenGL) — see the reviewer-side
[`review-pr/procedures/cross-host-smoke.md`](../../review-pr/procedures/cross-host-smoke.md)
for the author-host subtraction logic. If the host kernel is none of those, the
label is skipped — uncommon hosts don't fit the smoke matrix.
