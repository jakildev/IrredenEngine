# file-epic — shared flow

The canonical `file-epic` flow: take an approved architect plan that covers
a multi-ticket epic and file it as a fleet expects — an umbrella issue
labelled with the repo's **epic label**, one child per phase labelled with
the **task label**, each **child plan posted as a `## Plan` issue comment**
(the canonical plan per the #1932 redesign — its implementer commits
`.fleet/plans/issue-<N>.md` as the **first commit of the child's impl PR**,
so there is **no separate per-child plan-doc PR**), and post-filing stack
validation.

> **Umbrella plan + steward ledger stay a committed file.** The umbrella has
> no impl PR to carry its plan, and the epic-steward maintains its `## Steward
> ledger` in the committed `.fleet/plans/issue-<umbrella>.md` via its own
> docs PRs (`epic-steward-protocol.md`). So this flow commits **only** the
> umbrella plan (step 6.5), not the children's. Moving the umbrella plan into
> a `## Plan` comment too (per #1932 PR4's full intent) is coupled to a
> matching epic-steward-protocol change (the ledger's home) and is a tracked
> follow-on — see step 6.5.

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
| **plans dir** | Local staging for the plan, pre-commit (umbrella only). | `~/.fleet/plans/issue-<N>.md` |
| **repo-side plan path** | Committed, authoritative plan workers read from master (umbrella only). | `<repo>/.fleet/plans/issue-<N>.md` |
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

After saving, append the initial `## Steward ledger` section (schema from
[`epic-steward-protocol.md`](../epic-steward-protocol.md)) to the staged plan.
Child rows are not yet known — they are seeded in step 5.5 after filing:

```bash
cat >> <plans-dir>/issue-<N>.md << 'EOF'

## Steward ledger

reconciled-through: <YYYY-MM-DD>
proposal-pending: none

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|

### Decisions
<!-- entries: D<n> (<YYYY-MM-DD>): <decision> — source: <link>  (numbered scheme per epic-steward-protocol.md §Decisions; escalation rules reference decisions by D-id) -->

### Events
- <YYYY-MM-DD>: filed via file-epic
EOF
```

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
**Model:** <fable|opus|sonnet>
**Effort:** <low|medium|high|xhigh|max>   <!-- optional; omit for the class default -->
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

### 5.5. Seed the Children checklist and ledger rows (REQUIRED)

After all children are filed and every issue number is known:

**a. Insert child rows into the `### Children` table** in the `## Steward
ledger` section of `<plans-dir>/issue-<N>.md` — one row per filed child,
state `open`. Use the Write tool to update `<plans-dir>/issue-<N>.md`:
locate the empty table separator row (the `|---|---|---|---|---|` line under
`### Children`) and append one row per child below it.

```
| #<child-N> | open | — | plan | <YYYY-MM-DD> |
```

**b. Write the `## Children` checklist** into the umbrella issue body in one
edit (never one edit per child — the steward's membership merge is based on
a single coherent checklist section):

```bash
gh issue view <umbrella> --repo <repo> --json body --jq '.body' > .file-epic-umbrella-body.md
cat >> .file-epic-umbrella-body.md << 'EOF'

## Children

- [ ] #<N1> — <title of child 1>
- [ ] #<N2> — <title of child 2>
EOF
gh issue edit <umbrella> --repo <repo> --body-file .file-epic-umbrella-body.md
rm -f .file-epic-umbrella-body.md
```

> **Checklist format is load-bearing.** The scout's parser and the
> `--check-checklist` validator (step 7.5) both take the **first** `#N` on
> each checkbox line. Keep the format plain: `- [ ] #N — title`. No bold
> refs or extra tokens before the number.

The `## Children` checklist is the steward's **membership source of truth**
for this epic. Pre-protocol epics get healed on the steward's first claim;
new filings must be born managed — skip this step and the checklist is
missing from the start.

### 6. Post each child's plan as a `## Plan` comment

After each child is filed, post its plan as a **`## Plan` comment on the child
issue** — the canonical plan per the #1932 redesign. The queue gate keys on this
comment, and the child's implementer commits it to `.fleet/plans/issue-<N>.md`
as the **first commit of the child's impl PR** (no separate plan-doc PR). Write
the comment body to a temp file, then
`gh issue comment <N> --repo <repo> --body-file <file>`. The comment body
(`<N>` is the tracker-assigned number):

```markdown
## Plan: <ticket title>

- **Issue:** #<N>
- **Model:** <fable|opus|sonnet>
- **Date:** <YYYY-MM-DD>
- **Epic:** #<umbrella> — see `<plans-dir>/issue-<umbrella>.md` for full context
- **Blocked by:** (none)   <!-- or same-repo `#<prior>[, #<other>]`; same exact form as the issue body -->

### Scope
<focused subset of the umbrella plan that applies to THIS ticket>

### Affected files
<bullet list of file paths the implementer will touch>

### Approach
<more detail than the issue body — the implementation guide>

### Acceptance criteria
<concrete, testable>

### Gotchas
<watchouts that need code-aware framing>

### Verification
<what to build, what to run, what passes>
```

The per-ticket plan adds **implementation detail beyond the issue body**.
Don't duplicate — point at the umbrella plan for arch context, then add file
paths, code-level approach, and verification.

Each per-ticket plan must carry the **same rigor as a standalone plan**.
[`PLANNING-PROTOCOL.md`](../PLANNING-PROTOCOL.md) step 2 is the **canonical
rigor list** — treat this paragraph as a pointer to it, not a replacement, so
the bar can't drift out of sync (it has before: #2012's gate and the
cross-system-audit requirement landed in PLANNING-PROTOCOL but not here). At
minimum a child plan must: state the **verified current state** of the code
this child touches (with a **confirmed repro against the actual code path**
when the child fixes a defect); commit to **one approach** — never a "confirm
during investigation/design" hand-off to the worker; and **reconcile siblings
+ in-flight PRs** on the same surface (including the epic's other children: say
what this child assumes its predecessors have landed).

Two step-2 requirements bite epic children specifically, because an epic child
is **usually a migration or a shared-resource change**:

- **Cross-system audit.** When the child deletes or migrates a shared resource
  (a component, system, SSBO/GPU buffer, public API, coordinate convention,
  helper retired across many call sites — exactly the shape most epics take),
  list **every consumer** with a per-consumer migration note. Audit by grep on
  the type/symbol name **and** on any slot/binding numbers. Without it the
  worker discovers gaps mid-task and design-blocks.
- **Source-verify negative/gap claims.** Any "the engine does not do X today" /
  "Y is missing" claim that motivates the child's work must be checked across
  the **full candidate set** before the approach commits — a negative is true
  only if every candidate was checked (#1814).

A child plan that just restates the umbrella's phase line is a stub
— #1440's 23-line stub prescribed an approach a sibling had already proved
wrong (#1456). The queue's plan gate checks only that a `## Plan` comment
exists; the rigor is on the author.

### 6.5. Commit the umbrella plan into the repo (REQUIRED — the steward reads it)

Only the **umbrella** plan (with its `## Steward ledger`) is committed by this
flow. The children's plans are `## Plan` comments now (step 6), and each child's
plan file lands via the **first commit of its own impl PR** (#1932) — `file-epic`
no longer opens a per-child plan-doc PR. But the umbrella has no impl PR, and the
epic-steward maintains the ledger in the committed
`.fleet/plans/issue-<umbrella>.md` via its iteration docs PRs
(`epic-steward-protocol.md`), so the umbrella plan must live in the repo.

```bash
mkdir -p <repo-root>/.fleet/plans
cp <plans-dir>/issue-<umbrella>.md <repo-root>/.fleet/plans/issue-<umbrella>.md
# branch + commit + PR via the commit-and-push skill (docs-only, reviews fast).
# Filename keeps the issue-<N>.md form — there is NO T-<NNN> rename.
```

The PR is docs-only. It does **not** gate child claimability — children carry
their plan in the `## Plan` comment, which the queue gate (`fleet-queue-ingest`,
#1932 PR2) reads directly — but land it promptly so the steward and any
cross-host reader see the umbrella plan + ledger from master.

> **Follow-on (#1932 PR4 full intent).** Moving the umbrella plan + ledger out
> of a committed file and into the umbrella's `## Plan` comment requires a
> matching `epic-steward-protocol.md` change (the steward would edit the comment
> instead of writing the file via its docs PR). That coupling is out of this
> flow's scope; until it lands, the umbrella plan stays a committed file.

### 7. Post the umbrella summary comment

After all children are filed, write the summary body to a file first, then:

```bash
gh issue comment <umbrella> --repo <repo> --body-file .file-epic-body.md
rm -f .file-epic-body.md
```

The summary is the single source of truth linking the umbrella to its
children: a phase/ticket/title/model table for the closing path, an
independent-follow-on table, an ASCII dependency chain, closing criteria,
and any shape changes from the original phasing. Include one line pointing
at the Steward ledger: `Steward ledger: <repo-side-plan-path>/issue-<N>.md §Steward ledger`.

### 7.5. Validate filed bodies (STOP on failure)

Before reporting success, assert every child carries the structured fields
the queue machinery reads — a standalone `**Model:** fable|opus|sonnet`
line (fable is opt-in for genuinely hard work; see FLEET.md "Model
split"), a standalone `**Part of epic:** #<umbrella>` line, and (for every
non-head child) a standalone `**Blocked by:** #N` line. An optional
`**Effort:**` line, when present, must be low|medium|high|xhigh|max. The **validate-stack
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

Once `--check-checklist` is available (tracked in #1667), also validate the
umbrella's `## Children` checklist:

```bash
<validate-stack-command> <umbrella> --check-checklist
<validate-stack-command> <umbrella> --check-checklist --repo game  # game repo
```

### 7.6. How the human gates interact (file-with-plan model, #2012)

Epic children filed this way are **file-with-plan**: each carries its `## Plan`
comment (step 6), so the planning gate is already satisfied. The consequence
for the human gates:

- A child **queues directly** once the human stamps `human:approved` on it —
  no worker re-plan round, because the plan is already posted.
- A child does **not** hit the worker-planning `human:review-plan` fallback.
  That gate (#2012) holds a *worker-authored* plan for a human's approach
  sign-off; an epic filed from a design conversation already had the human in
  the planning loop, and their approval of the umbrella plan **is** the
  approach sign-off. So **do not pre-stamp `human:review-plan`** on children.
- **Exception the human owns:** if one child is independently high-stakes
  (PLANNING-PROTOCOL.md step 3 checklist — ambiguous approach, cross-cutting,
  expensive/hard-to-reverse, or changes a public contract) *beyond* what the
  umbrella plan settled, the human may add `human:review-plan` to that child
  for a separate per-child hold. It is a queue-block until they clear it. This
  is a human action, not something this flow applies by default.

What the human still owns regardless: **per-child `human:approved` triage.**
The `fleet:task` label means "ready for human triage" — the queue-manager
never auto-approves children. The human approves each child individually (and
the `**Blocked by:**` chain still gates the dependents until their
predecessors close).

If, on reading a child's `## Plan` comment, the human wants its **approach
reworked**, they don't hand-swap labels — they **add the single
`human:revise-plan` label** (#2052) plus a comment, and the fleet re-plans the
child for them (adds `fleet:needs-plan`, strips stale stage labels, keeps
`human:approved`). See
[`PLANNING-PROTOCOL.md § Human: requesting plan changes`](../PLANNING-PROTOCOL.md#human-requesting-plan-changes-humanrevise-plan).

> **Stale child before it's worked?** If a filed child's plan goes stale
> (a predecessor shipped a different design than its `## Plan` assumed) before
> the child is claimed, it re-plans through the flip-and-move-on flow — flip
> `fleet:queued → fleet:needs-plan`, comment why, and move on; the dispatcher
> assigns the re-plan to a fresh planning dispatch (#2197). See
> [`PLANNING-PROTOCOL.md § Re-planning a stale queued plan`](../PLANNING-PROTOCOL.md#re-planning-a-stale-queued-plan)
> (#1999). `file-epic` does not re-file the child.

### 8. Report

Reply with: the umbrella issue URL + epic-label confirmation; the child
issue URLs; the umbrella plan-file path; the per-child `## Plan` comment links; the
validate-stack result (must pass); and what the user still must do
(triage each child individually with `human:approved` — the queue-manager
won't auto-approve; do not pre-stamp `human:review-plan`, per step 7.6).

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
- ❌ Per-child `## Plan` comments that duplicate the issue body verbatim. The
  plan adds implementation detail; the issue body is the discussion surface.
- ❌ Per-child `## Plan` comments that restate the umbrella's phase line and stop.
  Each child plan meets PLANNING-PROTOCOL.md step-2 rigor on its own:
  verified current state / confirmed repro, one picked approach, sibling +
  in-flight reconciliation (#1456).
- ❌ A migration/deletion child plan with **no cross-system audit**. Most epic
  children change a shared resource (retire a helper, change a public API,
  migrate every consumer); a plan that doesn't enumerate every consumer +
  per-consumer migration is a stub (step 6, PLANNING-PROTOCOL.md step 2).
- ❌ Pre-stamping `human:review-plan` on children. They're file-with-plan, so
  the umbrella-plan approval is the approach sign-off (step 7.6, #2012). The
  human adds that label only for a child that's high-stakes beyond the umbrella.
- ❌ Skipping the umbrella summary comment — the umbrella↔children
  cross-reference then isn't visible from the umbrella's own thread.
- ❌ Filing an epic for changes that are really one ticket. If the scope
  fits in one PR, file one issue.
- ❌ Inline-authoring the umbrella plan file. Always copy from the
  **architect plans dir** — the architect plan is the canonical source.
- ❌ Filing an epic without seeding the `## Children` checklist (step 5.5b)
  or the `## Steward ledger` (step 2 + step 5.5a) — the steward heals
  pre-protocol epics, but new filings must be born managed.

## Plan file lifecycle

| Location | Purpose | Lifecycle |
|---|---|---|
| **architect plans dir** (`<slug>.md`) | Architect's draft, approved by user. | Session-local; archive after the epic ships. |
| **child `## Plan` comment** (on each child issue) | Canonical child plan; the queue gate reads it (#1932). | Posted in step 6; committed to `.fleet/plans/issue-<N>.md` as the first commit of the child's impl PR. |
| **umbrella plan** (`issue-<umbrella>.md`) | Umbrella plan + `## Steward ledger`; the steward reads/maintains it. | Staged in step 2; committed via the step-6.5 docs PR; maintained thereafter by the epic-steward's iteration docs PRs until close-out. |

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
