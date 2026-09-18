# Campaign protocol — canonical flow

A **campaign** is the per-objective lane
[`docs/design/objectives/README.md`](../design/objectives/README.md)
§"What this tier changes" names as the natural next step: one persistent
fable-class session works one signed objective end to end, from a loose
plan, stacking PRs continuously. Approval authority does not move — every
PR is still human-merged — but the human approves at merge checkpoints
instead of per ticket.

Each repo's `.claude/commands/role-campaign.md` is a thin wrapper: harness
frontmatter, a pointer here, a `## Deltas` table
([`docs/design/role-sharing.md`](../design/role-sharing.md)).

## Campaign versus epic

| | Epic | Campaign |
|---|---|---|
| Plan | fixed child list with per-child `## Plan` comments | goal + spirit + ordered directions; slices are chosen as evidence develops |
| Who works it | many transient workers, fresh context per iteration | one persistent session that keeps its context and resumes across restarts |
| Memory | umbrella checklist and steward ledger on GitHub | the campaign doc in the tree, updated in every slice PR |
| Review | fleet reviewers per PR | the campaign runs reviewer subagents at each checkpoint, then the fleet's smoke lane after merge |
| Branching | one PR per issue, molecule stacks | cursor stacks, one PR per slice, stacked while the slices depend on each other |

## Repo deltas this flow needs

| Delta key | Meaning |
|---|---|
| **repo-slug** | The GitHub repo (`owner/name`) for `gh` calls. |
| **repo-root** | Absolute path of the primary clone. |
| **worktree-path** | The campaign's dedicated worktree, `<clone>/.claude/worktrees/campaign-<slug>`. |
| **role-name** | The role's name for banners and feedback (`campaign`). |
| **role-banner** | The one-line banner printed at startup. |
| **build-presets** | The host → preset map this role builds with. |
| **branch-prefix** | The head-branch prefix (`claude/<slug>-<topic>`). |
| **campaigns-dir** | Where campaign worklist docs live (`docs/design/campaigns/`). |
| **objectives-dir** | Where the objective files live. |
| **launcher** | The command that starts or resumes a campaign pane. |
| **feedback-file** | This role's end-of-iteration feedback file under `~/.fleet/feedback/`. |

## Shared rules

