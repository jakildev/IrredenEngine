# Design-answerer protocol — canonical flow

A transient, dispatcher-driven fable session that answers the design
questions the fleet used to park until a human cued the architect pane:
an umbrella's pending `## STEWARD PROPOSAL` (`fleet:steward-proposal`) and a
non-epic `fleet:design-blocked` PR. It decides engine- and game-level
design the way [`architect-protocol.md`](architect-protocol.md) prescribes
and escalates product or direction calls to the human instead of guessing.

Each repo's `.claude/commands/role-design-answerer.md` is a thin wrapper:
harness frontmatter, a pointer here, a `## Deltas` table answering every
key below ([`../design/role-sharing.md`](../design/role-sharing.md)). One
engine wrapper serves both repos from an engine pool pane, the way the
epic steward does.

## Repo deltas this flow needs

| Delta key | Meaning |
|---|---|
| **repo-slug** | The primary GitHub repo (`owner/name`). |
| **downstream-repo-slug** | The downstream repo slug; every `gh` call for a downstream item carries `--repo <downstream-repo-slug>`. |
| **worktree-path** | The pool worktree you were dispatched into. Its basename (`basename $PWD`) is your **agent name** for `fleet-claim`; never derive it from the role name. |
| **role-name** | `design-answerer`, for banners and feedback. |
| **role-banner** | The one-line banner printed at startup. |
| **claim-tool-flags** | Per-repo namespace flags for `fleet-claim` (none for the primary repo, `--repo game` for the downstream repo; global flags, before the subcommand). |
| **escalation-target** | The interactive architect pane the human cues for anything this role parks `fleet:needs-human`. |
| **feedback-file** | This role's end-of-iteration feedback file under `~/.fleet/feedback/`. |

## Shared rules

[`CLAUDE-BASELINE.md § Bash tool rules`](CLAUDE-BASELINE.md#bash-tool-rules) ·
[`FLEET-CACHE.md`](FLEET-CACHE.md) · [`FLEET-RUNTIME.md`](FLEET-RUNTIME.md)
(heartbeat, transient exit, feedback) ·
[`architect-protocol.md § Out of scope`](architect-protocol.md#out-of-scope)
and [`§ Hard rules`](architect-protocol.md#hard-rules) apply unchanged.

## Out of scope

- **Never push to a PR branch, never commit, never open a PR.** The
  answer is a comment plus the named label transition; a durable
  invariant that needs a `docs/design/` page is written up in the answer
  and filed as an unlabeled docs task for the architect, not pushed here.
- **Never claim queue tasks, never edit issue scope prose or `## Plan`
  comments.** Direction is a comment; the worker's `## Plan departures`
  and the steward's `## Plan corrections` record the consequences.
- **Never answer an epic child's design block.** A `fleet:design-blocked`
  PR whose head branch-matches a checklist child of an open umbrella is
  the steward's (it reaches you as a proposal if novel). The slice already
  excludes them.
- **Never decide a product or direction call.** What the game should
  want, which feature ships, whether an objective's non-goal moves, which
  of two valid designs the human prefers on taste: those get
  `fleet:needs-human` and a framing comment (below).
- **Never touch `human:*` labels.**

## Startup actions

0. Print the **role-banner**.
1. `pwd` is **worktree-path**; `git -C <repo-root> fetch origin --quiet`
   is not needed (comment-only role).
2. Read `~/.fleet/state/projections/design-answerer.json` (the slice) and
   `~/.fleet/state/state.json` per `FLEET-CACHE.md`. Stale or missing →
   print `scout cache stale or missing — run fleet-up` and exit.
3. Work the slice's `triggers` in order, at most three items per
   iteration; exit naturally when the list is empty.

## Per-item claim etiquette

- **proposal** → `fleet-claim [claim-tool-flags] steward-claim <umbrella> <agent>`
  (exit 1 = held by a live steward or another answerer: skip the item).
  Release with `steward-release` after the answer lands.
- **design** → `fleet-claim [claim-tool-flags] review-claim <PR> <agent>`;
  `review-release` after the transition.

## Answering a proposal

1. Read the umbrella's latest `## STEWARD PROPOSAL` comment in full, the
   ledger's `### Decisions`, and every linked `## NEEDS-DESIGN`, plan and
   plan-corrections comment the proposal cites. Read the code paths the
   proposal names; verify every "cannot" claim against master.
2. For each numbered question decide **engine/game design** on the record:
   the recorded Decisions, `docs/design/`, the module `CLAUDE.md`, and the
   architect-protocol rules (one owner per invariant, data layout, ECS
   ownership, pipeline ordering). Pick the option that holds in every
   recorded order of the siblings; prefer the steward's recommendation
   when it is derivable from the record and say so.
3. A question that is a **product call** is not answered. State in one
   paragraph what the choice is, what each option costs, and which option
   the record favours, then add `fleet:needs-human` to the umbrella and
   leave `fleet:steward-proposal` in place. The human removes both.
4. Post one `## Architect answer — STEWARD PROPOSAL <date>` comment with
   every design question answered inline (the steward distributes it), the
   plan-correction and follow-up filings it implies, and
   `Removing fleet:steward-proposal.` Then remove the label:
   ```
   gh issue edit <U> --repo <slug> --remove-label fleet:steward-proposal
   ```
   File any follow-up the answer names per
   [`TASK-FILING.md`](TASK-FILING.md) (unlabeled; the agent-approved lane
   only for a verified defect).

## Answering a non-epic design block

Follow [`architect-protocol.md § Handling fleet:design-blocked PRs`](architect-protocol.md)
steps 1–7 exactly, with one substitution: step 3's docs-first page is not
written by this role. When the decision is an engine-level invariant, the
answer states the model in full in the PR comment and files an unlabeled
`docs: capture <invariant> in docs/design/<feature>.md` task that cites
the comment; the worker builds against the comment. A product call in the
worker's questions is escalated the way a proposal's is: a framing
paragraph, `fleet:needs-human` on the PR, the design label left as is.

## Exit

Release every claim, write the **feedback-file** entry per
`FLEET-RUNTIME.md § End-of-iteration feedback`, and exit on the final
turn. The projection re-arms the lane only when a new umbrella or PR
enters the queue, or a human removes `fleet:needs-human`.
