## Plan: jitter_probe rotation gate — derive the excursion bar on the `--pivot-origin` pinned sweep (measurement + docs, zero code)

- **Issue:** #2606
- **Model:** opus
- **Date:** 2026-08-07
- **Supersedes:** both prior `## Plan` comments (06:27Z — executed; its Phase 0 refuted the default-focus premise, see the 07:15Z bail. 07:52Z — bounced 10:57Z: its Phase 1 rebuilt a flag `--pivot-origin` already ships, with a provably identical effective camera). Both stay as audit trail; this plan follows the 10:57Z review's re-plan directions 1–4.

### Scope

Land the two ACs PR #2922 could not: a rotation gate on which the per-axis excursion assertion **fires on the defect and passes healthy runs** (AC2), and docs making that check the primary rotation-gate assertion with a published per-zoom bar (AC3). The re-anchor is the existing composition **`--yaw-sweep --pivot-origin`** — `RotationPivotMode::ORIGIN` removes the pivot-orbit term entirely, today, with zero new code. This task is **measurement + two doc surfaces only**; no flag ships (10:57Z direction 4: a `--yaw-sweep-pinned-focus` alias would reproduce `--pivot-origin` bit-for-bit, and ORIGIN's "no pivot term at all" dominates an explicit focus's "term that cancels to zero").

