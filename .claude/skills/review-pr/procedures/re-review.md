# Re-review

Fires on "re-review PR <N>", on `fleet:changes-made` on a PR this loop
flagged `fleet:needs-fix` / `fleet:blocker`, or when asked to confirm the
nits of a `fleet:has-nits` review were cleaned up. A first review runs the
standard flow instead.

1. `gh pr checkout <N>` again.
2. Verify the previously-flagged items **before** the checklist:
   ```bash
   gh pr view <N> --comments
   gh api repos/jakildev/IrredenEngine/pulls/<N>/comments \
       --jq '.[] | "[\(.path):\(.line // .original_line)] \(.body)"'
   git rev-parse --short HEAD
   ```
   Find this fleet's prior review (`## Review —` header, `🤖 Reviewed by`
   footer); extract every `<path>:<line>` / `<path>` from its Blockers,
   Needs-fix, and Nits, plus every inline comment. Read each site at HEAD
   and classify it. The new review body opens with:
   ```
   ### Prior-review resolution
   - ✅ `path:line` — <prior issue> — verified fixed at <SHA>
   - ❌ `path:line` — <prior issue> — still present; re-flagged below
   - ↗ `old_path:old_line` — <prior issue> — moved to `new_path:new_line`; re-flagged below
   ```
   Every prior Blocker, Needs-fix, and Nit appears in one of the three
   states.
3. `git log origin/master..HEAD --oneline`, scoped to commits after the
   prior review's `created_at`. Run the checklist on that delta only,
   carrying forward still-open and moved items; never re-raise a confirmed
   fix, and never re-apply `fleet:needs-fix` / `fleet:blocker` without
   having walked the resolution table — new commits that fix everything
   lift the verdict to approve.
4. Post with the resolution table first, then new findings; shared-flow
   steps 5, 5b, 6 as usual.
