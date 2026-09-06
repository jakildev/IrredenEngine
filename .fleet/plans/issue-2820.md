## Plan: fleet — narrow the needs-gl-host claim gate with a backend-symmetric discriminator

- **Issue:** #2820
- **Model:** opus
- **Date:** 2026-08-09
- **Source:** revision 2, posted as the `## Plan` comment on #2820 and cleared by plan-review 2026-08-09T02:25:17Z.

---

## Plan

Revision 2, addressing the 2026-08-01 plan-review bounce. Everything the review
endorsed (phase-0 measurement gate, positive control, opposite-direction lock,
#2805 live-carrier caution) is kept; both gaps are now decided in-plan, and the
8-day decay of the bounce's own inputs is re-measured below.

### Decay audit — what changed since rev-1 (measured 2026-08-09 on origin/master)

- **PR #2709 MERGED 2026-08-04; #2704 closed.** Rev-1's "stack on #2709"
  directive is retired — there is nothing to stack on. The opposite-direction
  lock now pins **master's shipped inference**: `_body_requires_gl_host`
  (`fleet-state-scout:765`) + `_GL_ONLY_SOURCE_PATH_RE` (`:758`), locked by
  `scripts/fleet/tests/test_scout_gl_host_backstop.py`.
- **All rev-1 line cites drifted.** Re-measured: `fleet-claim`
  `check_host_capability()` `:882-927` (premise comment `:883-897`, label match
  `:913-916`, refusal `:924`), `cmd_claim` call site `:1081-1085`,
  `cmd_amending_claim` guard `:1736-1745`; scout `needs_gl_host` projection
  `:900-909`; `fleet_task_class.py` `GL_CAPABLE_HOSTS` `:116`, `GL_HOST_LABEL`
  `:120`, `_host_incompatible` `:193-205`, `_task_claimable` `:207`,
  `_terminally_unclaimable` `:235`, feedback-PR dimension `:362-373`;
  `fleet-labels` registry line `:142`; `fleet-state-machine.json` node `:258`
  (scope `"issue+pr"`); `fleet-labels-reference.md:276-296`. Re-measure again at
  pickup; treat rev-1's numbers as dead.
- **#2816 (live carrier) re-verified**: still OPEN, still
  `fleet:queued + fleet:opus + fleet:needs-gl-host + fleet:agent-approved +
  fleet:no-plan`. Phase 0's repro target stands.
- **Nothing shipped any part of this** — `gh label list` still has no
  `fleet:backend-symmetric`, no counter-gate, and the predicate is unchanged.
  Not scope-shipped.

### Committed approach (core unchanged from rev-1)

Split the conflated bit: `fleet:needs-gl-host` keeps meaning *"needs GL runtime
verification somewhere"* (inference untouched — #2704/#2709's surface is not
edited); a new discriminator `fleet:backend-symmetric` means *"the task also has
a Metal-verifiable half"*. The gate refuses only when the first holds and the
second does not.

### Gap 1 decided — the narrowing is issue-claims-only; the PR path deliberately does not narrow

