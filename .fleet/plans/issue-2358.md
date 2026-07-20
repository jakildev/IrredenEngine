## Plan: extract shared verify-harness helpers (scripts/verify_common.py)

- **Issue:** #2358
- **Model:** sonnet — mechanical dedup with concrete acceptance; the drift
  decisions below remove the only judgment calls, so implementation is bounded.
- **Date:** 2026-07-20

### Scope

Create `scripts/verify_common.py` owning the six helpers duplicated across the
render-family harnesses, plus the multi-pass diff-dir routing (`_run_pass`) that
the human's recurrence note (#2356) flags as bug-causing. Migrate
`cull-verify.py`, `render-verify.py`, `gui-verify.py`, and `light-verify.py` to
import them and delete the local copies. CLI/behavior of each harness is
preserved; this is a correctness-preserving dedup, not a feature change.

### Verified current state (measured against the actual code, 2026-07-20)

Signature drift is real and asymmetric — the extraction must pick the **superset
/ hardened** form, not the first copy. Confirmed by inspection:

| helper | canonical (adopt this) | drifted copies to delete |
|---|---|---|
| `run` | `render`'s `_run(cmd, cwd=None, check=True, env=None, timeout=None) -> int` (line 76) | `cull`/`light` `(cmd,cwd,check)`; `gui` `(cmd)` — all subsets |
| `run_capture` | `light`'s streaming `_run_capture(cmd, cwd=None, timeout=None) -> tuple[int,str]` (line 108) | `gui` `(cmd)` subset |
| `detect_worktree_root` | `(start) -> Path` — byte-identical in cull/light/render, no drift | cull/light/render copies |
| `detect_backend` | the `_detect_backend(build_dir) -> str` form **with the CMakeCache.txt guard** (cull:103 / render:92, identical) | `light`'s `_detect_backend()` (line 134) — no arg, **no CMakeCache guard**; migrate light to pass its build_dir |
| `find_exe` | `render`'s hardened `_find_exe(build_dir, target_name, demo_name)` (line 116) — `os.access(X_OK)` filter + non-existent-`search_root` skip + demo-subtree scoping | `cull` `(build_dir, demo_name)`; `light` `(build_dir, target)` — the exact copy #2356's nit flagged as missing X_OK + search_root skip |
| `compare` | `compare(actual, reference, diff_out, thresholds=None) -> dict` — cull/light `_compare` bodies are byte-identical (wrap `render-compare.py --json`); add the `thresholds` param so it matches render's `_compare_shot` shape (thresholds passed in, not a module dict) | cull/light `_compare` copies |

**Exclude (do NOT move):** `render-verify.py`'s `_run_capture` at line 498 is a
**different function that happens to share the name** — a keyword-only
`--auto-screenshot` demo-capture runner (`*, worktree, target, shots_dir,
warmup, timeout, demo_args, pass_label`), not the generic subprocess wrapper.
Leave it in `render-verify.py`.

### Approach

Phase 0 (premise check — cheap, ~2 min): re-grep the six `def _<helper>` sites
to confirm the line numbers/signatures above still hold before editing (they can
drift if another PR touched a harness). If any canonical form has changed, adopt
the still-most-complete copy — the rule is superset-wins, not line-number-wins.

1. **Create `scripts/verify_common.py`** exporting (public names, no leading
   underscore since they cross a module boundary now): `run`, `run_capture`,
   `detect_worktree_root`, `detect_backend`, `find_exe`, `compare`, and
   `run_pass`. Module-level constant `RENDER_COMPARE = Path(__file__).parent /
   "render-compare.py"` so `compare` locates the comparator relative to the
   shared module. Use the canonical bodies from the table.