[`CLAUDE-BASELINE.md § Bash tool rules`](CLAUDE-BASELINE.md#bash-tool-rules) ·
[`FLEET.md § Resource coordination`](FLEET.md#resource-coordination) ·
[`FLEET.md § Stacked PRs`](FLEET.md#stacked-prs) ·
[`AUTHOR-PIPELINE.md`](AUTHOR-PIPELINE.md).

## The two documents

- **The objective** (`<objectives-dir>/<slug>.md`) is the human's "what":
  Outcome, Done means, Non-goals. The campaign never edits Done means or
  Non-goals; an amendment is its own docs PR the human merges.
- **The campaign doc** (`<campaigns-dir>/<slug>.md`) is the loose plan and
  the durable memory: `## Spirit` (guardrails), `## Directions` (ordered
  ladders, each pointing at the canonical worklist it draws from),
  `## Working agreement`, `## Ledger` (checkpoints, decisions, follow-ups
  filed) and `## Now` (the slice in flight and the next one). Every slice
  PR updates `## Now` and `## Ledger`; a resumed session can rebuild its
  state from this file and the open PR list alone.

## Out of scope

- Never commit to `master`, never merge, never `--force` or `--no-verify`.
- Never `fleet-claim` queue tasks, edit other issues' bodies or labels, or
  touch `human:*` labels.
- Never rewrite the history of a PR that carries `fleet:approved`; a later
  change is a new commit on that PR's own branch.

## Startup (fresh or resumed)

0. Print the **role-banner**; `pwd` must be the **worktree-path**.
1. `git -C <repo-root> fetch origin --quiet`; `git fetch origin --quiet`.
   A `claude/<slug>-*` branch with an open PR → continue it. Otherwise
   `start-next-task` fresh off `origin/master`. A dirty tree is never
   discarded: commit it to the current slice branch first.
2. Read the objective and the campaign doc, `## Now` last. List the open
   campaign PRs:
   `gh pr list --repo <repo-slug> --state open --search "head:<branch-prefix>"`.
3. Print `campaign <slug>: resuming <direction> / <slice>` and, in `live`
   mode, begin the loop. `dry-run` stops here and reports the next slice.

The fleet state cache is optional for this role: a stale or missing
`~/.fleet/state/state.json` is not an exit condition.

## Loop

1. **Pick the slice.** The first direction with an unfinished item whose
   inputs exist. A slice is one PR: one measurable question answered, one
   contract change, or one visual gate closed. Prefer measurement before
   mechanism, and a control that isolates one variable over a bundle.
2. **Work it.** Read every `CLAUDE.md` on the path. Build with the
   **build-presets** preset; run the fixture the campaign doc names.
   Evidence per [`AUTHOR-PIPELINE.md`](AUTHOR-PIPELINE.md): render changes
   carry `attach-screenshots` output and the campaign's pixel-identity
   control; perf changes carry `scripts/perf/repeat_profile.py` tables with
   fingerprints and full-frame GPU accounting; a GLSL change lands with its
   `.metal` twin in the same PR.
3. **Update the campaign doc** in the same slice: `## Now`, a `## Ledger`
   row, any `## Decisions taken` and discovered follow-ups.
4. **`commit-and-push`.** Cursor-stack mode when the slice depends on an
   open slice (`start-next-task` recorded the base); off `origin/master`
   when it is independent. The campaign decides; it never asks. After the
   PR opens: `gh pr edit <N> --repo <repo-slug> --add-label fleet:wip`
   (reviewers and the merger stand off until the checkpoint), then
   `start-next-task` with the stacking cue when the next slice depends on
   this one.
5. **Checkpoint** after four to six open PRs, before a slice that changes
   a shared surface, when the human asks, or when a direction completes:
   a. Spawn a fresh-context reviewer per PR (`review-pr`, plus the
      `review-invariant-render` / `review-invariant-ecs` subagents for
      render and ECS diffs) and address every finding on that PR's own
      branch; `gh stack sync` the children after a base moves.
   b. Confirm `gh pr view <N> --json mergeable` for every PR in the stack;
      resolve conflicts bottom-up.
   c. Remove `fleet:wip` and add `fleet:approved` on each PR, and record in
      the PR body under `## Campaign review` which reviewer ran and what it
      found. The campaign is the reviewer of record for its own stack; the
      post-merge smoke lane still runs.
   d. Add a `## Ledger` checkpoint row and tell the human the merge order
      (bottom-up). `fleet-decisions` lists the approved PRs as the merge
      queue.
   e. Keep working. The next slice stacks on the top approved PR when it
      depends on it; GitHub retargets the stack as the human merges.
6. **Resume after merges.** `git fetch`; if the whole stack merged,
   `start-next-task` fresh; otherwise continue on the top open PR.

## Follow-ups, decisions, escalation

- Discoveries outside the current direction, or needing a human decision,
  are filed unlabeled per [`TASK-FILING.md`](TASK-FILING.md) with
  `**Objective:** <slug>`, and listed in the campaign doc's ledger. A
  verified defect-shaped follow-up may take the agent-approved lane.
- A design fork inside the objective's boundary is decided in the tree:
  write the decision and the rejected alternatives under `## Ledger`,
  choose the option that best keeps `## Spirit`, and flag it in the PR
  body under `## Decisions taken`. Never block on it.
- A fork that changes the objective's Done means or Non-goals becomes a
  docs PR proposing the amendment; the campaign continues on other
  directions until the human merges or declines it.
- Feedback on a campaign PR (`human:needs-fix`, `fleet:needs-fix`) is the
  campaign's to address, on that PR's branch, per
  [`FLEET-FEEDBACK-HANDLING.md`](FLEET-FEEDBACK-HANDLING.md); clear it
  before starting the next slice.

## Interaction with the fleet

Campaign PRs carry `fleet:author-claude` from `commit-and-push` and
`fleet:wip` until a checkpoint, so reviewer, feedback, conflict and merger
projections skip them; no `fleet-claim` locks are involved. The campaign
runs beside the fleet or alone: **launcher** starts or resumes the pane
with the same session-sidecar mechanism as the architect panes, so
`/clear`, a crash or a restart resumes the conversation. Context loss is
survivable by design: the campaign doc plus the open PR list is the state.

## Modes

`live` — startup then the loop, until the human stops it. `dry-run` —
startup only; print the next slice and stand by.

## End-of-iteration feedback

[`FLEET-RUNTIME.md § End-of-iteration feedback`](FLEET-RUNTIME.md#end-of-iteration-feedback);
the file is the **feedback-file** delta. Write an entry at every
checkpoint.

## Hard rules

[`CLAUDE-BASELINE.md §"Hard rules for autonomous fleet roles"`](CLAUDE-BASELINE.md#hard-rules-for-autonomous-fleet-roles),
plus: after opening a PR, `start-next-task` before responding further; a
usage-limit error is printed and waited out, never retried blindly.
