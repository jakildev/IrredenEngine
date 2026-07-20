# review-pr — shared flow

The canonical `review-pr` flow: review an open PR and post a structured
review covering ownership, project invariants, allocation hot paths, naming,
tests, and project-specific smells, then set the verdict label.

Every repo that runs a fleet keeps its
`.claude/skills/review-pr/SKILL.md` as a thin wrapper that points here and
supplies only its **deltas** — most importantly its own **review
checklist**, which is inherently repo-specific (the engine checks ECS/render
invariants; a game checks its simulation model; an editor checks UX). The
*flow* is single-sourced here so the wrappers can't drift on mechanics. See
[`docs/design/skill-sharing.md`](../../design/skill-sharing.md).

Wherever a step needs a repo-specific value it names a **delta key** in bold.

---

## Repo deltas this flow needs

| Delta key | What it is | Engine value |
|---|---|---|
| **repo** | The `gh --repo` slug / `gh api repos/<repo>` path. | `jakildev/IrredenEngine` |
| **claim tool** | The fleet claim/release helper. | `fleet-claim` |
| **default branch** | The repo's main branch (step-1c base check). | `master` |
| **review checklist** | The repo's domain-specific review items (step 4). | the engine checklist in the wrapper |
| **smoke procedure** | Cross-host/backend validation tagging, if any. | the wrapper's `procedures/cross-host-smoke.md` |
| **re-review procedure** | The repo's re-review expansion. | the wrapper's `procedures/re-review.md` |
| **stacked-review procedure** | Per-PR scoping for stacked PRs. | the wrapper's `procedures/stacked-pr-review.md` |
| **fleet doc** | The repo's fleet reference (model split, reviewer loop). | [`docs/agents/FLEET.md`](../FLEET.md) |

The fleet verdict-label set (`fleet:approved`, `fleet:needs-fix`,
`fleet:blocker`, `fleet:has-nits`) and the bail-label set are shared fleet
machinery and are used concretely below; a repo that renames them notes the
mapping in its wrapper.

---

## When to run

Trigger from a persistent reviewer-loop session whose launch prompt told it
to poll `gh pr list` and review anything new; in that mode the reviewer
resolves the unreviewed set itself and invokes this once per PR. Or when the
user explicitly asks ("review PR <N>", "review the last PR", etc.).

Do **not** invoke proactively inside an unrelated working session (e.g.
mid-refactor on your own PR). The bar: either the user asked, or this
session's whole job is to be a reviewer.

## Model expectations (two-tier review)

Runs on either Sonnet or Opus:

- **Sonnet first pass** — cheap, catches the 80% (style, obvious bugs,
  missing null checks, naming, untested branches). A Sonnet reviewer is
  thorough on the checklist but explicitly flags anything subtle (lifetime,
  concurrency, an invariant several layers deep, a GPU/CPU handoff) with
  "I am not confident on this invariant, please Opus-review:".
- **Opus second pass (or sole pass)** — for any PR touching core invariants,
  anything performance-sensitive, any GPU/CPU sync, any concurrency, or any
  PR the Sonnet pass escalated. Opus also confirms earlier Sonnet nits were
  addressed.

When running as Sonnet, mention in the verdict whether Opus escalation is
needed. When running as Opus on a PR with a prior Sonnet review, read it
first and focus on what Sonnet couldn't confirm. See the **fleet doc**
"Model split".

## Preconditions

1. `gh` authenticated (`gh auth status`).
2. A PR number/URL or "latest" — resolve before starting.
3. You are **not** the agent that wrote the code. If the author asks to
   review their own work, warn them and offer to run it anyway with the
   tunnel-vision caveat.

---

## Flow

### 1. Resolve the PR and pull its metadata

