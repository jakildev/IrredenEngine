---
name: review-pr
description: >-
  Review an open GitHub PR on the Irreden Engine repo and post a structured
  review. Use when the user says "review PR <N>", "review #<N>", "review the
  last PR", "check my PR", "review any new PRs", "review the PR queue", "check
  for new PRs to review", "review the open PRs", or otherwise asks for a code
  review — and also invoke automatically from a persistent reviewer-loop session
  when `gh pr list` surfaces a PR that has not yet been reviewed by this fleet.
  Designed to be invoked inside a dedicated "reviewer" worktree/agent whose job
  is to look at another agent's work with a fresh context. Writes a structured
  review covering ownership, ECS invariants, allocation hot paths, naming,
  tests, and project-specific smells, then posts it as a PR comment.
---

# review-pr

Runs a code review on a Github PR using Irreden-Engine-specific criteria.
Intended for a dedicated **reviewer agent** running in its own worktree — one
whose context window is *not* polluted by the code it is reviewing.

## When to invoke

Trigger when the user says:

- "review PR 42" / "review #42"
- "review the last PR" / "review the open PR"
- "check my PR" / "give me a review"
- "review any new PRs" / "review the PR queue" / "check for new PRs to review"
- Any phrase implying: look at this PR and tell me what's wrong.

**Also trigger from a persistent reviewer-loop session**, where the session's
own launch prompt told it to poll `gh pr list` on an interval and review
anything new. In that mode the reviewer agent resolves the set of unreviewed
PRs itself and invokes this skill once per PR without the user typing a fresh
phrase each time. The loop pattern is documented in `docs/AGENT_FLEET_SETUP.md`.

Do **not** invoke proactively inside an unrelated working session — e.g. while
an author-agent is mid-refactor on its own PR, don't auto-jump into reviewing
a third-party PR you happen to notice. The bar is: either the user asked, or
this session's whole job is to be a reviewer.

## Model expectations (two-tier review)

This skill runs on either Sonnet or Opus. The two roles are different:

- **Sonnet first pass** — cheap and fast, catches the 80% of issues that
  are style, obvious bugs, missing null checks, naming inconsistencies,
  untested branches. A Sonnet reviewer should be thorough on the
  checklist below but explicitly call out "I am not confident on this
  invariant, please Opus-review:" for anything that looks subtle
  (lifetime, concurrency, an ECS invariant three systems deep, a GPU
  buffer handoff, an ffmpeg/audio-thread interaction).
- **Opus second pass (or sole pass)** — for any PR touching core engine
  invariants (`engine/render/`, `engine/entity/`, `engine/system/`,
  `engine/world/`, `engine/audio/`, `engine/video/`, `engine/math/`),
  anything that looks performance-sensitive, any GPU/CPU sync, anything
  concurrency-touching, or any PR where the Sonnet first-pass review
  flagged an Opus escalation. Opus also confirms that earlier Sonnet
  nits were properly addressed.

When running as Sonnet: finish the review, then in the verdict summary
mention whether Opus escalation is needed. When running as Opus on a PR
that already has a Sonnet review: read the Sonnet review first, treat
it as a first pass, and focus on what Sonnet could not confirm. See
the top-level `CLAUDE.md` for the full model split.

## Preconditions

1. `gh` authenticated (`gh auth status`).
2. User supplied a PR number, PR URL, or said "latest" — resolve before starting.
3. You are **not** the same agent that wrote the code. If the user is asking
   the author agent to review its own work, warn them and offer to run the
   review anyway with the caveat that author-agents have tunnel vision.

## Flow

### 1. Resolve the PR and pull its metadata

```bash
gh pr view <N> --json number,title,body,headRefName,baseRefName,author,files,additions,deletions,commits
gh pr diff <N>
```

If the user said "latest" / "most recent":

```bash
gh pr list --state open --limit 5
```

Pick the top result, confirm the title with the user if there's ambiguity.

### 1b. Check whether the PR is stacked

