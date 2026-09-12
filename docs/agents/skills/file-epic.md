# file-epic — shared flow

Take an approved architect plan covering a multi-ticket epic and file it as
the fleet expects: an umbrella issue carrying the **epic label**, one child
per phase carrying the **task label**, a `## Plan` comment on every child
and on the umbrella, a `## Steward ledger` comment on the umbrella, and
post-filing stack validation. Nothing is committed — every plan artifact
is an issue comment.

Each repo's `.claude/skills/file-epic/SKILL.md` is a thin wrapper that
points here and answers the delta keys below
([`docs/design/skill-sharing.md`](../../design/skill-sharing.md)).

---

## Repo deltas this flow needs

| Delta key | What it is | Engine value |
|---|---|---|
| **repo** | The `gh --repo` slug the epic targets. | `jakildev/IrredenEngine` (or `--repo game` for the game repo) |
| **epic label** | Marks the umbrella so the queue-manager skips it. | `fleet:epic` |
| **task label** | Marks each child as a queue-ingestable task. | `fleet:task` |
| **architect plans dir** | Where the approved architect draft lives. | `~/.claude/plans/<slug>.md` |
| **validate-stack command** | Asserts every child carries the structured fields. | `fleet-validate-stack` |
| **title area vocabulary** | `<area>` tokens for child titles. | `engine`, `render`, `game`, module names |

---

## When to invoke

`/file-epic`, "file the epic", "ship the epic", "open the tickets for this
plan", or "proceed" / "go" after plan mode proposed multiple follow-up
tickets. Never before the user has approved the plan.

## Preconditions

- The plan exists in the **architect plans dir** (or a path the user
  names) with: the umbrella issue number; one section per child with a
  title, model tag, and acceptance criteria; a dependency chain.
- `gh auth status` succeeds; the **repo** is confirmed from the plan header
  or asked once.

---

## Flow

### 1. Umbrella

`gh issue view <N> --repo <repo> --json number,title,labels,state`. If it
already carries the **epic label** and `gh issue list --repo <repo>
--search "Part of epic: #<N>"` finds children, stop — don't double-file.

### 2. Draft the steward ledger

Posted in step 5.5 once child numbers are known; schema from
[`epic-steward-protocol.md`](../epic-steward-protocol.md):

```bash
cat > .file-epic-ledger.md << 'EOF'
## Steward ledger

reconciled-through: <YYYY-MM-DD>
proposal-pending: none

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|

### Decisions
<!-- D<n> (<YYYY-MM-DD>): <decision> — source: <link> -->

### Events
- <YYYY-MM-DD>: filed via file-epic
EOF
```

### 3. Label the umbrella

`gh issue edit <N> --repo <repo> --add-label "<epic-label>"` — the
queue-manager skips it and the epic-steward takes it over.

### 4. Parse the children

Per child section: descriptive title (the tracker's issue number is the
canonical id; an architect `T-XXX`/`P<n>` slug is optional readability),
model tag, scope, approach, acceptance criteria, dependencies, gotchas.

### 5. File children sequentially

Sequential because later children reference earlier numbers in `Blocked
by:`. Write each body with the Write tool to `.file-epic-body.md`:

```
**Model:** <fable|opus|sonnet>
**Effort:** <low|medium|high|xhigh|max>   <!-- optional; omit for the class default -->
**Part of epic:** #<umbrella>
**Blocked by:** (none)

## Scope
## Approach
## Acceptance criteria
## Dependencies
## Gotchas
## References
```

`**Blocked by:**` is machine-parsed: exactly `(none)`, or same-repo `#N`
refs (`#1487, #1490` on one line, or several lines — the gate unions
them), or a repo-qualified ref for a cross-repo dependency
(`jakildev/IrredenEngine#1476`, `IrredenEngine#1476`, `irreden#125`, each
resolved against the referenced repo). Free text (`none — first
unblocked`) reads as an unresolved prose blocker and holds the ticket and
every dependent out of the queue.

```bash
gh issue create --repo <repo> --label "<task-label>" \
  --title "<area>: <descriptive title> (<phase-slug>)" \
  --body-file .file-epic-body.md
rm -f .file-epic-body.md
```

`<area>` from the **title area vocabulary**; the `(<phase-slug>)` suffix
is optional. Capture the returned issue number.

### 5.5. Ledger comment and `## Children` checklist

a. Append one row per child below `### Children` in the step-2 draft —
`| #<child-N> | open | — | plan | <YYYY-MM-DD> |` — and post it once:

