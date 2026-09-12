# review-pr — shared flow

Review an open PR, post a structured review, and set the verdict label.

Each repo's `.claude/skills/review-pr/SKILL.md` is a thin wrapper that
points here and answers the delta keys below — most importantly its own
**review checklist**, which is inherently repo-specific
([`docs/design/skill-sharing.md`](../../design/skill-sharing.md)). A step
that needs a repo-specific value names its **delta key** in bold.

---

## Repo deltas this flow needs

| Delta key | What it is | Engine value |
|---|---|---|
| **repo** | The `gh --repo` slug / `gh api repos/<repo>` path. | `jakildev/IrredenEngine` |
| **claim tool** | The fleet claim/release helper. | `fleet-claim` |
| **default branch** | The repo's main branch (step-1c base check). | `master` |
| **review checklist** | The repo's domain-specific review items (step 4). | the engine checklist in the wrapper |
| **acceptance grader** | Optional subagent that grades the PR against the originating issue's acceptance criteria (step 4b). | the `review-acceptance` agent |
| **smoke procedure** | Cross-host/backend validation tagging, if any. | the wrapper's `procedures/cross-host-smoke.md` |
| **re-review procedure** | The repo's re-review expansion. | the wrapper's `procedures/re-review.md` |
| **stacked-review procedure** | Per-PR scoping for stacked PRs. | the wrapper's `procedures/stacked-pr-review.md` |
| **fleet doc** | The repo's fleet reference (model split, reviewer loop). | [`docs/agents/FLEET.md`](../FLEET.md) |

The verdict labels (`fleet:approved`, `fleet:needs-fix`, `fleet:blocker`,
`fleet:has-nits`) and the bail labels are shared fleet machinery; a repo
that renames them notes the mapping in its wrapper.

---

## When to run

From a persistent reviewer-loop session polling `gh pr list`, once per
unreviewed PR; or on explicit ask ("review PR <N>", "review the last PR").
Never from inside an unrelated working session.

## Model expectations

- **Sonnet first pass** catches style, obvious bugs, naming, untested
  branches, and flags anything subtle (lifetime, concurrency, deep
  invariants, GPU/CPU handoff) with "I am not confident on this invariant,
  please Opus-review:". State in the verdict whether Opus escalation is
  needed.
- **Opus second or sole pass** for core invariants, performance, GPU/CPU
  sync, concurrency, or any Sonnet escalation. Read the prior Sonnet review
  first and focus on what it could not confirm. **fleet doc** "Model split".

## Preconditions

`gh` authenticated; a PR number, URL, or "latest" resolved; you did not
write the code (if the author asks anyway, warn about tunnel vision and
proceed).

---

## Flow

### 1. Resolve the PR and pull its metadata

Read author and human comments before the diff — they mark deliberate
scope and pre-flagged concerns.

```bash
gh pr view <N> --json number,title,body,headRefName,baseRefName,author,files,additions,deletions,commits,mergeable,comments,reviews,labels
gh api repos/<repo>/pulls/<N>/comments
```

The second call returns inline comments that `gh pr view --json` omits.
"Latest": `gh pr list --state open --limit 5`, confirm if ambiguous.

### 1b. Bail check, then diff

If the labels include `fleet:semantic-conflict`, `fleet:merger-cooldown`,
`fleet:wip`, `human:wip`, or any `fleet:amending-*`: release and skip
without fetching the diff or posting.

```bash
<claim-tool> review-release <N> <your-worktree-name>
```

Otherwise `gh pr diff <N>`.

### 1c. Stacked?

`baseRefName != <default-branch>` means stacked on that branch (a legacy
`Stacked on:` body line is confirmation only, never the signal). Apply the
**stacked-review procedure**, then continue.

### 1d. Churn audit when `mergeable == CONFLICTING`

From the per-file `additions`/`deletions`:

- A file with ≥100 added+deleted lines the body does not mention →
  **Needs-fix** (Blocker if it deletes functions/files that break the
  build): the branch may be silently reverting work that landed after it
  was cut.
- A file neither described in the body nor a mechanical side-effect of the
  claimed scope → **Needs-fix**: acknowledge or rebase it away.

If neither fires, note "CONFLICTING state checked — no out-of-scope files
or oversized churn."

### 2. Check out the branch (read-only)

`gh pr checkout <N>`, then compare `git rev-parse HEAD` with `headRefOid`
from `gh pr view <N> --json headRefOid`. A failed checkout leaves the old
tree in place — report the blocker through the completion contract rather
than testing that tree. Never commit or push from a review.

### 3. Read the diff in context

Read each changed file in full, not just the hunks. Cross-reference new
symbols against existing conventions; a changed shader means also reading
the CPU-side struct that feeds it.

Rank findings. The blocker / needs-fix line is "does the default branch
survive this merge?":