Every fleet PR today is single-task. When workers claim a dependency
chain via `fleet-claim stack`, they produce a sequence of single-task
PRs where each one's `--base` points at the previous task's branch
instead of `master` — GitHub calls these "stacked PRs". The review
pass is still per-PR; you don't re-review the parent PR as part of
reviewing its child.

Detect stacking from the metadata already fetched in step 1:

- **Base branch** (`baseRefName`) is not `master` → stacked on the PR
  whose head is that branch.
- **Body** contains a `Stacked on:` line → confirms it, and the line
  gives you the parent PR URL for the review-body callout.

If stacked:

- Review **only this PR's own diff**. `gh pr diff <N>` already scopes
  to the changes this PR introduces on top of its base branch — it
  does NOT include the parent's changes. Trust that output; don't
  manually expand the range.
- Note the stack context in the review body, e.g. "Stacked on
  #<parent>; approval assumes #<parent> lands first." Reviewers
  (and the human merger) use that line to sequence merges.
- Do **not** read, cite, or re-verify the parent PR's diff. It has
  its own independent review and label. Cross-contamination between
  stacked PRs' reviews is the main failure mode to avoid.
- Verdict and label are set for this PR alone, the same way as a
  non-stacked PR.

If the base is `master` and there's no `Stacked on:` line, this is a
standalone PR — proceed with the rest of the flow as normal.

### 2. Check out the PR branch locally (read-only)

```bash
gh pr checkout <N>
```

This puts the reviewer worktree on the PR's head branch. You still have full
read access to the rest of the repo for cross-reference. Do **not** commit or
push from this worktree — you are a reader, not a writer.

### 3. Read the diff in context

For each changed file:

- Read the **full file**, not just the diff hunks. ECS and render bugs often
  hide in the surrounding code.
- Cross-reference any component or system name against
  `engine/prefabs/irreden/**/components/` and `**/systems/` to make sure the
  change honors existing conventions.
- If a shader changed, also read the CPU-side frame-data struct that feeds
  it (GLSL layouts and C++ structs in `engine/render/include/irreden/render/`
  must stay in sync).

Keep a running mental list of issues, ranked by severity:

- **Blocker** — will crash, corrupt data, violate ECS invariants, leak
  memory, or break the build.
- **Needs-fix** — correctness or performance issue that must be addressed
  before merge.
- **Nit** — style, naming, minor simplification, docs.
- **Praise** — non-obvious good decision worth calling out so the author-agent
  keeps doing it.

### 4. Apply the Irreden-Engine-specific review checklist

Go through each of these explicitly. For every item, either confirm
compliance or raise an issue.

**ECS invariants**
- ❌ Per-entity `getComponent` / `getComponentOptional` inside a system tick
  function. Fix: add the component to the system's template parameters.
- ❌ `createEntity` / `removeComponent` called mid-iteration in a system
  tick without using the deferred variant.
- ❌ Allocating memory (new, std::vector push in hot loop, std::string
  concat) in per-entity tick paths.
- ❌ New prefab system that isn't added to the `SystemName` enum in
  `engine/system/include/irreden/ir_system_types.hpp`.
- ❌ New component that isn't `C_`-prefixed, or whose public members don't
  have a trailing `_`.

**Ownership / lifetime**
- ❌ `shared_ptr` where `unique_ptr` would do.
- ❌ Raw owning pointers (raw pointer = non-owning, always).
- ❌ Storing references or pointers to ECS component storage across ticks —
  archetype changes invalidate addresses.
- ❌ Capturing `this` or references to World managers in lambdas that outlive
  the World (e.g. lua callbacks registered before World teardown).

**Render pipeline**
- ❌ CPU frame-data struct out of sync with its GLSL `layout(std140)` counter-
  part.
- ❌ New shader file not following the `c_` / `v_` / `f_` / `g_` prefix.
- ❌ Canvas allocation before the canvas entity exists.
- ❌ Compute dispatch size doesn't match `voxelDispatchGridForCount()`.

**Math / coordinates**
- ❌ Mixing 3D world coords with iso 2D coords without going through
  `IRMath::pos3DtoPos2DIso` or a named helper.
- ❌ Hard-coded `x + y + z` where `IRMath::pos3DtoDistance` exists.
- ❌ PlaneIso axis swapped.

