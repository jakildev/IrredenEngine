---
name: file-epic
description: >-
  Take an approved architect plan and file it as the fleet expects: umbrella
  issue labeled fleet:epic, one child fleet:task per phase, per-ticket plan
  files at ~/.fleet/plans/issue-<N>.md, and post-filing stack validation.
---

# file-epic

Take an architect plan that covers a multi-ticket epic and file it as
the fleet expects: umbrella issue labeled `fleet:epic`, one child
`fleet:task` per phase, per-ticket plan files at
`~/.fleet/plans/issue-<N>.md` so the queue-manager copies them into
the repo on ingest.

> **Placement (per #1312):** this skill is repo-tracked as a project
> skill in each repo's `.claude/skills/file-epic/` — this is the engine
> copy; the private game repo carries its own. It loads by cwd, exactly
> like `commit-and-push` / `review-pr`. Edit the copy in the repo you're
> filing for; keep the shared core in sync.

## When to invoke

- `/file-epic` (with the architect plan already approved and saved at
  `~/.claude/plans/<slug>.md`)
- "file the epic", "ship the epic", "open the tickets for this plan"
- After ExitPlanMode returns with a plan that proposes multiple
  follow-up tickets and the user says "proceed" / "go" / "ship it"

Do not invoke before the user has approved the plan. The user gates
the move from "plan in `~/.claude/plans/`" to "tickets in
`~/.fleet/plans/` + GitHub".

## Preconditions

1. **Plan file exists** at `~/.claude/plans/<slug>.md` (or the user
   names a path). Plan must include:
   - A clear umbrella issue number to attach to (e.g. "engine #226").
   - One section per child ticket with a title, model tag
     (`[opus]` / `[sonnet]`), and acceptance criteria.
   - A dependency chain.
2. **`gh` authenticated.** Verify via `gh auth status`.
3. **Repo identified.** Most epics target `jakildev/IrredenEngine`
   (engine) or the private game repo (game). Confirm from the plan
   header or ask the user once.

## Flow

### 1. Identify umbrella issue + target repo

From the plan's "Context" section, extract:
- Umbrella issue number (e.g. `#226`).
- Target repo (e.g. `jakildev/IrredenEngine`).

If either is ambiguous, ask the user once before proceeding.

Confirm the umbrella exists and isn't already labeled `fleet:epic`:

```bash
gh issue view <N> --repo <repo> --json number,title,labels,state
```

If it's already labeled and has child issues filed (check by
`gh issue list --repo <repo> --search "Part of epic: #<N>"`), STOP
and report — the epic is already filed. Don't double-file.

### 2. Save the umbrella plan

Copy the architect plan to the fleet plans dir:

```bash
cp ~/.claude/plans/<slug>.md ~/.fleet/plans/issue-<N>.md
```

This is the worker-facing reference. The queue-manager will copy
it into the repo as `.fleet/plans/T-<NNN>.md` when each child gets
ingested, but the umbrella plan itself stays at the issue-<N>
filename for the umbrella's lifetime.

### 3. Apply `fleet:epic` to the umbrella

```bash
gh issue edit <N> --repo <repo> --add-label "fleet:epic"
```

`fleet:epic` tells the queue-manager to skip this issue (children
ingest individually). The queue-manager also auto-closes the
umbrella when all children close.

### 4. Parse the plan's child-ticket sections

For each section like `### T-XXX — <title> `[model]``, extract:
- The descriptive title (drop the `T-XXX` prefix — that's the
  architect's internal slug; GitHub assigns its own number, and
  the queue-manager assigns the canonical `T-NNN` at ingest).
- Model tag (`opus` / `sonnet`).
- Scope, approach, acceptance criteria, dependencies, gotchas.

### 5. File children sequentially (NOT in parallel)

Sequential filing matters because:
- GitHub assigns issue numbers in order. Later issues can reference
  earlier ones by number in their bodies.
- The dependency chain in the plan often has shape "T-222 blocked by
  T-221". You can only write `Blocked by: #<actual N>` after the
  earlier issue is filed.

For each child:

Write the issue body to a temp file first (avoids command-substitution and
backtick hazards in the body text):