Out of scope: the residual criterion / `--max-residual` bar (#2907), the default-focus probe's composite redness (#2907), the pivot derive itself (#2641/#2758/#2548), and any change under `tools/`, `scripts/`, or demo sources.

### Verified current state (2026-08-07, origin/master + PR #2922 branch)

- **The composition chain, each link verified against origin/master** (the 10:57Z review derived it; re-checked here rather than inherited):
  - `--pivot-origin` registered at `creations/demos/shape_debug/main.cpp:750`, read at `:817`, applied **once at setup** at `:898-902` → `IRRender::setRotationPivotMode(RotationPivotMode::ORIGIN)` plus the exact log string `RotationPivotMode: ORIGIN (--pivot-origin) — Z-yaw pivots about the world origin` (`:901`) — the ready-made per-run **arm-identity marker** (10:57Z direction 2; no new plumbing).
  - `applyShotCameraState` (`engine/video/src/auto_screenshot.cpp:28-46`) sets/clears the *focus* only, never the *mode* — the yaw-sweep shots' `hasPivotFocus_=false` clear is harmless under ORIGIN.
  - `getEffectiveCameraIso` (`engine/render/src/ir_render.cpp:46-49`) tests ORIGIN **before** the focus branch and returns `cameraIso` unmodified — no pivot term is ever computed.
  - `updateDefaultRotationPivotFocus` early-returns unless `mode == CAMERA_CENTER` with no explicit focus (`engine/render/src/render_manager.cpp:310`, guard at ~`:343` with the comment "ORIGIN mode ignores the focus entirely") — ORIGIN pays no depth readback.
- **Candidate-set closure, restated mechanism-free** (the axis the 10:57Z bounce corrected): the requirement is "remove the pivot *orbit*", and the candidate set is the `RotationPivotMode` enum — **exactly two members**, `ORIGIN = 0, CAMERA_CENTER = 1` (`engine/render/include/irreden/render/ir_render_types.hpp:840`) — plus the per-shot explicit focus (`pivotFocusWorld_`/`hasPivotFocus_`, #1921). ORIGIN is CLI-reachable today; explicit focus has no yaw-sweep CLI path and would merely cancel to the identical value (`cameraYawPivotOffset(cameraIso, vec3(0), yaw) == cameraIso` at every yaw, `ir_math.hpp:1106` — the 10:57Z identity). There is no third mechanism.
- **The fixture was designed for ORIGIN**: `--spin-shape` spawns ONE shape at `vec3(0.0f, 0.0f, 0.0f)` on the voxel, SDF, and "figure" paths alike (`main.cpp:2623-2662`), with the spawn comment "Under camera Z-yaw-about-origin the shape stays screen-centred". The composition pins **because** the fixture is origin-centred — the doc recipe must bind the two (see Gotchas).
- **Census, stated fully** (10:57Z non-blocking note absorbed): `git grep -- --yaw-sweep` code hits are shape_debug's own registration/comments, `creations/demos/fog_demo/main.cpp:637` (comment), and `tools/jitter_probe/main.cpp:26` (comment) — all inert; remaining hits are docs/skills/plan files. `git grep -- --pivot-origin` hits are shape_debug-only (registration `:750`, getFlag `:817`, apply+log `:898-902`, comments). No script drives either flag; the only jitter_probe stdout parser is `scripts/pivot-verify.py`, which parses `--stationary` output only. Composing the two flags cannot break any harness.
- **Tool base**: `--max-excursion-x/-y` and the `excursion=` print are **not** on origin/master (`git grep -e --max-excursion-x origin/master -- tools/jitter_probe/main.cpp` → no match); they ship on PR **#2922** (`claude/2606-jitter-probe-excursion`, `fleet:approved`, MERGEABLE, open), which also rewrote the two doc sections this plan edits further: `engine/render/CLAUDE.md:549-577` ("**do not put a bar on this probe yet** … re-derive the bar here from a post-#2641 capture") plus the `:942-950` historical-columns note, and `tools/jitter_probe/README.md:160-200` (flags section + 2026-08-07 note).
- **The "wait for #2641" path those texts close on is dead**: PR #2758 (Closes #2641, `fleet:approved`, open) rules the default derive's cap-entry orbit **inherent** — it gates the orbit's growth, it does not remove it. The default-focus probe can never carry an excursion bar; the pinned probe is the durable home.
- **Defect fixture unchanged**: `IR_PERAXIS_OVERFLOW_DISABLE` presence check at `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp:504`.

### Approach

**Phase 0 — base + composition premise probe (bail-gated).**
- Base: at branch time re-run the tool-base check above; if #2922 is unmerged, stack on `claude/2606-jitter-probe-excursion` (native stack). Never re-implement any tool piece.
- Composition probe (10:57Z direction 1 — the statically-proven identity still gets its cheap measured confirmation): **one** capture, H4-pinned — canonical recipe + `--pivot-origin` (wipe → `IRShapeDebug --spin-shape cylinder --spin-shape-voxel --yaw-sweep --pivot-origin --zoom 4 --auto-screenshot 6` → 6-digit glob, `--expect-frames 24`), ORIGIN log line asserted. **Expected reading**: x excursion in the pinned regime, ~1–3px (explicit-focus blocks pin at 0.94/1.27px re-verified live 2026-08-07; the pre-orbit 2026-07-28 table read 1.26px @ z4) — an order of magnitude under the 38.18px unpinned reading. **Bail**: if it lands in the tens-of-px regime *with the ORIGIN line asserted*, the composition premise is refuted — stop, post the measurement here, cross-comment #2641, park `fleet:needs-human`. Do not build the dependent phases.
- A Phase-0 capture taken under the full protocol (wipe, identity assert, archive) is retained as population H4.

**Phase 1 — populations + bar.** One host/backend, one session; archive each population (frames + engine log) before the next capture. Arms:
- **U4** — healthy voxel z4 **unpinned** (differential control; expect ~38px x excursion per the 08-07 table — proves the flag changes the regime).
- **H2/H4/H8** — healthy voxel, pinned (`--yaw-sweep --pivot-origin`), zoom 2/4/8 (H4 from Phase 0).
- **S4/S8** — SDF control, pinned (omit `--spin-shape-voxel`).
- **D2/D4/D8** — `IR_PERAXIS_OVERFLOW_DISABLE=1`, voxel, pinned.

Assert arm identity **per run from the engine log, never the argv**: zoom as received; the voxel-fixture log line present iff a voxel arm; the ORIGIN line present iff a pinned arm; the `Yaw-sweep: 24 shots` line present. Read x/y excursion from the tool's `excursion=` print; hand-derive H4 from `--verbose` centroids as the ±0.01px instrument cross-check.

**Bar rule (committed — carried verbatim from the vetted 06:27Z plan and its review):** per zoom, `bar(z)` = the smallest half-integer ≥ 2.5 × the max healthy x excursion at that zoom (voxel and SDF both count as healthy), and it must be ≤ 0.5 × the defect x excursion at that zoom. **Per-zoom bail** (06:37Z binding constraint 1): publish bars only for zooms that separate; a non-separating zoom is omitted from the doc table with a one-line note and its measurement posted here. Expected regime (context, never controls): healthy pinned ~1–3px, defect ~11px @ z4 (2026-07-28, measured before the orbit landed).

**Whole-task bail** only if **no** zoom separates: the premise is then refuted on the pinned family too — post the full table here, cross-comment #2641/#2907, park the issue `fleet:needs-human` recommending close-or-rescope. No PR ships (unlike the 07:15Z bail there is no tool piece left to salvage — #2922 already banked it). A second refutation is a human call, not a fourth plan.

**Phase 2 — acceptance runs** (same session, re-scoring the archived populations):
- **Positive fire + attribution:** each D arm at a separating zoom under the canonical score flags + `--max-excursion-x bar(z)` → exit 1; an isolation arm adds `--max-residual 99` → **still** exit 1, so the failure is attributable to the excursion criterion by construction (the exact "caught by accident" failure mode this issue documents).
- **Healthy pass:** H2/H4/H8 + S4/S8 print x excursion ≤ bar(z) — gated on the printed x-axis summary line, not the composite exit; if any healthy pinned arm is composite-red via the residual axis, record the residuals and cross-comment #2907 (pinned-probe residuals discriminate "orbit-inflated fit" from "true floor" — free evidence either way).
- **Differential:** pinned H4 x excursion ≤ 0.25 × unpinned U4 x excursion (expect ~3px vs ~38px; proves the flag was live and the orbit was the dominating term).

**Phase 3 — docs (AC3).**
- `engine/render/CLAUDE.md` §"Verifying temporal stability": the rotation-recipe line gains `--pivot-origin` (with a comment binding it to the origin-centred `--spin-shape` fixture); the scoring line gains `--max-excursion-x <bar(zoom)>`; the recipe's log-assert guidance gains the ORIGIN-line check; **replace** the `:549-577` "do not put a bar yet … post-#2641 capture" close: the orbit is inherent (#2758), the pinned `--pivot-origin` sweep is the canonical rotation gate, and the unpinned sweep keeps **no** excursion bar (one line: its excursion measures the inherent pivot orbit; that surface belongs to pivot-verify). Publish the per-zoom bar table (populations, x values, y excursions recorded, date, host/backend). Reword the `:942-950` historical note to point at the pinned table instead of "until a post-#2641 capture".
- `tools/jitter_probe/README.md`: the canonical rotation recipe (~`:187`) gains `--pivot-origin`; the "no separating value" / 2026-08-07 passage points at the CLAUDE.md bar table (single home for the numbers — no duplicated table to drift).
- Untouched: `--max-residual` / accepted-residual text (#2907's), pivot-verify docs, `camera-yaw-pivot.md` (#2758's).

### Affected files

- `engine/render/CLAUDE.md` — recipe + bar table + replace the wait-for-#2641 close
- `tools/jitter_probe/README.md` — pinned recipe line + table pointer
- `.fleet/plans/issue-2606.md` — replaced with this plan (first commit of the PR, #1932)

**No code changes.** No new flag, no demo edit, nothing under `tools/`/`scripts/`.

### Acceptance criteria

1. **Positive fire, attributable:** at every separating zoom the D arm exits 1 under `--max-excursion-x bar(z)` AND under the isolation pair (`+ --max-residual 99`). Fixture: `IR_PERAXIS_OVERFLOW_DISABLE` (exists on master).
2. Every healthy pinned population (H2/H4/H8, S4/S8) prints x excursion ≤ bar(z); the U4 differential shows ≥ 4× pinned-vs-unpinned separation.
3. Docs per Phase 3 carry the measured table (values, populations, date, host/backend); the published recipe is `--yaw-sweep --pivot-origin`; the unpinned probe explicitly keeps no bar.
4. **Zero code:** the PR diff touches only the two doc files plus `.fleet/plans/issue-2606.md`.
5. Every capture's arm identity is asserted from the engine log (zoom-as-received; voxel iff voxel arm; ORIGIN line iff pinned arm) and the run logs are archived with the populations; the PR body carries the full table including y excursions.

### Gotchas

- `IR_PERAXIS_OVERFLOW_DISABLE` is a `getenv != nullptr` **presence** check — `VAR=` (empty) counts as SET; fully `unset` it for healthy arms and verify with `env`.
- Wipe the screenshots dir before EVERY capture (exact `find … -delete` form in the recipe); `--expect-frames 24` (`--auto-screenshot N` is per-shot warmup, not the shot count); archive each population immediately after its capture.
- zsh does not word-split an unquoted `$spec` (the 07:15Z pass's live precedent) — assert every arm from the engine log, never from the argv you think you passed.
- jitter_probe silently drops frames under ~50 foreground px (per-frame centroid validity floor) and aborts under 3 valid frames — confirm 24/24 valid frames per run. Under ORIGIN the origin-centred shape stays screen-centred so this should hold; a sudden valid-frame drop means the arm is not what you think it is.
- `--pivot-origin` is applied once at demo setup and covers every shot of the run — never mix pinned and unpinned populations in one run.
- `fleet-run --timeout` goes BEFORE the target token; redirect `fleet-build` output to a file and check `$?` — piping through `grep` masks a failed build, and a stale binary then "passes".
- If #2922 merges mid-task, rebase normally; the Phase-0 base check is branch-time-only. Never re-implement the tool pieces.

### Reconciliation (siblings / in-flight)

- **PR #2922** (`fleet:approved`, open, MERGEABLE) — the base (stack while unmerged); ships the AC1/AC4/AC5 tool surface plus the doc text Phase 3 edits further. This plan is the residual AC2+AC3.
- **PR #2758 / #2641** (`fleet:approved`, open, needs-gl-host) — supplies the orbit-is-inherent premise. With no demo edit here, the shared-`main.cpp` seam the 07:52Z plan reconciled is gone (10:57Z note). Even if the owed GL-host leg overturned the inherent ruling, the ORIGIN-pinned gate stays valid — no pivot term is stricter than any future default derive.
- **#2907** (open, needs-plan; residual axis, default-focus probe) — criterion untouched here; receives pinned-probe residual readings via cross-comment, which its re-planner can use to judge whether the default-probe redness is orbit-inflated.
- **PR #2850 / #2479** (`fleet:wip`) — same render domain, measured digit-identical to master on this probe (#2907's ruled-out section); no interaction.
- **#2469 / #2605** — the eps history stays; #2922 already rewrote the limitation text; Phase 3 only replaces its wait-for-#2641 close.

---

Not high-stakes per the PLANNING-PROTOCOL step-3 checklist: single committed approach (zero-code, directed by the 10:57Z review), two doc files, one PR, no public-contract change — `fleet:plan-review` only, no `human:review-plan`.

