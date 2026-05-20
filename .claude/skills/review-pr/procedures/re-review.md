# Re-review

Procedure for re-reviewing a PR after the author-agent has addressed prior review feedback. Fires when the user says "re-review PR 42", or when a reviewer-loop session sees `fleet:changes-made` on a previously-flagged PR. Skipped for first-pass reviews.

## When to invoke

Triggers:
- User says "re-review PR <N>".
- Reviewer-loop session sees `fleet:changes-made` on a PR it previously flagged with `fleet:needs-fix` or `fleet:blocker`.
- User asks to verify that nits from a prior `fleet:has-nits` review were cleaned up.

If this is the first review of the PR, return to [`SKILL.md`](../SKILL.md) step 1 and run the standard flow instead.

## Flow

### 1. Check out the updated branch

`gh pr checkout <N>` again — it pulls the latest commits onto the already-checked-out branch.

### 2. Verify previously-flagged items first — before running the checklist

This is the most important step. Skipping it causes false-positive re-flags.

a. Fetch the prior review body (run both commands in parallel — first gets conversation-level comments, second gets inline review comments):

```bash
gh pr view <N> --comments
gh api repos/jakildev/IrredenEngine/pulls/<N>/comments \
    --jq '.[] | "[\(.path):\(.line // .original_line)] \(.body)"'
```

Identify the review comment that was posted by this fleet (look for the `## Review —` header and `🤖 Reviewed by` footer).

b. Extract every `<path>:<line>` or `<path>` reference from the **Blockers**, **Needs-fix**, and **Nits** sections of that prior review. Inline comments from the second command are also flagged items — treat them the same way.

c. Get the HEAD commit SHA for attribution:

```bash
git rev-parse --short HEAD
```

d. For each flagged item, read the relevant portion of the file at HEAD using the **Read tool** with `offset` near the flagged line. Determine:
   - **Fixed** — the issue described in the prior review is no longer present at that location (and not obviously moved elsewhere).
   - **Still open** — the issue is still present at that location.
   - **Location changed** — the line moved; the issue exists at a new path/line.

e. Write a **Prior-review resolution** section that will open the new review body. Format:

```
### Prior-review resolution
- ✅ `path:line` — <prior issue summary> — verified fixed at <SHA>
- ❌ `path:line` — <prior issue summary> — still present; re-flagged below
- ↗ `old_path:old_line` — <prior issue summary> — moved to `new_path:new_line`; re-flagged below
```

Every **Blocker**, **Needs-fix**, and **Nit** item from the prior review must appear in one of these three states. (Praise and test-plan items don't require tracking.) Do NOT silently re-flag an item without first checking whether it was fixed.

### 3. Read the new commits only

`git log origin/master..HEAD --oneline` lists all commits on the PR branch since it diverged from master. Scope to the subset that arrived **after the prior review's timestamp** — the prior review comment's `created_at` (visible in the `gh pr view --comments` output from step 2a) is the cutoff. Commits older than that were already reviewed; commits newer than that are the delta to inspect. Avoid re-examining already-reviewed code — focus the checklist on what changed.

**Re-apply guard:** If new commits are present since the last review, you MUST work through every previously-flagged item in the resolution table (step 2e) before confirming the old verdict. Never re-apply `fleet:needs-fix` or `fleet:blocker` without checking whether the new commits actually address the issue. If new commits clearly fix all flagged items, the verdict may improve to `approve` even if prior iterations set `fleet:needs-fix`.

### 4. Run the full fresh-eyes checklist

Run `SKILL.md` step 4 (the Irreden-Engine-specific review checklist) against the new commits. Carry forward any "Still open" or "Location changed" items from step 2e. Do **not** re-raise items already confirmed fixed in the resolution table.

### 5. Post the review

Open the review body with the Prior-review resolution table (step 2e), then present any new findings and any carried-forward open items. If all prior items are fixed and no new issues appear, the verdict is approve. Follow the same body format and label steps as the main flow (`SKILL.md` steps 5, 5b, 6).
