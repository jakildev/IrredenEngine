## Plan: tooling/fleet — ruff lint over scripts/fleet/

- **Issue:** #2047
- **Model:** opus
- **Date:** 2026-06-27

### Verified current state (premise check)
- **No Python lint exists anywhere** — confirmed: no `ruff` on PATH, no
  `.pre-commit-config.yaml`, no `.githooks/`, no active git hooks, and no root
  `pyproject.toml` / `ruff.toml` / `setup.cfg` / `.flake8`. The issue's "Python
  gets nothing" premise holds.
- **There is no `pre-commit` *framework* to hook into.** "Mirror how C++ gets
  clang-format + simplify" is the right instinct, but the C++ quality surface is
  *not* a git pre-commit hook. It is three things:
  1. CMake custom targets `format` / `format-check` / `format-changed` defined in
     `cmake/ir_quality_tools.cmake`, exposed as `*-format-check` / `*-lint`
     presets in `CMakePresets.json`.
  2. The **CI gate** `.github/workflows/quality.yml` runs `windows-format-check`
     + `windows-lint` on every `pull_request` — this is the authoritative,
     merge-affecting half.
  3. The **author-side** `commit-and-push` flow runs `fleet-build --target
     format-changed` + the `simplify` skill before the PR is opened — the
     "fix before review" half.
  So a faithful mirror = a **CI lint step** (the bite) + an **author-side
  documented command** (the pre-review fix), not a new hook framework.
- **Scope reality:** `scripts/fleet/` holds ~40 Python files (30 `*.py` + 10
  python-shebang executables like `fleet-state-scout`, `fleet-dispatcher`,
  `fleet-claim`) plus `scripts/fleet/tests/` (~23 `*.py`). A first `ruff check`
  *will* surface a backlog, so the implementation PR must bring the tree clean,
  not just add the gate (else CI is red on master immediately).
- **Out of scope (note, do not lint in v1):** `scripts/*.py` outside fleet —
  ~13 render/perf harnesses (`render-verify.py`, `gui-verify.py`,
  `render_metric_util.py`, `scripts/perf/compare_perf_runs.py`, …). The issue
  scopes explicitly to `scripts/fleet/`; widening now would balloon the
  first-fix. File a v2 follow-up to extend the same `ruff.toml` to `scripts/`.
- **Honesty on the comment policy:** ruff has **no native "comments must be one
  line" rule.** ruff covers PEP8 (`E`/`W`), pyflakes (`F`), import-order (`I`),
  and bare-`assert`-as-guard (`S101`, which retroactively covers the rejected
  #1868 class). The engine one-line-comment policy is a *custom* convention; do
  **not** promise mechanical enforcement of it in v1. Assert it in docs only.

### Approach (single, picked)
One PR. Order:

1. **Add `ruff.toml` at repo root.** Pin a conservative initial rule set so the
   first-fix is bounded and the gate is stable:
   - `target-version` = the repo's Python floor (`py310` is safe; bump if a
     module already uses newer syntax).
   - `[lint] select = ["E", "F", "I", "S101"]` — pycodestyle errors, pyflakes,
     isort (import-order, the nit the issue calls out), bare-assert.
   - `line-length = 88` (ruff default; do not import the C++ 80-col rule).
   - `extend-exclude = ["scripts/fleet/__pycache__", "scripts/fleet/completions"]`.
   - Scope via `[lint.per-file-ignores]` only if a class of files needs it;
     prefer fixing over ignoring.
   The canonical invocation is **`ruff check scripts/fleet/`** (the config pins
   rules; the path argument pins the v1 scope).
2. **Clean the existing tree.** Run `ruff check --fix scripts/fleet/` for the
   autofixable set (imports, spacing), then **hand-fix** the residue with
   judgment — never blind-suppress. Resolve `fleet-state-scout:56-77` (the
   issue's cited occurrence: function-between-imports `E402`, missing blanks
   `E30x`, import order `I001`) so it passes; that doubles as an acceptance
   check. Keep this diff reviewable — if the backlog is large, the *lint fixes*
   are mechanical but must be a clearly separated, self-evident part of the PR.
