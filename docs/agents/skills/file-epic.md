# file-epic — shared flow

The canonical `file-epic` flow: take an approved architect plan that covers
a multi-ticket epic and file it as a fleet expects — an umbrella issue
labelled with the repo's **epic label**, one child per phase labelled with
the **task label**, per-ticket plan files **committed into the repo** at the
**repo-side plan path** so a worker on any host reads them from master, and
post-filing stack validation.

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
| **plans dir** | Local staging for the plan, pre-commit. | `~/.fleet/plans/issue-<N>.md` |
| **repo-side plan path** | Committed, authoritative plan workers read from master. | `<repo>/.fleet/plans/issue-<N>.md` |
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

This stages the umbrella plan locally; step 6.5 commits it (and every
per-ticket plan) into the repo at the **repo-side plan path**. The umbrella
plan keeps the `issue-<N>` filename for the umbrella's lifetime.

### 3. Apply the epic label to the umbrella

```bash
gh issue edit <N> --repo <repo> --add-label "<epic-label>"
```

The **epic label** tells the queue-manager to skip the umbrella (children
ingest individually) and hands the umbrella to the epic-steward, which
maintains its checklist and closes it via the close-out flow once every
child is verified closed (see
[`epic-steward-protocol.md`](../epic-steward-protocol.md)).

### 4. Parse the plan's child-ticket sections

For each child section extract: the descriptive title (drop any architect
`T-XXX`/`P<n>` slug if you prefer — the tracker assigns the canonical issue
number, which is what every downstream reference uses); model tag; scope,
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
  --title "<area>: <descriptive title> (<phase-slug>)" \
  --body-file .file-epic-body.md
rm -f .file-epic-body.md
```

Title convention: `<area>: <descriptive title> (<phase-slug>)` using one of the
repo's **title area vocabulary** tokens. An optional `(<phase-slug>)` suffix
(e.g. `(P1)`) preserves the architect's phase numbering for readability; the
tracker's **issue number** is the canonical identifier every downstream
reference (plan filename, `Blocked by:`, claim) uses. Capture the issue number
the tracker returns.

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

### 6.5. Commit the plan files into the repo (REQUIRED — workers read from master)

The **plans dir** (`~/.fleet/plans/`) is **local staging only** — it is
visible solely on the host that filed the epic. The fleet runs across hosts
(Linux/WSL + macOS), so a worker that claims a child on a *different* host
reads its plan from the **repo-side plan path** (`<repo>/.fleet/plans/issue-<N>.md`,
"synced from master" per the worker role docs). If the plan never lands in the
repo, that worker finds nothing and falls back to the issue body — the
per-ticket detail you just wrote is lost. So the producer (this skill) must
commit the plans; there is **no** automatic queue-manager copy.

Copy the umbrella plan **and** every per-ticket plan into the repo and open a
small docs PR (never commit to the default branch directly):

```bash
mkdir -p <repo-root>/.fleet/plans
cp <plans-dir>/issue-<umbrella>.md <repo-root>/.fleet/plans/issue-<umbrella>.md
for N in <child-1> <child-2> ...; do
    cp <plans-dir>/issue-$N.md <repo-root>/.fleet/plans/issue-$N.md
done
# branch + commit + PR via the commit-and-push skill (filenames keep the
# issue-<N>.md form — there is NO T-<NNN> rename).
```

The PR is docs-only (reviews fast). It does **not** need to merge before the
head child is claimable — same-host workers read local staging — but a
cross-host worker needs it merged, so land it promptly. Naming stays
`issue-<N>.md`; the obsolete `T-<NNN>` rename is retired.

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
| **plans dir** (`issue-<N>.md`) | Local staging, pre-commit (same-host only). | Written in step 6; copied into the repo in step 6.5. |
| **repo-side plan path** (`issue-<N>.md`) | Committed, authoritative plan workers read from master. | Committed via the step-6.5 docs PR; lives in the repo until the ticket closes. |

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