This is a semantic commitment, not scope-trimming, and it comes from the
label's own canonical definition (`fleet-labels-reference.md:279-283`): on an
**issue** the label marks *the whole task* — exactly the framing this issue
shows is imprecise; on a **PR** it marks *the remaining work* (#2524 — "a
design-unblocked resume whose architect reply leaves only GL gate runs"). Task
symmetry is an axis of the *task*; the PR label is a claim about the
*residual*, stamped by whoever knows what's left. A backend-symmetric task
whose PR carries `fleet:needs-gl-host` is being told its residual is GL-only —
refusing that on a Metal host stays correct. Narrowing the PR path on a
task-axis discriminator would be a category error, and would also re-inflate
the feedback-election phantom counts #2696 fixed.

Consequences, per the bounce's three sub-points:

1. **Field reach**: the scout stamps `backend_symmetric` on TASK records only.
   PR records stay raw-labels-only (no new PR field).
   `_host_incompatible` (`fleet_task_class.py:193-205`) consults the
   discriminator **via the task field only — never via PR labels**. Its
   comment's forward-compat line ("stays correct if the scout later stamps the
   field on PRs", `:197-201`) is rewritten to state the asymmetry is
   deliberate (task-axis vs residual-axis), citing this issue.
2. **T7/T8 stay untouched and green** — they keep pinning the whole-PR
   refusal. A **new test pins the asymmetry itself**: fixture 3003 = PR
   carrying `fleet:needs-gl-host` **and** `fleet:backend-symmetric`, mac →
   `amending-claim` still exit 1. Without this, the "inconsistency" invites a
   future hand-fix that silently reopens #2696.
3. **No gated edit exists in the diff.** `role-worker.md:268-271` and
   `FLEET-FEEDBACK-HANDLING.md:60-66` state the PR-side skip rule — the PR
   path does not change, so both stay accurate as written. The rev-1 claim
   "worker-pushable" now holds soundly.

Mechanically: `check_host_capability()` gains the narrowing only in
issue-claim context — `cmd_claim` passes a context flag; `cmd_amending_claim`'s
existing call (`:1742`) stays flag-less and keeps today's whole-PR behavior.

### Gap 2 decided — minting and stamping

**Mint** via the standard three-way label sync (#1998 precedent), one commit,
`fleet-labels --check` green:

- `scripts/fleet/fleet-labels` (beside `:142`):
  `"fleet:backend-symmetric|c5def5|Task fixes both .glsl and .metal twins; Metal hosts may claim despite fleet:needs-gl-host"`
  (89 chars, cap is 100 — verified).
- `docs/agents/fleet-state-machine.json` (beside `:258`): node with
  `"scope": "issue"` — deliberately **not** `"issue+pr"`; the scope field is
  where the Gap-1 decision is encoded in the vocabulary.
- `docs/agents/fleet-labels-reference.md` — entry beside the
  `fleet:needs-gl-host` entry (`:276`), including the residual-routing sentence
  (below).

**Stamping surface, named**: `docs/agents/architect-protocol.md:568-576` — the
same step that already instructs adding `fleet:needs-gl-host` gets the paired
instruction: when filing/triaging a shader defect that must land in both a
`.glsl` and its `.metal` twin, add `fleet:backend-symmetric` alongside it.
That file is `docs/agents/` — not gated. (#2816's 32-seconds-later hand-applied
timeline shows filing-time stamping is the live workflow; this is where the
instruction lands.)

**Backstop** (so the primary doesn't degrade to prose-only — rev-1's own
requirement): the scout infers `backend_symmetric` when the body cites **both**
a real `.glsl` filename and a real `.metal` filename — new
`_body_backend_symmetric()` sibling beside `_body_requires_gl_host`
(`fleet-state-scout:765`), same real-filename discipline as
`_GL_ONLY_SOURCE_PATH_RE`. The two inferences are **structurally disjoint**:
the GL-only regex deliberately excludes `.glsl` and `src/metal/` (scout's own
comment block — shader fixes are macOS-verifiable), so no body can make them
fight. Polarity note: a false positive here degrades to
claimed-then-smoke-caught, a false negative degrades to today's starvation —
still precision-first (require both real filenames), but the stakes are lower
than the #1969/#2704 direction.

### New consumer both rev-1 and its review missed: the dispatcher's safety re-arm

`fleet-dispatcher:2180-2191` re-arms the worker lane by **delegating** to
`resolve_worker_class` (its comment records that the previous inline predicate
drifted on exactly this field — "excluded inflight_pr but not needs_gl_host",
the #1969 idle-churn). Because it delegates, narrowing `_host_incompatible`
fixes re-arm automatically. Instruction: **do not hand-patch the dispatcher**;
instead extend `test_dispatcher_rearm.sh` (`:39` writes a `needs_gl_host: true`
slice) with a symmetric-task case asserting the re-arm now fires on a mac
host. Without that case, the claim gate could open while the wake path stays
shut — the starvation this issue reports, relocated one layer down and
invisible.

### New churn hazard rev-1 missed: label-vs-body agreement flips polarity

For `needs_gl_host`, body inference is dispatch-side only and `fleet-claim`
matches labels only — safe polarity (dispatch stricter than claim). The
discriminator **flips** it: body-inferred `backend_symmetric` makes *dispatch*
more permissive, so a label-less symmetric-bodied task would dispatch and then
be refused at claim — the exact claim→refuse→release churn the gate exists to
prevent (#1998). Fix: `check_host_capability()` honors the **same** body
backstop. `fetch_issue_info` (`fleet-claim:675-703`) already returns the body
(base64) in the same fetch — decode it and apply the same
both-real-filenames test in bash. The bash and python implementations of one
predicate are a drift pair (#2727 class): Phase 2 pins **both layers against
the same fixture body text**.

### Phase 0 — measurement gate (kept from rev-1, one addendum)

Do not assume the Metal half is verifiable; measure it once on a macOS pane:
build the Metal render target and run the #2816 repro shape (non-cardinal yaw
+ fractionally positioned content), capturing a rotation screenshot. **If this
fails, stop and report** — the premise collapses and the remaining phases are
void.

Addendum (new since rev-1): master's `shape_debug` macos-debug render-verify
references were stale (#2943); the refresh PR #2945 is approved but unmerged
at plan time. If Phase 0 uses render-verify for the repro, re-derive #2945's
merge state first — a red compare from stale committed refs is not a Phase-0
failure.

### Phase 1 — mint + predicate

1. Mint `fleet:backend-symmetric` (three-file sync above; run `fleet-labels`
   to create it on GitHub; `--check` green).
2. Scout: `backend_symmetric` field on task records (beside `:908`):
   `"fleet:backend-symmetric" in labels or _body_backend_symmetric(body)`.
   Task records only.
3. `fleet_task_class.py`: add `BACKEND_SYMMETRIC_LABEL` and
   `METAL_CAPABLE_HOSTS = {"mac"}` beside `GL_CAPABLE_HOSTS` (`:116`); narrow
   `_host_incompatible` via the task field only: a `needs_gl_host` item passes
   iff `host in GL_CAPABLE_HOSTS`, or (`host in METAL_CAPABLE_HOSTS` and
   `backend_symmetric`). Unknown hosts still refuse — the discriminator opens
   only the mac door (rev-1 Gotcha 2, now executed as a test).
4. `fleet-claim`: `check_host_capability` gains issue-context narrowing (label
   or decoded-body backstop, mac only); `cmd_amending_claim` call unchanged.
5. Comment/description sync in the same commits (the bounce's AC-5 fold-in —
   all surfaces stating the whole-task conclusion): premise comment
   `fleet-claim:883-897`; `fleet-labels:142` description →
   `"Remaining work needs an OpenGL-4.5 host (Linux/Windows); Metal panes skip unless backend-symmetric"`
   (98 chars — verified); `fleet-state-machine.json:258-263` description
   likewise; `fleet-labels-reference.md:276-296` entry;
   `_host_incompatible` comment rewrite; `architect-protocol.md` stamping
   paragraph.

### Phase 2 — executed acceptance tests (extend existing suites; no new parallel suite)

Extend `test_fleet_claim_host_gate.sh` (stub-`gh` + `FLEET_TEST_HOST` harness,
fixtures 2001-2003 / 3001-3002):

- **T9** — fixture 2004: `fleet:needs-gl-host` + `fleet:backend-symmetric`
  labels, mac → claim exit 0 (AC-1 claimable direction).
- **T10** — fixture 2005: gl-host label, body cites `x.glsl` **and**
  `x.metal`, no discriminator label, mac → claim exit 0 (bash body backstop;
  kills the polarity churn above).
- **T11** — fixture 2006: gl-host label, body cites only a GL-only source
  path (the #2704 shape), mac → claim exit 1 (opposite-direction lock at the
  claim layer).
- **T12** — fixture 3003: PR with gl-host + backend-symmetric labels, mac →
  `amending-claim` exit 1 (pins the deliberate PR-path asymmetry — Gap 1.2).
- **T13** — fixture 2004, `FLEET_TEST_HOST=freebsd` → claim exit 1
  (fail-closed unknown host survives the narrowing).
- T1/T4/T5/T7/T8 must pass **unmodified** — GL-only refusal, fail-closed,
  opt-in, and both PR-path directions.

Extend `test_fleet_task_class.py` (`needs_gl_host` kwarg exists at `:74`):
symmetric task claimable on mac / GL-only not / unknown host refuses despite
symmetric / feedback-PR dimension ignores a backend-symmetric PR label.
Extend `test_scout_gl_host_backstop.py`: `_body_backend_symmetric` positive
and negative bodies, **including the same body text T10 uses** (the bash/python
drift pin), and the #2704-shape body still projecting `needs_gl_host=true`
with `backend_symmetric=false` (AC-2, executed).
Extend `test_dispatcher_rearm.sh`: symmetric-task slice on mac → re-arm fires;
existing gl-only case stays no-fire.
Extend `test_worker_projection.py`: `backend_symmetric` present on the labeled
task row.

**Positive control (kept verbatim in spirit — load-bearing, not ceremony):**
run T9/T10 against the **pre-change** `fleet-claim` (checkout of master's copy)
and show both refused there; paste the run in the PR body. This change makes a
gate refuse less — a green suite on the fixed tree proves nothing without the
red run at base.

### Residual routing (AC-3) — existing machinery; verified, no new code

Smoke labels are stamped by the **reviewer after the verdict**, keyed on
`fleet:authored-on-<host>` (`FLEET-CROSS-HOST-SMOKE.md:39`, `:101`:
authored-on-macos → add `fleet:needs-windows-smoke`). A backend-symmetric task
is a render/shader task by construction, so its PR enters exactly that path —
the #2475 / #2812 / #2659 / #2624 rows are this mechanism already live. Plan
work: the `fleet-labels-reference.md` entry states the routing ("GL runtime
residual rides the reviewer-stamped `fleet:needs-{linux,windows}-smoke` lane")
so the pair stays linked at the vocabulary layer. AC-3's executed half is
reviewer-protocol behavior, not this diff's code; it discharges via the doc
cross-ref plus the first live instance (#2816's eventual PR).

### In-flight reconciliation (re-derived 2026-08-09)

#2709 merged — nothing to stack on. Open approved fleet PRs overlapping the
touched scripts, all in unrelated regions (measured via
`git diff --name-only` per branch): #2961 (`fleet-claim` — empty-exit backoff
marker), #2964 / #2967 / #2953 / #2978 (`fleet-state-scout` — wake
suppression, degraded skip, smoke label sets). None touch
`check_host_capability`, `_host_incompatible`, or the task projection dict.
Plain rebase awareness; no stack. Re-derive at pickup.

### Out of scope

- The GL 4.1-vs-4.5 premise and any Metal-host GL verification. Unchanged.
- Hand-editing #2816's labels (live carrier only; re-derive at pickup, #2805).
- A `fleet:needs-metal-host` counter-gate (the scout's comment records it as a
  separate design call; still true).
- PR-path narrowing — now a **decided non-goal with its own pinning test**
  (T12), not an open fork.
- `_body_requires_gl_host` / `_GL_ONLY_SOURCE_PATH_RE` — #2704/#2709's
  surface; the new inference is a sibling, not an edit.

### Affected files (all ungated — verified against the gated set)

- `scripts/fleet/fleet-claim`
- `scripts/fleet/fleet-state-scout`
- `scripts/fleet/fleet_task_class.py`
- `scripts/fleet/fleet-labels`
- `docs/agents/fleet-state-machine.json`
- `docs/agents/fleet-labels-reference.md`
- `docs/agents/architect-protocol.md`
- `scripts/fleet/tests/`: `test_fleet_claim_host_gate.sh`,
  `test_fleet_task_class.py`, `test_scout_gl_host_backstop.py`,
  `test_dispatcher_rearm.sh`, `test_worker_projection.py`

No `.claude/` file is in the diff: `role-worker.md:268-271` stays accurate
because the PR path does not change (Gap 1.3).

### Gotchas

1. **Three layers stay in lock-step, two by construction**: dispatch + re-arm
   share `fleet_task_class.py` (delegation), and claim + amending-claim share
   `check_host_capability` (the context flag is the only divergence point —
   keep it a single boolean, not a second predicate).
2. **Fail-closed unknown host**: the discriminator opens only the mac door
   (`METAL_CAPABLE_HOSTS`), executed as T13 — not a comment.
3. **The bash/python body-backstop pair is a #2727-class drift risk** — pinned
   by sharing one fixture body across T10 and the scout test.
4. **Positive control is load-bearing** (see Phase 2).
5. **`fleet-labels` descriptions are capped at 100 chars**, enforced by
   `--check`; both proposed strings are pre-measured (89, 98).
6. The smoke-servable dispatch path shares `_host_incompatible`'s layer;
   field-only narrowing leaves PR-shaped smoke records untouched — verify with
   the existing smoke-projection tests, change nothing there.

**Model:** opus