3. **Wire the CI gate** — add a `Python lint` step to the existing
   `quality-checks` job in `.github/workflows/quality.yml`, mirroring the
   `Format check` / `Lint` steps. Install ruff in that step
   (`pipx install ruff` or `astral-sh/ruff-action`) and run
   `ruff check scripts/fleet/`. This is the merge-affecting half.
4. **Make the tool available locally** — add ruff to the three bootstraps so
   workers can run it pre-commit, mirroring clang-format:
   `scripts/bootstrap_linux.sh` (apt has no ruff → `pipx install ruff` or
   `python3 -m pip install ruff`), `scripts/bootstrap_macos.sh`
   (`brew install ruff`), `scripts/fleet/setup-windows.sh`
   (`pacman -S` has `mingw-w64-x86_64-python-ruff`, else `pipx`).
5. **Document the author-side step (NOT in the gated SKILL.md).** Add the
   one-liner "run `ruff check scripts/fleet/` before committing fleet-script
   changes" to `docs/agents/BUILD.md` (alongside the `format-changed` note) and
   the shared flow `docs/agents/skills/commit-and-push.md`. Do **NOT** edit
   `.claude/skills/commit-and-push/SKILL.md` — it is gated self-config a worker
   cannot apply (see Gotchas).
6. **Assert the policy for Python (docs only)** — one line in
   `docs/agents/CLAUDE-BASELINE.md` stating the one-line-comment / WHY-not-
   narration policy applies to Python too, with the caveat that ruff enforces
   PEP8 + import-order mechanically and the comment policy is reviewer-enforced.

### Affected files
- `ruff.toml` (new) — rule set, scope, line length, excludes.
- `.github/workflows/quality.yml` — new `Python lint` step in `quality-checks`.
- `scripts/bootstrap_linux.sh`, `scripts/bootstrap_macos.sh`,
  `scripts/fleet/setup-windows.sh` — install ruff.
- `docs/agents/BUILD.md`, `docs/agents/skills/commit-and-push.md` — author-side
  command.
- `docs/agents/CLAUDE-BASELINE.md` — Python comment-policy assertion.
- `scripts/fleet/**` (and `scripts/fleet/tests/**` if `select` covers them) —
  lint fixes to make the gate green on master.

### Acceptance criteria
- `ruff check scripts/fleet/` exits 0 on the PR branch.
- The CI `Python lint` step is present and green on the PR.
- `ruff check` flags the pre-fix `fleet-state-scout:56-77` occurrence (verify by
  checking out the pre-fix lines), and the PR fixes it.
- ruff is installed by all three bootstrap scripts (or a clear `pipx` fallback
  is documented where no native package exists).
- No `# noqa` blanket-suppressions introduced to dodge real violations.

### Gotchas
- **Gated self-config:** `.claude/skills/commit-and-push/SKILL.md` and
  `.claude/commands/role-*.md` are deterministically gated — a fleet worker
  **cannot** edit them. Keep all author-side wiring in the non-gated docs
  (`BUILD.md`, `docs/agents/skills/commit-and-push.md`). If the team later wants
  the SKILL.md itself to call ruff, that is a separate human-applied change.
- **Red-on-merge risk:** the CI step and the tree-clean fixes must land in the
  **same** PR, or `quality.yml` goes red on master the moment the step merges.
- **Shebang executables:** many fleet scripts are Python with no `.py`
  extension (`fleet-claim`, `fleet-dispatcher`, …). `ruff check scripts/fleet/`
  picks `.py` files by default; pass the directory and rely on ruff's shebang
  detection, or explicitly include them — verify the chosen invocation actually
  lints the extension-less executables (the largest, most-edited files).
- **Don't blind `--fix`** the side-effecting scripts: review autofixes to
  import blocks in files with import-time side effects / conditional imports.
- **Scope creep:** resist linting `scripts/*.py` (render/perf) in this PR; the
  first-fix is already non-trivial. v2 follow-up extends the config.

### Decomposition + model
One PR, **`[opus]`** — judgment in rule-set selection, the comment-policy
stance, and fixing (not suppressing) the existing backlog; touches the CI gate
+ 3 bootstraps. Not a stack.

### Follow-up to file after this lands (per TASK-FILING.md, no labels)
- Extend `ruff.toml` scope from `scripts/fleet/` to all of `scripts/` (render /
  perf / gui harnesses), with its own bounded lint-fix pass.

