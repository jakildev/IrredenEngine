# file-epic — shared flow

The canonical `file-epic` flow: take an approved architect plan that covers
a multi-ticket epic and file it as a fleet expects — an umbrella issue
labelled with the repo's **epic label**, one child per phase labelled with
the **task label**, per-ticket plan files in the **plans dir** so the
queue-manager copies them into the repo on ingest, and post-filing stack
validation.

Every repo that runs a fleet keeps its
`.claude/skills/file-epic/SKILL.md` as a thin wrapper that points here and
supplies only its **deltas** (below). See
[`docs/design/skill-sharing.md`](../../design/skill-sharing.md) for the
mechanism.

---

## Repo deltas this flow needs

| Delta key | What it is | Engine value |
|---|---|---|
| **repo** | The `gh --repo` slug the epic targets. | `jakildev/IrredenEngine` (or `--repo game` for the game repo) |
| **epic label** | Marks the umbrella so the queue-manager skips it. | `fleet:epic` |
| **task label** | Marks each child as a queue-ingestable task. | `fleet:task` |
| **architect plans dir** | Where the approved architect draft lives. | `~/.claude/plans/<slug>.md` |
| **plans dir** | Worker-facing plan store the queue-manager reads. | `~/.fleet/plans/issue-<N>.md` |
| **repo-side plan path** | Final repo-side plan after ingest rename. | `<repo>/.fleet/plans/T-<NNN>.md` |
| **validate-stack command** | Asserts every child carries the structured fields. | `fleet-validate-stack` |
| **title area vocabulary** | `<area>` tokens for child titles. | `engine`, `render`, `game`, module names |

---

## When to invoke

- `/file-epic` (architect plan already approved and saved in the
  **architect plans dir**).
- "file the epic", "ship the epic", "open the tickets for this plan".
- After plan mode returns a plan proposing multiple follow-up tickets and
  the user says "proceed" / "go" / "ship it".

Do not invoke before the user has approved the plan. The user gates the
move from "plan in **architect plans dir**" to "tickets in **plans dir** +
the issue tracker".

---

## Preconditions

1. **Plan file exists** in the **architect plans dir** (or the user names a
   path). It must include: an umbrella issue number to attach to; one
   section per child ticket with a title, model tag (`[opus]`/`[sonnet]`),
   and acceptance criteria; and a dependency chain.
2. **`gh` authenticated** (`gh auth status`).
3. **Repo identified.** Confirm the **repo** from the plan header or ask
   once.

---

## Flow

### 1. Identify umbrella issue + target repo

From the plan's Context section extract the umbrella issue number and target
**repo**. If either is ambiguous, ask once.

Confirm the umbrella exists and isn't already an epic:

```bash
gh issue view <N> --repo <repo> --json number,title,labels,state
```

If it already carries the **epic label** and has children filed (check
`gh issue list --repo <repo> --search "Part of epic: #<N>"`), STOP and
report — don't double-file.

### 2. Save the umbrella plan

```bash
cp <architect-plans-dir>/<slug>.md <plans-dir>/issue-<N>.md
```

This is the worker-facing reference. The queue-manager copies per-ticket
files into the repo as the **repo-side plan path** at ingest; the umbrella
plan itself stays at the `issue-<N>` filename for the umbrella's lifetime.

### 3. Apply the epic label to the umbrella

```bash
gh issue edit <N> --repo <repo> --add-label "<epic-label>"
```

The **epic label** tells the queue-manager to skip the umbrella (children
ingest individually) and to auto-close the umbrella when all children close.

### 4. Parse the plan's child-ticket sections

For each child section extract: the descriptive title (drop the architect's
internal `T-XXX` slug — the tracker assigns its own number and the
queue-manager assigns the canonical `T-NNN` at ingest); model tag; scope,
approach, acceptance criteria, dependencies, gotchas.

### 5. File children sequentially (NOT in parallel)

Sequential filing matters because the tracker assigns numbers in order
(later issues reference earlier ones), and the dependency chain needs the
real predecessor number before you can write `Blocked by: #<actual N>`.

For each child, write the body to a temp file first (avoids
command-substitution and backtick hazards):

