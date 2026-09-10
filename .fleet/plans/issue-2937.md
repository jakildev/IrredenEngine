# Plan: repository-scoped host-local fleet plan staging

## Outcome

An engine and game plan with the same issue number occupy distinct host-local
paths, and no fleet reader treats a legacy flat path as authoritative.

## Locked decisions

- Stage plans under `~/.fleet/plans/<repo>/issue-<N>.md`, where `<repo>` is
  `engine` or `game`.
- Keep repo-side `.fleet/plans/` unchanged; it is already scoped by repository.
- Make the ingest planning gate ignore flat staged files instead of guessing.
- Migrate legacy files with a dry-run-by-default CLI. Resolve unique issue
  numbers directly, use a plan-title match only for cross-repo collisions, and
  leave every inconclusive file unmoved.
- Treat an existing scoped destination as a conflict; never overwrite it from
  a legacy flat file.
- Never auto-migrate from `fleet-up`; report legacy files until a human runs
  the apply mode.
- Leave gated role/skill-wrapper edits for a human and list them in the PR.

## Constraints

- `T-*.md` files are never auto-moved.
- Migration issue lookups are one-shot CLI work, never part of an every-tick
  path.
- The ingest fast path remains network-free and retains its committed-plan API
  fallback.
- Engine artifacts must not disclose private downstream details.

## Acceptance criteria

- Unit coverage proves engine and game issue 310 have distinct paths and that
  a flat issue-310 path has no repository identity.
- The ingest harness proves a game issue cannot use an engine-scoped plan, a
  game-scoped plan works, and a flat legacy file is inert.
- CLI coverage exercises path output, dry-run and apply migration, ambiguous
  and `T-*` no-guess behavior, destination conflicts, idempotence, and clean
  checks. `check` ignores manual-only `T-*` files but remains nonzero while a
  flat `issue-*` file still needs migration or manual placement.
- Install completeness, the full fleet suite, and ruff pass.
- Living documentation uses the scoped path or `fleet-plans path`; explicit
  legacy warnings are the only remaining flat-path references.

## Affected files

- `scripts/fleet/fleet_plans.py`, `scripts/fleet/fleet-plans`
- `scripts/fleet/fleet-queue-ingest`, `scripts/fleet/fleet-up`
- `scripts/fleet/install.sh`, `scripts/fleet/fleet-help`
- Fleet plan and ingest tests
- Worker-editable plan-path documentation

## Gotchas

- Same-number test assertions must include the repository slug.
- A flat legacy population contains files from both repositories.
- Migration must fail closed when title matching does not identify exactly one
  repository.
