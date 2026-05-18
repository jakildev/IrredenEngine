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
[`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md) "Model split" for the full split.

## Preconditions

1. `gh` authenticated (`gh auth status`).
2. User supplied a PR number, PR URL, or said "latest" — resolve before starting.
3. You are **not** the same agent that wrote the code. If the user is asking
   the author agent to review its own work, warn them and offer to run the
   review anyway with the caveat that author-agents have tunnel vision.

## Flow

### 1. Resolve the PR and pull its metadata

Read author/human PR comments before walking the diff — they're how
authors flag deliberate scope ("I deferred X to a follow-up") and how
humans pre-flag concerns ("watch out for Y on macOS"). Missing them
leads to reviews that re-raise points the author already addressed in
conversation.

```bash
gh pr view <N> --json number,title,body,headRefName,baseRefName,author,files,additions,deletions,commits,mergeable,comments,reviews
gh api repos/jakildev/IrredenEngine/pulls/<N>/comments
gh pr diff <N>
```

The first command returns issue-level comments (`comments`) and review
summaries (`reviews`). The second returns inline code-review comments
(line-attached) — `gh pr view --json` does not include these. Together
the two commands cover every kind of PR conversation.

If the user said "latest" / "most recent":

```bash
gh pr list --state open --limit 5
```

Pick the top result, confirm the title with the user if there's ambiguity.

### 1b. Check whether the PR is stacked

Every fleet PR today is single-task. Some are stacked: their `--base` is another open PR's branch rather than `master`. The review pass is still per-PR; you don't re-review the parent.

**Quick detection** (from the metadata fetched in step 1):
- `baseRefName != "master"` → stacked on that branch.
- Body contains `Stacked on:` line → confirms it; gives the parent PR URL.

If stacked: see [`procedures/stacked-pr-review.md`](procedures/stacked-pr-review.md) for the per-PR scoping rules, the body callout format, and the upstream-fragility flag — then return here for step 1c.

If standalone (base `master`, no `Stacked on:`): continue with step 1c.

### 1c. Churn audit when `mergeable == CONFLICTING`

A PR in `CONFLICTING` state has a stale branch relative to `master`.
A stale branch can silently carry reverted hunks — content that landed
on `master` after the PR branch was cut but isn't reflected in the PR
body because the author never rebased. `gh pr diff --stat` makes the
actual scope visible before any code-path review.

**When `mergeable` is `CONFLICTING`**, run:

```bash
gh pr diff <N> --stat
```

Apply two checks to the stat output:

1. **Oversized churn** — any file with ≥100 added + deleted lines that
   the PR body does not explicitly mention. Flag as **Needs-fix** (escalate
   to **Blocker** if the churn includes deleted functions or files that
   would break the build): the PR may be silently reverting work that
   landed on master after the branch was cut. Classic pattern: a file
   with 200+ line deletions in a PR that never claimed to touch it
   (e.g. `system_compute_light_volume.hpp` appearing in a PR scoped to
   12 other system files).
2. **Out-of-scope file** — any file that appears in the stat output but
   is neither described in the PR body nor a known mechanical side-effect
   of the PR's claimed scope (e.g. a `CMakeLists.txt` accompanying a new
   source file — not `TASKS.md`, `.fleet/plans/` drift, or unrelated docs).
   Flag as **Needs-fix**: author must acknowledge the file in the PR
   body or rebase to drop the accidental hunk.

If neither check fires, note in the review body:
> "CONFLICTING state checked — `gh pr diff --stat` shows no out-of-scope
> files or oversized churn."

If `mergeable` is anything other than `CONFLICTING`, skip this step.

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

Keep a running mental list of issues, ranked by severity. The
boundary between **blocker** and **needs-fix** is the question
"would master survive this merge?" — if no, blocker; if yes-but-
worse, needs-fix.

- **Blocker** — master build breaks, the demo crashes/hangs, or data
  on disk is corrupted if this lands. The PR is unmergeable as-is —
  the human cannot ship it without a fix.
- **Needs-fix** — the PR could compile and run on master, but
  introduces a correctness or performance regression that must be
  repaired before merge. Master survives, but in a worse state than
  it should be.
- **Nit** — style, naming, minor simplification, docs. Truly
  optional; the PR may merge without addressing.
- **Praise** — non-obvious good decision worth calling out so the
  author-agent keeps doing it.

### 4. Apply the Irreden-Engine-specific review checklist

Go through each of these explicitly. For every item, either confirm
compliance or raise an issue.

**ECS invariants** — check against [`.claude/rules/cpp-ecs-smells.md`](../../rules/cpp-ecs-smells.md). For each item, confirm compliance or raise an issue.

**Ownership / lifetime**
- ❌ `shared_ptr` where `unique_ptr` would do.
- ❌ Raw owning pointers (raw pointer = non-owning, always).
- ❌ Storing references or pointers to ECS component storage across ticks —
  archetype changes invalidate addresses.
- ❌ Capturing `this` or references to World managers in lambdas that outlive
  the World (e.g. lua callbacks registered before World teardown).
- ❌ Stored `g_*Manager` pointer or reference in any object whose lifetime
  can outlive `World` (background threads, sol2 callback closures, long-lived
  caches) — see `engine/world/CLAUDE.md` and `engine/CLAUDE.md`.

**Render pipeline**
- ❌ CPU frame-data struct out of sync with its GLSL `layout(std140)`
  counterpart. `vec3` members pad to 16 bytes; array elements stride to 16
  bytes; members crossing a 16-byte boundary need `alignas(16)`. If either
  the C++ struct (in `engine/render/include/irreden/render/`) or its shader
  `uniform` block changed, cross-reference both sides.
- ❌ shader references `binding = N` but the C++ `kBufferIndex_*` constant
  was not updated — the mismatch is silent (wrong uniforms, no error).
  Confirm every bind-point index agrees on both sides.
- ❌ New shader file not following the `c_` / `v_` / `f_` / `g_` prefix.
- ❌ Canvas allocation before the canvas entity exists.
- ❌ Compute dispatch size doesn't match `voxelDispatchGridForCount()`.
- ❌ new `*.glsl` added without a matching `*.metal` counterpart (if parity
  is intentionally deferred, the PR body must acknowledge it and reference a
  follow-up task).

**Lighting** (if the diff touches `system_*ao*`, `system_*shadow*`,
`system_*flood*`, `system_*fog*`, `system_build_light_occlusion_grid*`,
or any `c_compute_*shadow*.glsl` / `.metal`)
- Check 1 (grid coverage): `system_build_light_occlusion_grid.hpp`
  iterates the full voxel pool — it does **not** include
  `cull_viewport_state.hpp` and does not call `visibleIsoViewport`.
  Off-screen geometry participates in lighting by design.
- Check 2 (shadow-ring extent): if chunk streaming is involved, the
  resident-chunk set extends past the view frustum by
  `maxCasterHeight × cot(sunAltitude)` in the sun-projection direction.
- Check 3 (light-seed expansion): the flood-fill seed gather does not filter
  by `visibleIsoViewport` without expanding by `C_LightSource::radius_`.
  Off-screen light sources within radius must still seed on-screen tiles.
- Check 4 (AO/shadow guard band): when chunk streaming is active, the
  resident chunk set includes a 1-chunk guard band in all six directions for
  correct AO neighbor-voxel sampling.

**Math / coordinates** (see [`.claude/rules/cpp-math.md`](../../rules/cpp-math.md) for the full glm→IRMath substitution table)
- ❌ Mixing 3D world coords with iso 2D coords without going through
  `IRMath::pos3DtoPos2DIso` or a named helper.
- ❌ Hard-coded `x + y + z` where `IRMath::pos3DtoDistance` exists.
- ❌ PlaneIso axis swapped.

**Serialization** (when the diff touches `engine/asset/`, `engine/prefabs/irreden/voxel/`, or `engine/world/`)
- ❌ A struct annotated `// IRAsset: serialized` gained, lost, or renamed a field without
  bumping `static constexpr uint16_t kSaveVersion = N;` in the same diff.
  Fix: increment `kSaveVersion` and add a reader migration keyed on
  `(structType, oldVersion)` in the format's load function. (Save Format Extensibility Rule #3.)
- ❌ A new serialized record type with no `// IRAsset: serialized` annotation — the
  simplify and review-pr checks cannot guard it without the annotation. Add the annotation
  and set `kSaveVersion = 1` on the struct.

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

**Opus-only items** (Sonnet should not attempt these — escalate via the
verdict footer if any of these surfaces are touched)

Sonnet's single-file diff scan can't reliably catch these — they live
multiple frames deep, span modules, or only manifest under specific
scheduling. If the PR touches any of these surfaces, append
`Opus recheck required: <one-line reason>` to the verdict footer.

- **GPU buffer lifetime across frames.** Does an SSBO/UBO get bound on
  frame N and read on frame N+1 without a fence or explicit double-
  buffer swap? Async readback (compute → CPU mapped pointer) is
  especially prone to use-after-free if the destination buffer is
  recycled before the readback completes.
- **Archetype-mutation race during structural-change deferral.** A
  system that calls `addComponent` / `removeComponent` /
  `removeEntity` mid-iteration must use the deferred variant. If it
  touches the live archetype while a parallel system is iterating,
  component addresses are invalidated silently. Confirm every
  structural call inside a tick path goes through the deferred queue.
- **Race between `flushStructuralChanges` and async GPU readback.**
  The readback's destination buffer (or the entity it indexes) may
  vanish if the structural flush runs first. Verify the readback's
  lifetime is decoupled from any entity that `flushStructuralChanges`
  could remove — either it indexes by stable ID, or it has its own
  refcount.
- **Allocator behavior in long-lived caches.** A cache that uses
  `std::unordered_map<EntityId, T>` with a poorly-chosen hash, or a
  `std::vector<T>` that's never shrunk, grows unbounded across
  frames. Confirm the cache has an eviction path (entity-removed
  hook, LRU cap, periodic compact) or a clear teardown order.
- **Hot-path register pressure.** A per-entity tick that touches >8
  components or carries >12 live local variables across a loop body
  is approaching the architecture's register file. The compiler may
  spill to memory; the cost is invisible until profiled. Flag any
  tick path that grew significantly in number of locals or component
  reads, especially in `engine/render/` or `engine/world/`.

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

### 5. Write the review and set the verdict label

**These are one indivisible action.** Post the review comment and set
the verdict label in immediate succession — no intervening bash calls,
no context switches. A review without a verdict label is invisible to
the human's merge queue (observed on PR #230 where both passes approved
but the label was never set — PR sat unlabeled for hours).