- **Blocker** — build breaks, crash/hang, or on-disk data corruption.
- **Needs-fix** — compiles and runs, but a correctness or perf regression
  that must be repaired before merge.
- **Nit** — style, naming, minor simplification, docs. Truly optional.
- **Praise** — a non-obvious good decision.

Two rules for the findings themselves:

- A finding of the shape "X does this via Y; align with it" requires
  having read Y in the tree; otherwise phrase it as a question. Asserting
  that something does *not* exist (no tracking issue, no label, no
  precedent) requires having run the enumerating command (`gh issue list
  --search` in both fleet repos, `gh label list`, a grep) and saying what
  was searched.
- Grep the diff for `remove when #`, `TODO`, `FIXME` markers referencing an
  issue this PR's body closes — those blocks must be gone before merge.

### 4. Apply the review checklist

Walk the **review checklist** explicitly; confirm or raise each item. If
the PR touches a subdirectory with its own `CLAUDE.md` or `REVIEW.md`,
apply that too — the repo checklist is the baseline, the subdirectory's
rules are the delta.

### 4b. Grade acceptance against the originating issue

When the repo defines an **acceptance grader** and the body carries `Closes
#N`, dispatch it (Agent tool) with the PR and issue numbers — concurrently
with step 4; it needs step 1's metadata, not your findings. It grades each
planned criterion met / unmet / unverifiable from the PR's `## Acceptance
evidence`. Fold the fragment in as `### Acceptance (issue #N)` and mirror
every unmet criterion into the top-level `### Needs-fix` (or `###
Blockers`) list — blocking items never live only inside the fragment. Skip
when there is no grader, no `Closes #N`, or no planned criteria.

### 5. Post the review

Post and label as one indivisible action: the review comment, then the
verdict label as the very next bash call. `rm -f .review-body.md`, Write
the body to `.review-body.md` in the worktree root (gitignored; not
`/tmp/`), then:

```bash
gh pr review <N> --comment --body-file .review-body.md
```

Never `--body "$(cat <<'EOF'…)"` or any `$(...)` (the security gate trips
on backticks). Never `--approve` / `--request-changes` — every fleet agent
shares one account and the API rejects formal reviews on its own PRs;
merging is the human's call.

```markdown
## Review — <title>

**Verdict:** <approve | needs-fix | blocker>

### Blockers
- <path:line> — <issue> — <suggested fix>

### Needs-fix
- <path:line> — <issue> — <suggested fix>

### Nits
- <path:line> — <nit>

### Nits (follow-up)
- <path:line> — <wording-tier nit; no amend, no `fleet:has-nits`>

### Praise
- <non-obvious good decision, if any>

### Acceptance (issue #N)
<the acceptance grader's table, when step 4b ran>

### Test plan the author should run before merge
- [ ] <...>

🤖 Reviewed by Claude <model> (review-pr skill)
```

Cite `file:line` and a concrete fix for every issue; drop empty sections.

**The bright line:** a Nit is truly optional. Anything phrased "must
resolve before merge", "the comment and code must agree", "needs to be
reconciled" is needs-fix — move it and drop the verdict. "Approve, but fix
X before merge" is forbidden. Real nits are welcome: author roles address
every nit on approved PRs before landing. An unmet acceptance criterion is
blocking by definition.

### 5b. Set the verdict label

Immediately after `gh pr review`, apply the named edge with
`fleet-review-verdict` (the wrapper lists the commands). Fleet reviewers
pass `--agent <worktree-basename>`; an interactive human omits it. The tool
verifies the reviewing claim and a review pinned to the current head, then
delegates the swap to `fleet-transition`. A PR carries exactly one of
`fleet:approved` / `fleet:needs-fix` / `fleet:blocker`; `fleet:has-nits`
rides on top of `fleet:approved` and covers only `### Nits` (amend-worthy)
— `### Nits (follow-up)` sets no label and rides the author's next PR
(REVIEWER-PROTOCOL.md §"Nits vs needs-fix"). Stale verdict labels are
removed and the stacked-PR gate `fleet:awaiting-upstream-review` is cleared
in the same swap.

### 5c. Cross-host smoke

If the diff touches paths the **smoke procedure** covers, apply it after
the verdict label (it subtracts the author's host).

### 6. Report

PR number, title, verdict; counts of blockers / needs-fix / nits; link to
the review comment; one sentence on the author's next step.

---

## Re-review

"re-review PR <N>", or `fleet:changes-made` on a PR this loop previously
flagged: verify the previously-flagged items against the new commits
*before* running the checklist — the **re-review procedure**.

## Escalation footer

Every review body ends with one line:

- Sonnet, approve → `Escalation: none. Safe for merge.`
- Sonnet, approve-with-Opus-recheck → `Escalation: please Opus-recheck
  before merge (touches: <module(s)>).`
- Sonnet, needs-fix/blocker → `Escalation: author-agent to address, then
  re-request review.`
- Opus → no escalation line; the Opus verdict stands.