```
rm -f .file-epic-body.md
[Write tool → .file-epic-body.md → content:]
**Model:** <opus|sonnet>
**Part of epic:** #<umbrella>
**Plan file:** `~/.fleet/plans/issue-<umbrella>.md` (full epic plan)
**Blocked by:** #<prior-child-number> (if applicable)

## Scope
<one paragraph>

## Approach
<bulleted approach>

## Acceptance criteria
<bulleted criteria>

## Dependencies
<what blocks this, what this blocks>

## Gotchas
<watchouts not obvious from the scope>

## References
<links to related design sections, prior PRs>
```

Then file:

```bash
gh issue create --repo <repo> --label "fleet:task" \
  --title "<area>: <descriptive title> (T-<slug>)" \
  --body-file .file-epic-body.md
rm -f .file-epic-body.md
```

Title convention: `<area>: <descriptive title> (T-<slug>)`. The
`(T-<slug>)` suffix preserves the architect's internal numbering for
cross-reference; the queue-manager assigns the canonical `T-NNN`
separately and renames the plan file at ingest.

Capture the issue number that `gh` returns.

### 6. Write per-ticket plan file

After each child issue is filed, write
`~/.fleet/plans/issue-<N>.md` (where `<N>` is the GitHub-assigned
number, not the architect's slug):

```markdown
# Plan: <ticket title>

- **Issue:** #<N>
- **Model:** <opus|sonnet>
- **Date:** <YYYY-MM-DD>
- **Epic:** #<umbrella> — see \`~/.fleet/plans/issue-<umbrella>.md\` for full context
- **Blocked by:** #<prior> (if applicable)

## Scope
<focused subset of the umbrella plan that applies to THIS ticket>

## Affected files
<bullet list of file paths the implementer will touch>

## Approach
<more detail than the issue body — the issue is for discussion,
this is the implementation guide>

## Acceptance criteria
<concrete, testable>

## Gotchas
<watchouts that need code-aware framing>

## Verification
<what to build, what to run, what passes>
```

The per-ticket plan adds **implementation detail beyond the issue
body**. Don't duplicate — point at the umbrella plan for arch
context, then add the file paths, code-level approach, and
verification steps.

### 7. Post the umbrella summary comment

After all children are filed, write the summary comment body to a file
first (avoids command-substitution and backtick hazards):

```
rm -f .file-epic-body.md
[Write tool → .file-epic-body.md → content:]
## Epic execution plan

Architect plan filed: `~/.fleet/plans/issue-<umbrella>.md`.

### Children — closing path

| Phase | Ticket | Title | Model |
|---|---|---|---|
| 1 | #<id> | <title> | <opus/sonnet> |
| ... | ... | ... | ... |

### Independent follow-on (not gating)

| Phase | Ticket | Title | Model |
|---|---|---|---|
| N | #<id> | <title> | opus |

### Dependency chain

```
<ASCII dependency graph>
```

### Closing criteria

<from the plan>

### Shape changes from the original phasing

<deltas explained, if the architect plan made any>

Tickets ready for `human:approved` triage on individual basis.
```

Then post:

```bash
gh issue comment <umbrella> --repo <repo> --body-file .file-epic-body.md
rm -f .file-epic-body.md
```

The summary comment is the single source of truth linking the
umbrella to its children. Without it, navigating from #226 to
#1067-#1073 requires GitHub's "Linked issues" sidebar, which only
fills in when each child explicitly references the umbrella.

### 7.5. Validate filed bodies (STOP on failure)

Before reporting success, assert every child carries the structured
fields the queue machinery actually reads — a standalone
`**Model:** opus|sonnet` line, a standalone `**Part of epic:**
#<umbrella>` line, and (for every non-head child) a **single-ref**
standalone `**Blocked by:** #N` line. The `fleet-validate-stack`
helper (shipped #1317) checks exactly this, auto-discovers the
children, and fails loudly:

```bash
fleet-validate-stack <umbrella>              # engine
fleet-validate-stack <umbrella> --repo game  # game repo
```

If it exits **non-zero, STOP — do not proceed to step 8.** It prints
each malformed child; patch the offending issue bodies (`gh issue
edit <N> --body-file ...`) or re-file, then re-run until it passes.

Never report success on a stack the validator rejects. A prose-only
`Blocked on ...` header or a multi-`#N` `Blocked by:` line is
**invisible to the scout and `fleet-claim` parsers** — the stack
projects wrong at queue time and costs hours of triage downstream.
This is the belt that the hand-filing convention (step 5) is the
suspenders for.

### 8. Report

Reply with:

- Umbrella issue URL + the `fleet:epic` label confirmation.
- A list of child issue URLs.
- The path to the umbrella plan file.
- Per-ticket plan file paths (e.g.
  `~/.fleet/plans/issue-1067.md ... issue-1073.md`).
- The `fleet-validate-stack` result (must be passing).
- Anything the user still needs to do (typically: triage each child
  individually with `human:approved`, since the queue-manager won't
  auto-approve).

## Anti-patterns

- ❌ Filing children in parallel via concurrent `gh issue create`.
  Breaks the "later issues reference earlier ones" pattern.
- ❌ Forgetting the `fleet:epic` label on the umbrella. The
  queue-manager would otherwise ingest the umbrella itself.
- ❌ Putting `Blocked by:` in header prose ("**Blocked on T1 + docs
  PR #1306**") instead of a standalone line. The scout and
  `fleet-claim` parsers read **only** standalone `**Blocked by:** #N`
  lines; header prose is invisible to the queue machinery. This is
  the #1 hand-filing drift (#1300 / #1308–#1311 all hit it). Step 7.5
  catches it — don't rely on catching it; write the standalone line.
- ❌ Multiple `#N` refs on one `**Blocked by:**` line
  (`**Blocked by:** #1299, #1300`). The current
  `find-stackable-blockers` predicate requires exactly one blocker;
  multi-ref forms project as "blocked" but never stack-claim, so
  workers skip them indefinitely. Wait for an upstream to merge, then
  strip the satisfied ref from the body. (#1296 will live-resolve
  this; until it lands, one `#N` per `Blocked by:` line.)
- ❌ Per-ticket plan files that duplicate the issue body verbatim.
  The plan adds implementation detail (file paths, code-level
  approach); the issue is the discussion surface.
- ❌ Skipping the umbrella summary comment. Without it, the
  cross-reference between umbrella and children isn't visible from
  the umbrella's own conversation thread.
- ❌ Filing an epic for changes that are really one ticket. If the
  scope fits in one PR, file one issue, not seven.
- ❌ Inline-creating plan files for the umbrella. Always copy from
  `~/.claude/plans/`, never re-author. The architect plan is the
  canonical source.

## Engine-repo vs game-repo

The skill is repo-neutral — pass `--repo <owner>/<repo>` consistently
or read it from the plan header. Cross-repo info-isolation rule
applies: engine artifacts must not reference the private game repo by
name or feature. If the architect plan was authored from a game-repo
session and mentions game-specific features, scrub them from the
engine child issues before filing.

## Plan file lifecycle

| Location | Purpose | Lifecycle |
|---|---|---|
| `~/.claude/plans/<slug>.md` | Architect's draft. Created in plan mode, approved by user. | Session-local; can be archived after epic ships. |
| `~/.fleet/plans/issue-<N>.md` | Worker-facing plan. Either umbrella or per-ticket. | Persists. Queue-manager copies per-ticket files into the repo at ingest. |
| `<repo>/.fleet/plans/T-<NNN>.md` | Final repo-side plan. Renamed by queue-manager at ingest time. | Lives in the repo until the ticket closes. |

## When the architect plan is rough

If the architect plan is missing per-ticket detail (just an umbrella
sketch + "we'll figure out the children later"), STOP and ask the
user whether to:

1. Send the plan back for refinement (default — incomplete plans
   produce thin tickets that get bounced).
2. File a placeholder umbrella + one child per gap, with bodies that
   say "scope TBD; plan refinement in flight".

Don't file thin tickets without confirming. Worker-facing surfaces
that get bounced for "not enough info to start" waste the queue.