Post the review as a PR comment via `gh pr review`. **Do NOT use
`--body "$(cat <<'EOF'...)"` or any `$(...)` command substitution** —
it triggers Claude Code's `command_substitution` security gate and
causes parse errors when the body contains backticks or special
characters. Instead, write the body to a temp file with the **Write
tool**, then pass it with `--body-file`:

1. **First** run `rm -f .review-body.md` so the Write tool doesn't
   refuse with "File has not been read yet" — that error fires when
   an existing file at the path wasn't Read in this session, which
   is the normal case when a previous review iteration left the
   body file behind. Then use the **Write tool** to write the review
   body to `.review-body.md` in the worktree root (NOT `/tmp/` —
   the sandbox may block writes outside the project tree). This
   file is gitignored:

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

🤖 Reviewed by Claude <model> (review-pr skill)
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
- Verdict options (severity definitions live in step 3 — apply
  "would master survive this merge?" as the deciding question):
  - **approve** — no blockers, no needs-fix. Nits only, if any.
  - **needs-fix** — one or more needs-fix items. No blockers.
    Master would survive the merge but in a worse state.
  - **blocker** — one or more blockers. Master breaks, the demo
    crashes/hangs, or data on disk is corrupted if this lands.

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

### 5b. Set the verdict label (step 5 sequence, continued)

