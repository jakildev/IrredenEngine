# Single-task base resolution + `--stackable-on`

**Single-task mode** is the default — neither [fleet stack mode](fleet-stack.md)
nor [cursor stack mode](cursor-stack.md) is in effect. One path covers
both cases: a normal fleet claim or plain human PR bases on `master`; an
opportunistic `--stackable-on` claim (the idle-worker fallback tier — a
task whose single `**Blocked by:**` blocker has an open PR) bases on the
blocker's branch. `fleet-claim claim-base` returns the right value either
way, so always resolve the base the same way and let the value decide.

## Resolve the issue number, then the base

1. Issue number: `fleet-claim reservation-of "<your-worktree-name>"`
   (the claim auto-reserves the worktree↔task binding); fallback: parse
   `<N>` from a `claude/<N>-<topic>` branch name. No numeric `<N>` (a
   plain human / ad-hoc PR) → base is `master`, no label; standard step-8
   flow.
2. Base:
   ```bash
   base=$(fleet-claim claim-base "<N>")   # "master" for a normal claim; the blocker's branch for --stackable-on
   ```
   A no-op for normal claims (no `.meta` sidecar → prints `master`), so
   safe to run unconditionally.

## Open (or reconcile) the PR — idempotent

A fleet-worker WIP PR is usually already open (created at claim time;
`docs/agents/FLEET.md` §"Single-task base resolution (`claim-base`)"), so
ensure the invariant rather than assume creation — repair base/label if
the claim-time open missed them:

```bash
branch=$(git branch --show-current)
author_label="fleet:author-<claude|codex>"   # substitute the actual runtime from step 8c

existing=$(gh pr list --head "$branch" --state open --json url -q '.[0].url')
# Publication must use the exact body file that passed fleet-pr-body-lint.
if [[ -n "$existing" ]]; then
    gh pr edit "$existing" --base "$base" --add-label "$author_label" \
        --body-file .pr-body.md
else
    gh pr create --base "$base" --label "fleet:wip" --label "$author_label" \
        --title "<scope>: <title> (#<N>)" --body-file .pr-body.md
fi
```

Write `.pr-body.md` ([pr-body.md](pr-body.md)) with the Write tool before
this fence runs — never `--body "$(cat <<EOF …)"`.

When `base != master`, run [native-stack-link.md](native-stack-link.md)
with `$base` and the PR number. No `Stacked on:` body line.

## Notes

- No `stack-set-pr` — that bookkeeping is molecule-only
  ([fleet-stack.md](fleet-stack.md)). Single-task mode only *reads*
  `claim-base`.
- `fleet:wip` is unchanged: the claim-time PR is WIP; the finalize step
  removes it for reviewer pickup.
- GitHub owns the base after the link. Set the base only from the
  *current* `claim-base` value at push time; never re-stack a PR GitHub
  has already re-targeted.
- No stack label — membership is the native stack object
  (`docs/design/native-stacked-prs-migration.md`).