**Naming / style**
- ❌ `m_` on public members, trailing `_` on private members (backwards).
- ❌ `MinimapDetail`-style feature-specific helper namespaces instead of
  `detail`.
- ❌ Abbreviations in new names where a full word would read more clearly.
- ❌ Anonymous namespaces in headers.

**Tests / build**
- ❌ Code change with no corresponding test change where a test existed.
- ❌ New feature with no new test at all (flag as needs-fix unless the user
  explicitly said "no tests").
- ❌ Build or format-check not run before opening the PR (check commit
  message for mention, or run `cmake --build build --target format-check`
  yourself if cheap).

**Creation- or implementation-specific criteria**
- If the PR touches a subdirectory under `creations/` (or any other
  implementation layered on top of the engine), read the nearest
  `CLAUDE.md` in that subdirectory *in addition to* this engine-level
  checklist. Each creation/implementation defines its own domain-specific
  review rules — e.g. a game creation may have rules about its simulation
  model, an editor may have rules about UX, a test harness may have rules
  about coverage. The engine-level checklist above is the baseline; the
  creation's own `CLAUDE.md` is the delta. Apply both.
- If the subdirectory has a dedicated `REVIEW.md`, prefer that over its
  `CLAUDE.md` for the review-specific rules (some creations split the
  two). If neither exists, the engine-level checklist is the only bar.

### 5. Write the review

Post the review as a PR comment via `gh pr review`. **Do NOT use
`--body "$(cat <<'EOF'...)"` or any `$(...)` command substitution** —
it triggers Claude Code's `command_substitution` security gate and
causes parse errors when the body contains backticks or special
characters. Instead, write the body to a temp file with the **Write
tool**, then pass it with `--body-file`:

1. Use the **Write tool** to write the review body to `.review-body.md`
   in the worktree root (NOT `/tmp/` — the sandbox may block writes
   outside the project tree). This file is gitignored:

```markdown
## Review — <title>

**Verdict:** <approve | needs-fix | blocker>

### Blockers
- <path:line> — <issue> — <suggested fix>

### Needs-fix
- <path:line> — <issue> — <suggested fix>

### Nits
- <path:line> — <nit>

### Praise
- <non-obvious good decision, if any>

### Test plan the author should run before merge
- [ ] <...>
- [ ] <...>

🤖 Reviewed by Claude Opus 4.6 (review-pr skill)
```

2. Post the review:

```bash
gh pr review <N> --comment --body-file .review-body.md
```

Rules for the review body:

- Cite **file path and line number** for every issue. Reviewers who say
  "there's a bug in the render system" with no cite get ignored.
- For each blocker/needs-fix, suggest a concrete fix, not just "this is
  wrong". The author-agent will use your suggestion literally.
- Empty sections are fine — drop them, don't write "None".
- Verdict options:
  - **approve** — no blockers, no needs-fix. Nits only, if any.
  - **needs-fix** — one or more needs-fix items. No blockers.
  - **blocker** — at least one blocker. Merging would break master.

**The bright line between Nits and needs-fix:**

A "Nit" is a **truly optional** improvement the author may skip without
hurting the merge. Anything you describe with phrases like "must resolve
before merge", "pre-merge ask", "the comment and code must agree",
"safe to merge once X is resolved", or "needs to be reconciled" is
**by definition NOT a Nit** — it is a **needs-fix** item. Move it. The
verdict drops to `needs-fix`.

The contradiction "approve, but please fix X before merge" is forbidden.
If X must be fixed before merge, the verdict is needs-fix; if it doesn't,
say so and stop putting it in the body. The author and the human both
read the verdict label as the primary signal — equivocating in the body
defeats the workflow.

**Nits are still encouraged** — author-agents now scan approved PRs for
any "Nits" section and address every item before considering the PR
landed (see the role files). So put real nits in the Nits section
freely; they will get acted on. The bar isn't "is this nit worth
mentioning?" — it's "is this nit a merge blocker or not?"