```
rm -f .file-epic-body.md
[Write tool → .file-epic-body.md → content:]
**Model:** <opus|sonnet>
**Part of epic:** #<umbrella>
**Plan file:** `<plans-dir>/issue-<umbrella>.md` (full epic plan)
**Blocked by:** (none)

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

> **`Blocked by:` MUST be machine-parseable — this is not stylistic.** Write
> it as literally `(none)` for an unblocked head ticket, or as one or more
> same-repo `#N` refs (`**Blocked by:** #1487, #1490`) for a dependent one.
> The scout, `fleet-claim`, and `fleet-queue-ingest` recognize only `(none)`
> and `#N` (plus merged-PR URLs). Free-text like `none — first unblocked` is
> read as an unresolved *prose* blocker, so the ticket is held out of the
> queue forever and every child that chains off it stalls too. A cross-repo
> dependency is machine-gated too — qualify the ref with its repo:
> `**Blocked by:** jakildev/IrredenEngine#1476` (the bare `IrredenEngine#1476`
> or `irreden#125` form also works). All three tools route the qualified ref's
> state check to the *referenced* repo (#1522); a bare `#N` still resolves
> against the issue's own repo.

Then file:

```bash
gh issue create --repo <repo> --label "<task-label>" \
  --title "<area>: <descriptive title> (T-<slug>)" \
  --body-file .file-epic-body.md
rm -f .file-epic-body.md
```

Title convention: `<area>: <descriptive title> (T-<slug>)` using one of the
repo's **title area vocabulary** tokens. The `(T-<slug>)` suffix preserves
the architect's internal numbering; the queue-manager assigns the canonical
`T-NNN` separately. Capture the issue number the tracker returns.

### 6. Write per-ticket plan file

After each child is filed, write `<plans-dir>/issue-<N>.md` (where `<N>` is
the tracker-assigned number):

```markdown
# Plan: <ticket title>

- **Issue:** #<N>
- **Model:** <opus|sonnet>
- **Date:** <YYYY-MM-DD>
- **Epic:** #<umbrella> — see `<plans-dir>/issue-<umbrella>.md` for full context
- **Blocked by:** (none)   <!-- or same-repo `#<prior>[, #<other>]`; same exact form as the issue body -->

## Scope
<focused subset of the umbrella plan that applies to THIS ticket>

## Affected files
<bullet list of file paths the implementer will touch>

## Approach
<more detail than the issue body — the implementation guide>

## Acceptance criteria
<concrete, testable>

## Gotchas
<watchouts that need code-aware framing>

## Verification
<what to build, what to run, what passes>
```

The per-ticket plan adds **implementation detail beyond the issue body**.
Don't duplicate — point at the umbrella plan for arch context, then add file
paths, code-level approach, and verification.

### 7. Post the umbrella summary comment

After all children are filed, write the summary body to a file first, then:

```bash
gh issue comment <umbrella> --repo <repo> --body-file .file-epic-body.md
rm -f .file-epic-body.md
```

The summary is the single source of truth linking the umbrella to its
children: a phase/ticket/title/model table for the closing path, an
independent-follow-on table, an ASCII dependency chain, closing criteria,
and any shape changes from the original phasing.

### 7.5. Validate filed bodies (STOP on failure)

Before reporting success, assert every child carries the structured fields
the queue machinery reads — a standalone `**Model:** opus|sonnet` line, a
standalone `**Part of epic:** #<umbrella>` line, and (for every non-head
child) a standalone `**Blocked by:** #N` line. The **validate-stack
command** checks exactly this, auto-discovers the children, and fails
loudly:

```bash
<validate-stack-command> <umbrella>              # default repo
<validate-stack-command> <umbrella> --repo game  # game repo
```

If it exits **non-zero, STOP** — do not proceed to step 8. Patch the
offending bodies (`gh issue edit <N> --body-file ...`) or re-file, then
re-run until it passes. Never report success on a stack the validator
rejects. A prose-only `Blocked on …` header is read only as a *fallback*;
the canonical standalone `**Blocked by:** #N` line is what the
`--search "Part of epic: #N"` discovery and the queue parsers rely on.

### 8. Report

Reply with: the umbrella issue URL + epic-label confirmation; the child
issue URLs; the umbrella plan-file path; the per-ticket plan-file paths; the
validate-stack result (must pass); and what the user still must do
(typically: triage each child individually — the queue-manager won't
auto-approve).

---

## Anti-patterns

- ❌ Filing children in parallel — breaks the "later issues reference
  earlier ones" pattern.
- ❌ Forgetting the **epic label** on the umbrella — the queue-manager
  would ingest the umbrella itself.
- ❌ Putting `Blocked by:` in header prose instead of a standalone line.
  The scout and `fleet-claim` parsers read **only** standalone
  `**Blocked by:** #N` lines (a `Blocked on …` header is a fallback). This
  is the #1 hand-filing drift; step 7.5 catches it — don't rely on the
  catch, write the standalone line.
- ✅ Multiple blockers are supported: `**Blocked by:** #A, #B` on one line
  or several `**Blocked by:**` lines. The gate unions every ref.
- ❌ Per-ticket plan files that duplicate the issue body verbatim. The plan
  adds implementation detail; the issue is the discussion surface.
- ❌ Skipping the umbrella summary comment — the umbrella↔children
  cross-reference then isn't visible from the umbrella's own thread.
- ❌ Filing an epic for changes that are really one ticket. If the scope
  fits in one PR, file one issue.
- ❌ Inline-authoring the umbrella plan file. Always copy from the
  **architect plans dir** — the architect plan is the canonical source.

## Plan file lifecycle

| Location | Purpose | Lifecycle |
|---|---|---|
| **architect plans dir** (`<slug>.md`) | Architect's draft, approved by user. | Session-local; archive after the epic ships. |
| **plans dir** (`issue-<N>.md`) | Worker-facing plan (umbrella or per-ticket). | Persists. Queue-manager copies per-ticket files into the repo at ingest. |
| **repo-side plan path** (`T-<NNN>.md`) | Final repo-side plan, renamed at ingest. | Lives in the repo until the ticket closes. |

## When the architect plan is rough

If the plan is missing per-ticket detail (just an umbrella sketch), STOP and
ask whether to (1) send it back for refinement (default — incomplete plans
make thin tickets that get bounced) or (2) file a placeholder umbrella + one
child per gap with "scope TBD" bodies. Don't file thin tickets without
confirming.

## Cross-repo information isolation

The flow is repo-neutral — pass `--repo` consistently or read it from the
plan header. The cross-repo info-isolation rule still applies: artifacts in
one repo must not reference another repo by name or feature. If a plan
authored from one repo's session mentions another repo's specifics, scrub
them before filing.
