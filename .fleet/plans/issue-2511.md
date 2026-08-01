# Plan: fleet: architect-managed triage sweep — extend issue auto-triage to downstream repos

- **Issue:** #2511
- **Model:** opus — protocol/doc work plus one bounded script following an existing in-repo pattern; the design decisions are committed below, but the protocol rewording needs care that exceeds a mechanical pass
- **Date:** 2026-07-22

## Verified current state

- `docs/agents/triage-protocol.md` hard-rules "**Engine repo only** until the human extends scope" and is phrased engine-concretely (engine paths in § Inputs and § Invocation). It declares **no** `## Repo deltas this flow needs` table — the delta table exists only on the wrapper side (`.claude/commands/role-triage.md`, 8 keys: **repo-slug**, **repo-root**, **worktree-path**, **role-name**, **role-banner**, **objectives-path**, **singleton-env**, **feedback-file**).
- `scripts/fleet/fleet-decisions` already implements the exact `--repo engine|game|<owner/name>` resolver this issue asks for, and computes the untriaged predicate in code: an open issue with no label starting `fleet:` or `human:` (line ~156). It surfaces untriaged **counts** only — there is no per-issue enumeration or any label-application tooling for triage, on either repo.
- `scripts/fleet/fleet_validate_roles.py` (#1667) is **live**, not future as `docs/design/role-sharing.md` § Future considerations still claims: any protocol that declares a `## Repo deltas this flow needs` table gets exact-match key enforcement against each present wrapper (error on a missing key in a present wrapper; **warn** on a fleet-enabled repo with no wrapper).
- Gated-surface constraint: `.claude/commands/role-*.md` is self-config no autonomous worker can push (deterministic commit gate). Any design requiring an engine-wrapper edit makes the task non-worker-implementable — the approach below is shaped so the engine wrapper needs **zero** edits.
- Sibling reconciliation: **#2494** owns the scout → dispatcher wiring for the *dispatched* engine triage role — this task must not touch scout/dispatcher/state.json surfaces. **game #280** owns the downstream wrapper doc itself, matching this issue's Out-of-scope note.
- Per `role-sharing.md` § "Shared vs. delta", the `fleet:*`/`human:*` label vocabulary is **shared infrastructure**, not a per-repo delta — so "that repo's own label conventions" is satisfied by the shared catalog, and no new `labels-reference` delta key is needed. The genuinely per-repo classification reference is **objectives-path**, already a declared wrapper key.

## Scope

Extend the triage flow so an architect session can run a staged, human-confirmed
triage sweep against any fleet-enabled repo (primary consumer: downstream repos,
which have no triage coverage today), without changing the engine's dispatched
dry-run triage role. One PR: repo-neutral rewording of the canonical protocol + a
new `## Repo deltas this flow needs` table + a new § Architect-managed sweep mode
+ one new `scripts/fleet/fleet-triage-sweep` script + a one-line discovery
pointer in `architect-protocol.md`.

## Approach

1. **`docs/agents/triage-protocol.md` — parameterize by delta keys.**
   - Add a `## Repo deltas this flow needs` table declaring **exactly** the 8
     keys the engine wrapper already answers (names verbatim). Declaring only
     existing keys keeps `fleet-validate-roles` green with **no engine-wrapper
     edit** (the wrapper is gated).
   - Reword engine-concrete references to delta-key form: objectives live at
     **objectives-path**; `gh` calls take **repo-slug**; the § Invocation example
     stays as the engine instantiation, labeled as such.
   - Replace the "Engine repo only" hard rule with a repo-scoping rule: each
     triage run operates on exactly one target repo and never cites another
     repo's private content in output that lands on a public repo. The dispatched
     dry-run mode remains engine-only until a downstream fleet opts in via its own
     wrapper.
2. **New § Architect-managed sweep (staged approvals).** A second invocation
   mode, distinct from the dispatched dry-run role; the dispatched mode's text
   (singleton `FLEET_TRIAGE=1`, `## Triage` comment + `fleet:triage-recommend`,
   digest routing, 10-issue cap) is unchanged. The sweep mode runs interactively
   inside the target repo's architect session on human cue; enumerates via
   `fleet-triage-sweep list --repo <slug>`; stages one JSON file at
   `~/.fleet/triage/<repo>-sweep-<date>.json`; the human confirms in-conversation;
   `fleet-triage-sweep apply` applies confirmed label sets only. Closes stay
   human-executed — the sweep never calls `gh issue close`. Idempotency comes from
   the untriaged predicate itself; `list` annotates re-surfaced issues with their
   prior verdict from `~/.fleet/triage/`.
3. **New script `scripts/fleet/fleet-triage-sweep`** (bash + embedded python,
   same shape as `fleet-decisions`; read-only `gh` except `apply`):
   - Reuse `fleet-decisions`' `resolve_repo` pattern verbatim (`--repo
     engine|game|<owner/name>`, flag before subcommand, dual-spelling validated).
   - `list`: fetch open issues, filter to the untriaged predicate, sort
     oldest-first, print TSV + a human-readable block; annotate entries found in
     prior staging files.
   - `apply <staging-file>`: refuses unless the file carries a top-level
     human-confirmation marker AND per-entry `confirmed: true`; re-fetches labels
     and **skips any issue that gained a `fleet:`/`human:` label since staging**;
     validates proposed labels against an allowlist (`human:approved`,
     `human:no-plan`, `human:owned`, `fleet:sonnet|opus|fable`); applies via `gh
     issue edit --add-label`; never calls `gh issue close`; appends an audit line
     to `~/.fleet/triage/log.jsonl`. `--dry-run` prints the would-be commands.
   - Register in `fleet-help`'s index and `install.sh`'s symlink set.
4. **Discovery pointer:** one line in `docs/agents/architect-protocol.md` (not
   gated) noting the sweep cue and pointing at triage-protocol.md § Architect-managed
   sweep.
5. **Drive-by staleness fix:** correct `docs/design/role-sharing.md` § Future
   considerations' claim that the role lint doesn't exist yet.
6. **Test:** `scripts/fleet/tests/test_triage_sweep.sh` following the
   `test_decisions.sh` fixture pattern.

## Affected files

- `docs/agents/triage-protocol.md` — delta-key table; repo-neutral rewording; scoping rule replacing "Engine repo only"; new § Architect-managed sweep
- `scripts/fleet/fleet-triage-sweep` — new script (`list` / `apply`, `--repo` resolver, confirmation + race guards, audit log)
- `scripts/fleet/tests/test_triage_sweep.sh` — new test per the fixture pattern
- `scripts/fleet/install.sh`, `scripts/fleet/fleet-help` — register the new tool
- `docs/agents/architect-protocol.md` — one-line discovery pointer
- `docs/design/role-sharing.md` — one-sentence lint-staleness correction
- **Deliberately untouched:** `.claude/commands/role-triage.md` (gated; design requires no edit), scout/dispatcher/state surfaces (#2494's lane), any downstream-repo file (game #280's lane)

## Acceptance criteria

- `fleet-triage-sweep list --repo engine` run against the live repo emits at
  least one row and every emitted issue has no `fleet:`/`human:`-prefixed label;
  an issue carrying either prefix never appears.
- `fleet-triage-sweep apply` on a staging file **without** the confirmation
  marker exits non-zero with a clear message and issues no `gh` write.
- `fleet-triage-sweep apply --dry-run` on a confirmed fixture prints `gh issue
  edit --add-label` commands matching the staged set, contains no `gh issue
  close`, and the race-guard fixture is reported skipped.
- `fleet-validate-roles` passes: triage-protocol.md's declared table exact-matches
  the engine wrapper's existing 8 keys (no engine wrapper diff in the PR; a warn
  for the not-yet-existing downstream wrapper is expected and acceptable until
  game #280 lands).
- `docs/agents/triage-protocol.md` contains § Architect-managed sweep and no
  longer contains the "Engine repo only" hard rule; the dispatched-mode text is
  preserved in meaning.
- `scripts/fleet/tests/test_triage_sweep.sh` passes.

## Gotchas

- **The engine wrapper is gated.** If implementation surfaces a genuine need to
  edit `.claude/commands/role-triage.md`, stop and re-read the design — the key
  set was chosen so no wrapper edit is needed.
- **Delta-key names must match the wrapper verbatim** — `fleet_validate_roles.py`
  exact-matches bold keys (its alias map is empty). Its `find_wrappers` matches
  **whole-file**: any `role-*.md` with a `## Deltas` section that mentions the
  literal basename `triage-protocol.md` anywhere becomes a triage wrapper. Today
  only `role-triage.md` matches; keep the discovery pointer out of `role-*.md`.
- **Engine-public isolation:** the protocol section, script, comments, and tests
  must use "downstream repo" phrasing and fleet mechanics only — no downstream
  feature names or jargon anywhere. Staging files and the audit log stay under
  `~/.fleet/` by design.
- **Do not drift into #2494's scope** (scout untriaged projection, dispatcher
  trigger, `FLEET_TRIAGE` gate) or game #280's (the downstream wrapper file).
- The apply allowlist is deliberately narrow; resist adding
  `fleet:queued`/`fleet:task`/verdict labels — those are scout/ingest-owned.
- `fleet-decisions` is the style precedent (bash arg parsing + python-over-manifest,
  read-only default); keep `apply` the only writing path.