**Immediately after** `gh pr review --comment --body-file .review-body.md`,
run the verdict label command — your very next bash call. No intervening
calls. Always remove stale verdict labels before adding the new one — a PR
should have exactly one verdict label (`fleet:approved` / `fleet:needs-fix`
/ `fleet:blocker`) at any time. `fleet:has-nits` is orthogonal — it rides
on top of `fleet:approved`. The remove list also clears
`fleet:awaiting-upstream-review` (previously-gated stacked PR exits cleanly),
`fleet:stacked-rebase` (re-eval after a stacked-PR retarget completes
the label's intent), and `fleet:needs-base-update` (stacked PR whose
upstream has since been re-approved or merged).

```bash
# For approve, no nits in body:
gh pr edit <N> --remove-label "fleet:needs-fix" --remove-label "fleet:blocker" --remove-label "fleet:has-nits" --remove-label "fleet:awaiting-upstream-review" --remove-label "fleet:stacked-rebase" --remove-label "fleet:needs-base-update" --add-label "fleet:approved"

# For approve WITH a non-empty Nits section in the body:
gh pr edit <N> --remove-label "fleet:needs-fix" --remove-label "fleet:blocker" --remove-label "fleet:awaiting-upstream-review" --remove-label "fleet:stacked-rebase" --remove-label "fleet:needs-base-update" --add-label "fleet:approved" --add-label "fleet:has-nits"

# For needs-fix (nits roll into the fix work; no separate label):
gh pr edit <N> --remove-label "fleet:approved" --remove-label "fleet:blocker" --remove-label "fleet:has-nits" --remove-label "fleet:awaiting-upstream-review" --remove-label "fleet:stacked-rebase" --remove-label "fleet:needs-base-update" --add-label "fleet:needs-fix"

# For blocker:
gh pr edit <N> --remove-label "fleet:approved" --remove-label "fleet:needs-fix" --remove-label "fleet:has-nits" --remove-label "fleet:awaiting-upstream-review" --remove-label "fleet:stacked-rebase" --remove-label "fleet:needs-base-update" --add-label "fleet:blocker"
```

