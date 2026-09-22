# Campaign protocol — canonical flow

A **campaign** is the per-objective lane
[`docs/design/objectives/README.md`](../design/objectives/README.md)
§"What this tier changes" names as the natural next step: one persistent
driver session works one signed objective end to end, from a loose
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
| Who works it | many transient workers, fresh context per iteration | a persistent driver plus explicitly scoped contributors, interactive or dispatched |
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
| **resync-command** | The re-entry oracle a startup runs before anything else: branch verdict, open and merged campaign PRs, other lanes' changes to this campaign's files. |
| **launcher** | How a campaign pane starts or resumes: the fleet config that schedules it at every fleet start, and the by-hand command. |
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

## Shared participation

Interactive and dispatched contributors use the same campaign doc and canonical
worklists. Add `fleet:campaign-<slug>` to each participating PR; the status tool
recognizes it independently of provider, branch naming and session type. Legacy
`claude/<slug>-*` branches remain discoverable without the label. Membership is
not a claim, permission to amend another participant's branch, or merge approval;
the existing claim and review protocols still apply.

Before each slice, each contributor runs the **resync-command** with `--worktree`
pointing at its own dedicated worktree, reads the default branch's campaign doc
and open participant PRs, and reconciles them with local memory. An incomplete
history report is not evidence of no competing work. The command returns 1 for
that condition and refuses `--apply` with 2; retry the failed read first.

Record the participant, owned scope, branch/PR and acceptance gate under
`## Now`; preserve the other participants' entries. Update its ledger in the
same slice PR, including corrections and rejected experiments. Shared work is
not complete until the evidence lands; record proposed work separately from
validated work. At a shared-file boundary, read the competing diff and choose
an explicit dependency or a disjoint slice before editing. Campaign membership
does not suppress another contributor's overlap warning or make its merge an
acknowledgement by the driver. The status report retains the driver's existing
reconciliation window for that reason.

## Startup

Every campaign session begins here, and this is the only place the campaign
learns what the world did while it was gone — so none of it is skippable on
the grounds that the conversation "already knows".

0. Print the **role-banner**; `pwd` must be the **worktree-path**.
1. Run the **resync-command** and act on its verdict before anything else:
   - `clean` — the branch is level with the default branch; start the next
     slice here.
   - `behind` — no commits of its own, but the default branch has moved.
     `--apply` fast-forwards it; a slice started here builds on a stale base.
   - `live` — an open PR on this head; continue it.
   - `superseded` — the stack merged and the branch holds nothing the default
     branch lacks. Re-run with `--apply`, which parks the branch back on the
     default branch and carries the uncommitted slice across. Never rebase
     instead: squash merges give every commit a fresh patch-id, so a rebase
     replays work that already landed.
   - `stranded` — merging the branch into the default branch would change it,
     so it carries content that never landed. Stop and ask the human; nothing
     is reset. `unknown` (the merge could not be evaluated) is the same stop.
2. Read the objective and the campaign doc, `## Now` last. Where the resync
   named another lane on this campaign's files, read that change before
   planning a slice that touches them — an oracle, fixture or shader the
   campaign authored may have moved under it.
3. Print `campaign <slug>: resuming <direction> / <slice>` and, in `live`
   mode, begin the loop. `dry-run` stops here and reports the next slice.

The fleet state cache is optional for this role: a stale or missing
`~/.fleet/state/state.json` is not an exit condition.

### Re-entry

A campaign pane resumes **within** a fleet session and never **across** one. A
crash or usage-limit exit mid-slice resumes the conversation with the slice
intact; a `fleet-down` → `fleet-up` starts a fresh session that re-runs the
startup above, retiring the previous sidecar to
`<role>.session-id.prev-<stamp>` so its transcript stays readable by uuid. A
babysit restarted by hand mid-fleet reads the same way and also starts fresh.

The split is the point. A resume carries no prompt, so a resumed campaign
re-reads nothing; across a restart it would work from a picture of the world
that predates the merge batch, the superseded branch, and whatever other lanes
landed on its files. Only the fresh path runs the startup that corrects it.

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
6. **Resume after merges.** Re-run the **resync-command** and follow its
   verdict as in startup step 1; `start-next-task` refuses on tracked
   modifications, which is exactly the state a merged stack leaves behind.

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

Campaign PRs carry the actual `fleet:author-*` runtime from `commit-and-push` and
`fleet:wip` until a checkpoint, so reviewer, feedback, conflict and merger
projections skip them; no `fleet-claim` locks are involved. The campaign
runs beside the fleet or alone: **launcher** starts or resumes the pane
with the same session-sidecar mechanism as the architect panes, so
`/clear` or a crash resumes the conversation and a fleet restart starts
fresh (§Re-entry). Listed in the
fleet config, the pane comes up with every fleet start, in its own tmux
window, and never receives dispatched work. Context loss is survivable by
design: the campaign doc plus the open PR list is the state.

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