```bash
gh issue comment <umbrella> --repo <repo> --body-file .file-epic-ledger.md
rm -f .file-epic-ledger.md
```

The steward edits this one comment in place from now on.

b. Append the checklist to the umbrella body in a single edit:

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

Format is load-bearing: the scout's parser and `--check-checklist` read
the **first** `#N` on each checkbox line — plain `- [ ] #N — title`, no
bold or extra tokens before the number. This checklist is the steward's
membership source of truth.

### 6. Each child's `## Plan` comment

Write to a temp file, then `gh issue comment <N> --repo <repo>
--body-file <file>`:

```markdown
## Plan: <ticket title>

- **Issue:** #<N>
- **Model:** <fable|opus|sonnet>
- **Date:** <YYYY-MM-DD>
- **Epic:** #<umbrella> — its `## Plan` comment carries the full epic plan
- **Blocked by:** (none)   <!-- same exact form as the issue body -->

### Scope
### Decisions
### Affected files
### Acceptance criteria
### Gotchas
### Verification
### Approach sketch (optional)
```

The child plan adds locked decisions, file paths, and runnable
acceptance checks beyond the issue body; the implementer owns the path.
Rigor is [`PLANNING-PROTOCOL.md`](../PLANNING-PROTOCOL.md) step 2, in
full — the queue gate only checks that the comment exists. Two step-2
requirements bite epic children in particular:

- **Cross-system audit** when the child deletes or migrates a shared
  resource (component, system, GPU buffer, public API, coordinate
  convention, widely-used helper): every consumer, each with a migration
  note, found by grep on the symbol and on any slot/binding numbers.
- **Source-verified negatives**: an "the engine does not do X" claim that
  motivates the child holds only after every candidate was checked.

A child plan that restates the umbrella's phase line is a stub.

### 6.5. The umbrella's `## Plan` comment

Same form as the children's, on the umbrella, derived from the
**architect plans dir** draft — never inline-authored.

### 7. Umbrella summary comment

`gh issue comment <umbrella> --repo <repo> --body-file .file-epic-body.md`
with: a phase/ticket/title/model table, an independent-follow-on table,
an ASCII dependency chain, closing criteria, any shape changes from the
original phasing, and a pointer to the ledger comment.

### 7.5. Validate (stop on failure)

```bash
<validate-stack-command> <umbrella>                    # default repo
<validate-stack-command> <umbrella> --repo game        # game repo
<validate-stack-command> <umbrella> --check-checklist  # the ## Children checklist
```

It asserts every child's standalone `**Model:**`, `**Part of epic:**`,
and (non-head) `**Blocked by:**` lines and an optional valid
`**Effort:**`. Non-zero exit → patch the bodies (`gh issue edit <N>
--body-file …`) and re-run; never report success on a rejected stack.

### 7.6. Human gates

Children filed this way are file-with-plan: the human's approval of the
umbrella plan is the approach sign-off, so a child queues as soon as the
human stamps `human:approved` on it — per child, never auto. A child whose
approach the human wants reworked gets the `human:revise-plan` label plus
a comment and the fleet re-plans it
([`PLANNING-PROTOCOL.md § Human: requesting plan changes`](../PLANNING-PROTOCOL.md#human-requesting-plan-changes-humanrevise-plan));
one to hold takes `fleet:needs-human`. A child whose plan goes stale
before it is claimed flips `fleet:queued → fleet:needs-plan` with a
comment
([`PLANNING-PROTOCOL.md § Re-planning a stale queued plan`](../PLANNING-PROTOCOL.md#re-planning-a-stale-queued-plan));
`file-epic` does not re-file it.

### 8. Report

Umbrella URL + epic-label confirmation; child URLs; links to the
umbrella's `## Plan` and `## Steward ledger` comments and each child's
`## Plan`; the validate-stack result; and that the human still approves
each child individually.

---

## Anti-patterns

- Filing children in parallel.
- Filing an epic for what fits in one PR — file one issue.
- Per-child plans that duplicate the issue body verbatim, or that restate
  the umbrella's phase line and stop.

## When the plan is rough

If per-ticket detail is missing, stop and ask: send it back for refinement
(default), or file a placeholder umbrella plus "scope TBD" children.

## Cross-repo isolation

Pass `--repo` consistently. Artifacts in one repo never reference another
repo by name or feature; scrub a plan authored in the other repo's session
before filing.