The verdict label is the **primary signal** the human uses to decide
what to merge. The comment body has the details. The `fleet:has-nits`
label tells the author "you've been approved, and there are nits to
clean up before this lands" — author roles poll for it and address
the nits without losing the approval.

### 5c. Tag render PRs for cross-host smoke validation

If the diff touches `engine/render/`, `engine/prefabs/irreden/render/`, `engine/render/src/shaders/`, any `*.glsl`, or any `*.metal` file, the PR needs cross-host smoke validation. The standard tagging path subtracts the author's host (only the *other* backend needs validation).

Render-touching PR? See [`procedures/cross-host-smoke.md`](procedures/cross-host-smoke.md) for the host-detection table, the `gh pr edit` calls per author host, and the skip conditions (game-repo PRs, non-render engine PRs).

Non-render PR? Skip to step 6.

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

When the user says "re-review PR 42" or a reviewer-loop session sees `fleet:changes-made` on a previously-flagged PR, the flow is different — you must verify previously-flagged items against the new commits BEFORE running the standard checklist, otherwise you risk re-flagging items that were already fixed.

See [`procedures/re-review.md`](procedures/re-review.md) for the full re-review procedure (Prior-review resolution table, post-cutoff commit scoping, re-apply guard).

## Escalation footer

Every review body ends with one of the following escalation hints so the
calling session knows what to do next:

- **Sonnet first-pass, approve** → `Escalation: none. Safe for merge.`
- **Sonnet first-pass, approve-with-Opus-recheck** →
  `Escalation: please Opus-recheck before merge (touches: <module(s)>).`
- **Sonnet first-pass, needs-fix/blocker** →
  `Escalation: author-agent to address, then re-request review.`
- **Opus review** → no escalation line needed; the Opus verdict stands.