Read author/human PR comments **before** walking the diff — they flag
deliberate scope ("I deferred X") and pre-flag concerns ("watch out for Y on
macOS"). Missing them produces reviews that re-raise addressed points.

```bash
gh pr view <N> --json number,title,body,headRefName,baseRefName,author,files,additions,deletions,commits,mergeable,comments,reviews,labels
gh api repos/<repo>/pulls/<N>/comments
```

The first returns issue-level comments, review summaries, and the live label
set (for the step-1b bail check). The second returns inline (line-attached)
code-review comments that `gh pr view --json` omits. The diff is fetched
**after** the bail check so bail-label PRs never pay the diff round-trip.

For "latest"/"most recent": `gh pr list --state open --limit 5`, pick the
top, confirm with the user if ambiguous.

### 1b. Label bail check + diff fetch

**Before** fetching the diff, check the step-1 `labels` for bail labels:
`fleet:semantic-conflict`, `fleet:merger-cooldown`, `fleet:wip`, `human:wip`,
any `fleet:amending-*`. If any is present, release the claim and skip the PR
without fetching the diff or posting:

```bash
<claim-tool> review-release <N> <your-worktree-name>
```

If none present, fetch the diff: `gh pr diff <N>`.

### 1c. Check whether the PR is stacked

Some PRs are stacked: their `--base` is another open PR's branch, not the
default branch. The review is still per-PR; you don't re-review the parent.

Quick detection from step-1 metadata: `baseRefName != <default-branch>` →
stacked on that branch; a `Stacked on:` body line confirms it and gives the
parent PR URL. If stacked, apply the **stacked-review procedure**, then
return for step 1d. If standalone, continue to step 1d.

### 1d. Churn audit when `mergeable == CONFLICTING`

A CONFLICTING PR has a stale branch that can silently carry reverted hunks.
When `mergeable` is `CONFLICTING`, run `gh pr diff <N> --stat` and apply:

1. **Oversized churn** — any file with ≥100 added+deleted lines the body
   doesn't mention. Flag **Needs-fix** (escalate to **Blocker** if it
   deletes functions/files that break the build): the PR may be silently
   reverting work that landed after the branch was cut.
2. **Out-of-scope file** — any file in the stat that's neither described in
   the body nor a known mechanical side-effect of the claimed scope. Flag
   **Needs-fix**: author must acknowledge it or rebase to drop the hunk.

If neither fires, note in the body: "CONFLICTING state checked — no
out-of-scope files or oversized churn." If `mergeable` is anything else,
skip this step.

### 2. Check out the PR branch locally (read-only)

```bash
gh pr checkout <N>
```

You still have full read access to the rest of the repo for cross-reference.
Do **not** commit or push from this worktree — you are a reader.

### 3. Read the diff in context

For each changed file: read the **full file**, not just the hunks (bugs hide
in surrounding code); cross-reference any component/system/symbol name
against existing conventions; if a shader changed, also read the CPU-side
struct that feeds it (GPU layouts and CPU structs must stay in sync).

Keep a running list ranked by severity. The boundary between **blocker** and
**needs-fix** is "would the default branch survive this merge?":

- **Blocker** — the build breaks, the app crashes/hangs, or data on disk is
  corrupted if this lands. Unmergeable as-is.
- **Needs-fix** — compiles and runs, but introduces a correctness/perf
  regression that must be repaired before merge. Survives, but worse.
- **Nit** — style, naming, minor simplification, docs. Truly optional.
- **Praise** — a non-obvious good decision worth reinforcing.

Two cross-cutting rules for the findings themselves:

- **Verify cited precedents/APIs before asserting them as the fix
  pattern.** A finding of the shape "X does this via Y; align with it"
  requires checking the tree first — grep for the named API, read the
  cited precedent. A miscited precedent is worse than a vague nit: an
  author who trusts it implements against a phantom API or propagates the
  misattribution into comments. If unverified, phrase the nit as a
  question, not an assertion.
- **Cross-check "remove when #X" annotations against the PR's `Closes`
  list.** Grep the diff for `remove when #`, `TODO`, `FIXME` markers
  referencing an issue this PR's body closes — those blocks must be gone
  before merge, or they ship to the default branch as dead code. (The
  author side has the same check at PR-open time; this is the backstop.)

### 4. Apply the repo's review checklist