**Do not use `gh pr review --approve` or `--request-changes`.** All fleet
agents share the same GitHub account, and GitHub's API rejects formal
review actions on your own PRs. The `--comment` review above is sufficient;
the verdict line in the body is what the human reads to decide whether to
merge. Merging is always the user's call.

### 5b. Set the PR label to match the verdict

**This step is non-negotiable.** A review without a verdict label is
invisible to the human's merge queue — they filter PRs by label, not
by review body. Posting a review and then exiting without setting the
label leaves the PR in limbo (observed in production on PR #230, where
both first-pass and re-review approved but the agent only described
the label in the body and never ran the gh command — PR sat unlabeled
for hours).

Your VERY NEXT bash call after `gh pr review --comment ...` MUST be
the `gh pr edit ... --add-label` below. Don't move on to the next PR
or invoke any other skill until you've confirmed the label is set
(verify with `gh pr view <N> --json labels --jq '.labels[].name'` if
you need to be sure).

**Always remove stale verdict labels before adding the new one** —
a PR should have exactly one verdict label (`fleet:approved` /
`fleet:needs-fix` / `fleet:blocker`) at any time. The
`fleet:has-nits` label is orthogonal — it can ride on top of
`fleet:approved`.

```bash
# For approve, no nits in body:
gh pr edit <N> --remove-label "fleet:needs-fix" --remove-label "fleet:blocker" --remove-label "fleet:has-nits" --add-label "fleet:approved"

# For approve WITH a non-empty Nits section in the body:
gh pr edit <N> --remove-label "fleet:needs-fix" --remove-label "fleet:blocker" --add-label "fleet:approved" --add-label "fleet:has-nits"

# For needs-fix (nits roll into the fix work; no separate label):
gh pr edit <N> --remove-label "fleet:approved" --remove-label "fleet:blocker" --remove-label "fleet:has-nits" --add-label "fleet:needs-fix"

# For blocker:
gh pr edit <N> --remove-label "fleet:approved" --remove-label "fleet:needs-fix" --remove-label "fleet:has-nits" --add-label "fleet:blocker"
```

The verdict label is the **primary signal** the human uses to decide
what to merge. The comment body has the details. The `fleet:has-nits`
label tells the author "you've been approved, and there are nits to
clean up before this lands" — author roles poll for it and address
the nits without losing the approval.

### 5c. Tag render PRs for cross-host smoke validation

Engine render PRs get built and run on whichever backend the author
happened to be using (OpenGL on Linux, or Metal on macOS). The other
backend's build + smoke path is not exercised until a fleet agent on
that host picks the PR up. The `fleet:needs-<host>-smoke` labels
surface that outstanding work so no render PR merges unvalidated on
either backend.

After setting the verdict label in 5b, check the diff's file paths:

```bash
gh pr diff <N> --name-only
```

If any path matches **any** of these, add both smoke labels:

- `engine/render/`
- `engine/prefabs/irreden/render/`
- `engine/render/src/shaders/`
- any `*.glsl` file
- any `*.metal` file

```bash
gh pr edit <N> --add-label "fleet:needs-linux-smoke" --add-label "fleet:needs-macos-smoke"
```

Each host's author agents (opus-worker, sonnet-author) poll for the
label matching their host, run a clean-checkout build + `IRShapeDebug`
smoke, and remove the label on success. While either label persists,
the human should hold the merge — that's the whole point of the
tally.

**Skip the tagging step for:**

- Game-repo PRs — the game's render pipeline uses the engine's
  backend, so cross-host applies at engine level only.
- Non-render engine PRs (tooling, docs, `.claude/`, non-render
  modules like `engine/system/`) — these don't exercise backends and
  don't benefit from cross-host smoke.

If both labels are already present from a prior reviewer pass, no
action needed — the `--add-label` call is a no-op when the label is
already set.

### 6. Report back

Reply with a compact summary to the calling session:

- PR number + title + verdict.
- Count of blockers / needs-fix / nits.
- Link to the review comment (`gh pr view <N> --json reviews` to fetch).
- One sentence on what the author-agent should do next (e.g. "address the
  two blockers around entity lifetime, then re-ping for re-review").

## Anti-patterns

- ❌ Reviewing only the diff, not the surrounding file. Context matters.
- ❌ Vague review comments without file:line citations.
- ❌ Approving work that violates an ECS invariant "because the test passes"
  — some invariants don't fail at test time.
- ❌ Merging the PR yourself. The user merges.
- ❌ Pushing commits to the PR branch from the reviewer worktree.
- ❌ Leaving a review that just says "LGTM" with nothing specific. Either the
  PR is actually clean (in which case call out one non-obvious good choice
  as **praise**, to reinforce it) or it isn't.

## Re-review

If the user says "re-review PR 42" after the author-agent has addressed
comments:

1. **Check out the updated branch.** `gh pr checkout <N>` again — it pulls
   the latest commits onto the already-checked-out branch.

2. **Verify previously-flagged items first — before running the checklist.**
   This is the most important step. Skipping it causes false-positive re-flags.

   a. Fetch the prior review body (run both commands in parallel — first gets
      conversation-level comments, second gets inline review comments):
      ```
      gh pr view <N> --comments
      gh api repos/jakildev/IrredenEngine/pulls/<N>/comments \
          --jq '.[] | "[\(.path):\(.line // .original_line)] \(.body)"'
      ```
      Identify the review comment that was posted by this fleet (look for
      the `## Review —` header and `🤖 Reviewed by` footer).

   b. Extract every `<path>:<line>` or `<path>` reference from the
      **Blockers**, **Needs-fix**, and **Nits** sections of that prior review.
      Inline comments from the second command are also flagged items — treat
      them the same way.

   c. Get the HEAD commit SHA for attribution:
      ```
      git rev-parse --short HEAD
      ```

   d. For each flagged item, read the relevant portion of the file at HEAD
      using the **Read tool** with `offset` near the flagged line. Determine:
      - **Fixed** — the issue described in the prior review is no longer present
        at that location (and not obviously moved elsewhere).
      - **Still open** — the issue is still present at that location.
      - **Location changed** — the line moved; the issue exists at a new path/line.

   e. Write a **Prior-review resolution** section that will open the new review
      body. Format:
      ```
      ### Prior-review resolution
      - ✅ `path:line` — <prior issue summary> — verified fixed at <SHA>
      - ❌ `path:line` — <prior issue summary> — still present; re-flagged below
      - ↗ `old_path:old_line` — <prior issue summary> — moved to `new_path:new_line`; re-flagged below
      ```
      Every **Blocker**, **Needs-fix**, and **Nit** item from the prior review
      must appear in one of these three states. (Praise and test-plan items
      don't require tracking.) Do NOT silently re-flag an item without first
      checking whether it was fixed.

3. **Read the new commits only.** `git log origin/master..HEAD --oneline` lists
   all commits on the PR branch since it diverged from master. Scope to the
   subset that arrived **after the prior review's timestamp** — the prior review
   comment's `created_at` (visible in the `gh pr view --comments` output from
   step 2a) is the cutoff. Commits older than that were already reviewed; commits
   newer than that are the delta to inspect. Avoid re-examining already-reviewed
   code — focus the checklist on what changed.

4. **Run the full fresh-eyes checklist** (Step 4 of the main flow) against the
   new commits. Carry forward any "Still open" or "Location changed" items from
   step 2e. Do **not** re-raise items already confirmed fixed in the resolution
   table.

5. **Post the review.** Open the review body with the Prior-review resolution
   table (step 2e), then present any new findings and any carried-forward open
   items. If all prior items are fixed and no new issues appear, the verdict is
   approve. Follow the same body format and label steps as the main flow
   (steps 5, 5b, 6).

## Escalation footer

Every review body ends with one of the following escalation hints so the
calling session knows what to do next:

- **Sonnet first-pass, approve** → `Escalation: none. Safe for merge.`
- **Sonnet first-pass, approve-with-Opus-recheck** →
  `Escalation: please Opus-recheck before merge (touches: <module(s)>).`
- **Sonnet first-pass, needs-fix/blocker** →
  `Escalation: author-agent to address, then re-request review.`
- **Opus review** → no escalation line needed; the Opus verdict stands.