2. **`run_pass` + diff-dir routing** (the correctness core, per the human's
   #2356 note): lift `light-verify.py`'s `_run_pass` (line 262) and make the
   per-pass diff PNGs route to a **sibling of `shots_dir`**
   (`shots_dir.parent / "<name>_diffs"`), cleared **once up front** — never
   nested under `shots_dir`, which each pass `rmtree`s on entry. This is the
   byte-for-byte re-derivation of render-verify.py's already-shipped fix, so the
   next harness in this family cannot re-hit the silent-diff-wipe footgun.
3. **Migrate each harness** — replace the local `def _helper` with
   `from verify_common import ...` (scripts run as `__main__` from the same
   `scripts/` dir; a top-level `import verify_common` resolves because CWD/script
   dir is on `sys.path`). Update call sites for the signature changes:
   - `gui-verify.py`: `_run`/`_run_capture` → `run`/`run_capture` (drop the
     minimal locals).
   - `cull-verify.py`: `_find_exe(build_dir, demo)` → `find_exe(build_dir,
     target_name, demo)` — pass the demo target name it already knows;
     `_detect_backend`/`_compare`/`_detect_worktree_root`/`_run` → shared.
   - `light-verify.py`: `_detect_backend()` → `detect_backend(build_dir)` (thread
     its build_dir in); `_find_exe(build_dir, target)` → `find_exe(build_dir,
     target, demo)`; `_run_pass` → `run_pass` with the sibling diff-dir; `_run`/
     `_run_capture`/`_compare`/`_detect_worktree_root` → shared.
   - `render-verify.py`: `_run`/`_detect_worktree_root`/`_detect_backend`/
     `_find_exe` → shared; keep its own `_run_capture` (line 498) and
     `_compare_shot`; if `_compare_shot` can now call the shared `compare` with a
     thresholds dict, do so, else leave it.
4. **Delete** every migrated local copy so no drifted duplicate survives.

### Affected files

- `scripts/verify_common.py` — **new**; the six helpers + `run_pass`/diff-dir
  routing + `RENDER_COMPARE` constant.
- `scripts/cull-verify.py` — import shared helpers; delete `_run`,
  `_detect_worktree_root`, `_detect_backend`, `_find_exe`, `_compare`; adapt
  `find_exe` call to the 3-arg signature.
- `scripts/render-verify.py` — import `run`/`detect_worktree_root`/
  `detect_backend`/`find_exe`; delete those locals; keep `_run_capture` (498)
  and `_compare_shot`.
- `scripts/gui-verify.py` — import `run`/`run_capture`; delete both locals.
- `scripts/light-verify.py` — import all six + `run_pass`; delete locals; thread
  `build_dir` into `detect_backend`; adopt hardened `find_exe`; route diffs to
  the sibling dir via shared `run_pass`.

### Acceptance criteria

- `scripts/verify_common.py` exists and exports the seven names above.
- All four harnesses import from it; **zero** local copies of the six helpers /
  `_run_pass` remain (grep `def _run\b|def _find_exe\b|def _detect_backend\b|def
  _compare\b|def _run_pass\b` across the four returns only render's domain-
  specific `_run_capture`/`_compare_shot`).
- **Positive-fire check:** `light-verify.py`'s per-pass diff PNGs from a ≥2-pass
  run all survive to `shots_dir.parent/<name>_diffs/` (count of surviving diff
  dirs == number of passes, > 1) — the exact regression #2356 fixed, now proven
  at the shared layer.
- `ruff check scripts/` stays green (verifiable on any host; `ruff` is on PATH).
- Spot-check: one run of **each** of the four harnesses passes on a host that
  has its reference images (build + refs required; `light-verify.py` is ~20 min —
  grep its raw DOMAIN-STATE stdout rather than the image compare where possible).

### Gotchas

- **`run_capture` name collision:** render-verify.py already has an unrelated
  `_run_capture` (demo-capture, line 498). Do not import the shared `run_capture`
  into render-verify or rename render's — they are different functions. Leaving
  render's local wins on name resolution anyway, but do not delete it.
- **`detect_backend` drift is semantic, not cosmetic:** light's copy lacks the
  CMakeCache guard. Adopting the guarded form means light now fails loudly if no
  configure has run — intended, but confirm light always has a real `build_dir`
  at the call site before threading it in.
- **Import path:** the scripts are executed directly (`./scripts/foo-verify.py`),
  not as a package. `import verify_common` works because the script's own dir is
  on `sys.path[0]`; do **not** add a package `__init__.py` or a relative import.
- **CLI is frozen:** no harness's argparse surface or exit codes may change —
  callers (skills, CI) invoke them as subprocesses.
- **Out of scope (do not expand):** `scripts/depth-tier-verify.py` is a 5th
  harness in the family sharing 2 of these helpers. Migrating it is a natural
  follow-up but is **not** in this ticket's acceptance — leave it, or file a
  follow-up per TASK-FILING.md. Do not silently broaden the diff.

### One task or subtasks

**One task.** Single new module + four in-place migrations on the same surface;
splitting would only add merge friction. Model stays **sonnet** — the drift
decisions above are pre-made, leaving mechanical execution.

— worker (opus planning pass)