Go through the **review checklist** (in the wrapper) explicitly. For every
item, either confirm compliance or raise an issue. This is the repo-specific
heart of the review — the engine's checklist covers ECS invariants,
ownership/lifetime, the render pipeline, lighting, math/coordinates,
serialization, naming/style, tests/build, and Opus-only deep items; a
creation layered on the engine adds (or replaces with) its own domain rules.

If the PR touches a subdirectory with its own `CLAUDE.md` (or `REVIEW.md`),
read it **in addition to** the repo checklist and apply both — the repo
checklist is the baseline, the subdirectory's rules are the delta.

### 5. Write the review and set the verdict label

**These are one indivisible action.** Post the review comment and set the
verdict label in immediate succession — no intervening bash calls. A review
without a verdict label is invisible to the human's merge queue.

Post via `gh pr review`. **Do NOT use `--body "$(cat <<'EOF'…)"` or any
`$(...)` command substitution** — it trips the security gate on backticks.
Instead: `rm -f .review-body.md` (so the Write tool doesn't refuse), Write
the body to `.review-body.md` in the worktree root (gitignored; NOT `/tmp/`),
then:

```bash
gh pr review <N> --comment --body-file .review-body.md
```

Body shape:

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

🤖 Reviewed by Claude <model> (review-pr skill)
```

Rules: cite **file:line** for every issue; suggest a concrete fix, not just
"this is wrong"; drop empty sections (don't write "None").

**The bright line between Nits and needs-fix:** a Nit is *truly optional*.
Anything you describe with "must resolve before merge", "pre-merge ask",
"the comment and code must agree", or "needs to be reconciled" is **NOT a
Nit** — it's needs-fix; move it and drop the verdict to `needs-fix`. The
contradiction "approve, but please fix X before merge" is forbidden. Nits
are still encouraged — author roles scan approved PRs and address every nit
before landing, so put real nits in freely.

**Do not use `gh pr review --approve` or `--request-changes`** — all fleet
agents share one account and the API rejects formal review actions on your
own PRs. The `--comment` review plus the verdict line is sufficient; merging
is always the user's call.

### 5b. Set the verdict label (continued)

**Immediately after** the `gh pr review` call — your very next bash call —
run the verdict-label swap. A PR has exactly one verdict label
(`fleet:approved` / `fleet:needs-fix` / `fleet:blocker`) at a time;
`fleet:has-nits` is orthogonal and rides on top of `fleet:approved`. Always
remove stale verdict labels before adding the new one, and clear the
stacked-PR gates (`fleet:awaiting-upstream-review`, `fleet:stacked-rebase`,
`fleet:needs-base-update`) in the same swap. The verdict label is the
**primary signal** the human uses to decide what to merge.

### 5c. Tag for cross-host/backend smoke validation

If the diff touches the paths the **smoke procedure** covers, apply it after
the verdict label (it subtracts the author's host so only the other
backend(s) get tagged). Otherwise skip to step 6.

### 6. Report back

Reply to the calling session with: PR number + title + verdict; count of
blockers/needs-fix/nits; a link to the review comment; one sentence on what
the author should do next.

---

## Anti-patterns

- Approving work that violates an invariant "because the test passes" — some
  invariants don't fail at test time.
- Pushing commits to the PR branch during a review iteration.

## Re-review

When the user says "re-review PR <N>" or a reviewer-loop session sees
`fleet:changes-made` on a previously-flagged PR, the flow differs — you
verify previously-flagged items against the new commits BEFORE running the
checklist, else you re-flag already-fixed items. See the **re-review
procedure**. (First review of a PR → use the standard flow above.)

## Escalation footer

Every review body ends with one escalation hint:

- Sonnet first-pass, approve → `Escalation: none. Safe for merge.`
- Sonnet first-pass, approve-with-Opus-recheck →
  `Escalation: please Opus-recheck before merge (touches: <module(s)>).`
- Sonnet first-pass, needs-fix/blocker →
  `Escalation: author-agent to address, then re-request review.`
- Opus review → no escalation line; the Opus verdict stands.
