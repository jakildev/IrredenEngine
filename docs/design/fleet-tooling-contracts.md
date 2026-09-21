# Fleet tooling contracts — rationale

Why the rules in [`scripts/fleet/CLAUDE.md`](../../scripts/fleet/CLAUDE.md)
are shaped the way they are. The rules themselves live there; the runtime
protocol is [`FLEET-RUNTIME.md`](../agents/FLEET-RUNTIME.md), the shared
cache is [`FLEET-CACHE.md`](../agents/FLEET-CACHE.md), and the validator
index is [`VALIDATION.md`](../agents/VALIDATION.md). This document holds
only rationale that is still true of the code and not readable from it.

## Hermetic tests fail closed

A fleet suite's verdict must be a function of the fixture, never of
production state. A mock that falls through to `urllib` or `gh` on a miss
keeps the suite green while it reaches live GitHub — and, worse, writes the
production scout's ETag cache from a test. The same silent transition happens
when a function that every suite covered network-free acquires its first
network call: nothing fails, the suites just start certifying whatever the
live repo says that day. That is why the seam is a raise, why `cache_dir` is
injected, and why fixtures should look synthetic: a plausible real issue ID
in a fixture hides the tell that the suite is no longer hermetic.

A `gh` stub that matches a substring of `"$@"` has the same failure mode one
layer down. It accepts flags the real binary rejects, so the suite certifies
a call that has never once succeeded in production; with the real call's
stderr going to `/dev/null`, a whole sweep can be disabled for weeks with
every covering suite green. Modelling the accepted flag set, and pinning it
with a fidelity assertion, is what makes the stub a test of the call rather
than of the string.

## Skips, tallies, and the vacuous pass

`run_all.sh` folds every `exit 0` into "N passed". A guard shaped
`if [[ ! -f "$SUBJECT" ]]; then echo SKIP; exit 0; fi` therefore reports a
suite that verified nothing as a pass — the exact failure mode wiring the
suites into CI was meant to remove, one level down. Exit 3 is a distinct
status the runner tallies as "skipped" so the omission is visible. The
`SKIP:` prefix is reserved for the missing-subject case because a suite that
skips for a missing environment dependency (no `git` on the host) is not
hiding a subject change.

The summary line is a closed grammar for the same reason. `fleet-positive-
control` reads each suite's own tally to decide MEANINGFUL versus VACUOUS; a
bespoke spelling (`echo "PASS: $PASS  FAIL: $FAIL"`) made roughly half the
suites un-controllable, and a local `summarize()` redefinition could print
anything while passing a population check. Seven forms are accepted
(`summarize()`'s two plus five legacy spellings), anchored at both ends so
`run_all.sh`'s own `… N passed, M failed, K skipped` line is excluded, and
`--parse-tally` is the single executor so the grammar has one copy. A tally
the grammar rejects and a suite that printed nothing are reported with
different wording under the same exit status; the tests assert on each
arm's wording and the absence of the other's.

## What a positive control can and cannot grade

Green CI proves a suite passes; only a run against the pre-fix ref proves it
would have gone red on the bug. Hand-staging that control is unreliable
because the wrappers dispatch to the `fleet_*.py` modules beside them: a
partial stage aborts every invocation on the lib-dir preflight, the suite
scores those aborts as ordinary assertion failures, and the result is a
plausible but wrong tally rather than an error. `fleet-positive-control`
stages the whole directory and dispatches on the file extension exactly as
`run_all.sh` does, because neither suite type carries a shebang.
`lib_preflight.sh` is the backstop for controls still run by hand, and its
adoption is ratcheted tree-wide rather than remembered: a guard scoped to
the file it lives in left more than half the hazard-bearing suites
uncovered.

The one axis the control cannot grade is an exclusion assertion — a check
that something is deliberately *not* matched. The verdict is a whole-suite
aggregate, and an exclusion usually predates the fix, so it passes against
the pre-fix ref too and MEANINGFUL came from the other arms. The load-bearing
proof is mutation of the current implementation: delete the exclusion and
watch the assertion go red. Same-fixture reachability (a known violation
beside the exempt declaration, or a coverage count that moves) is cheaper but
complementary — it proves the subject was reached, not that the assertion
bites. Before/after drift counts have the same blind spot: an input rejected
at the entry guard agrees on both revisions vacuously, which is why coverage
is reported beside the drift count.

## Wall clocks and windows

A test arm that derives anything from `now` has a meaning that depends on the
date the suite runs. Widening the window when it goes red removes the red
without restoring the assertion: the arm then passes for every input and no
longer discriminates. An injected clock pinned from the fixture's own
timestamps, paired with a window that excludes the fixture and asserts the
reported boundary, is the form a real clock reproduces for free.

## Workflow path filters are two ratchets on one axis each

`fleet-tests.yml` is the only thing that executes the fleet suites, so a
suite whose subject lives outside `scripts/**` runs only if that path is in
the workflow's `paths:` filter — and GitHub Actions has no YAML anchors, so
the `push:` and `pull_request:` blocks are hand-duplicated and drift
independently. `OUT_OF_TREE_SUBJECTS` in
`tests/test_fleet_tests_workflow_paths.sh` is that one workflow's own
subject-domain list; a subject absent from it is a subject nothing guards,
however green the suite runs, which is why `fleet_test_subjects.py` derives
the subject population from the suite sources themselves and fails on any
member the list omits. Whether a workflow's two blocks *agree* is a
different axis, and `tests/test_workflow_paths_sync.sh` checks it for every
workflow that declares both blocks, deriving the population from a glob
rather than a list. Those workflows live outside `scripts/fleet/**`, so they
are themselves out-of-tree subjects; the sync suite asserts directly off
the glob that each is in `fleet-tests.yml`'s filter, because the derived
side is the one a hand-maintained list cannot follow. The filter's first
glob is `scripts/**` rather than `scripts/fleet/**` because
`lint_python_registry.py` derives its population from the wider root; the
overlap with `render-harness-tests.yml`'s `scripts/*.py` is deliberate.

## Claim lanes and force-pushes

Disjoint claim-label namespaces (`fleet:amending-*`, `fleet:resolving-*`,
`fleet:reviewing-*`) look like a mutex and are not one: a PR that matches
two lanes' filters on one scout tick gets two panes force-pushing the same
head branch, last writer wins. The hazard has three properties that each
produced its own incident. It is **directional** — closing one side leaves
the race live, and both lanes are woken by the same labels by design, so a
one-sided guard reads as complete. It is **per lane pair** — a guard between
two lanes says nothing about a third that force-pushes. And a pre-acquire
GET is a TOCTOU on its own — the exclusion is a mutex only when arbitration
runs on the POST response over the lane union with a symmetric table. Hence
the rule that every force-pushing claim lane routes through the shared
table and that a new guard is enumerated against every other pair.

The merger force-pushes too and is outside the table by design: it takes no
`fleet-claim` lock and uses `--force-with-lease` as its concurrency control.
A mid-review mechanical rebase is an accepted design point, not an
oversight the rule covers.

A lane's admission filter must also be its sole one. An emit-site filter
that the lane's `slice_<role>` twin does not share leaves every other
consumer of the slice — class election, the quiet check, `fleet-up`'s
bootstrap trigger — reading the unfiltered set.

## Unattended loops: log the skip, escalate the warning, keep the edge

A `continue` that withholds a worker-visible affordance (a stackable offer, a
claim candidate, a queue row) produces a symptom a whole pipeline away from
its cause; the `log()` line is the only thing that connects them.

A warning in an every-tick loop persists until a human acts, so a plain
`echo >&2` re-emits identically forever — spam that hides the outage instead
of reporting it (a parked main clone once froze every claim on both repos
behind one line repeated per minute). Counting consecutive identical skips,
escalating once at N, and going quiet is the fix; N is sized against the
outage being caught. The alert file is rewritten on every tick past N, not
just at N, because once stderr is quiet the file is the only standing
signal: a write-once alert freezes its `count=` and age at the escalation
instant, and lets a human triaging the inbox silence a still-live condition
permanently, since nothing would ever recreate it. Counter and alert writes
are best-effort so a read-only `$HOME` cannot break the path being guarded.
The two reference implementations differ only in where the streak lives:
in-process for a loop (`fleet-clone-freshness.sh`), on disk for a one-shot
the dispatcher re-invokes (`fleet-rebase`, where an age ceiling does the
sizing and N is 1).

`fleet-up` is itself a file in the clone it advances, read incrementally by
the bash executing it, so the advance leaves the rest of the boot running
the pre-merge script. Snapshotting the script and exec'ing the copy would
make the boot deterministic but still run the old code; the contract is
instead that everything past the advance runs from the merged tree:
`fleet_up_reexec_if_stale` hashes the script and the helpers it sourced
(`fleet_surface_hash`) across the advance and `exec`s the merged script once
when the hash moved, with `FLEET_UP_REEXEC=1` marking a second pass that
never re-execs again — the one-shot sibling of the daemons' self-reload in
[`FLEET-CACHE.md`](../agents/FLEET-CACHE.md) §"Daemon source staleness",
with no debounce, probe, or cap because the boot runs once and the sentinel
bounds it. Two consequences bind edits to `fleet-up`: the head above the
advance runs twice on a stale boot, so it must stay idempotent — a second
pass over the same conf, argument list, and usage cache has to land on the
same state; and the re-exec runs under the launch environment, not the first
pass's, for the same reason the dispatcher's reload does — every knob the
head resolves is exported under its own name, and `fleet_env_override_names`
would report an inherited copy to the dispatcher as an operator pin that
shadows a later conf edit.

The scout's `queue-manager` and `queue-manager-ingest` lanes compare their
own projection hash inline instead of routing through `update_role_trigger`,
because nothing re-arms them. Recording the hash and then skipping — or
recording it and then failing to spawn — discards the change permanently:
the next tick compares equal and skips too. The write therefore clears the
whole fallible region; "put it below the guards" is the special case. A
multi-command lane needs an explicit partial-failure rule (`queue-manager`
is all-or-none, safe because every sweep it fires is idempotent), and because an
unwritten hash makes the lane retry every tick, its failure path needs the
escalate-then-quiet pair or it becomes exactly the per-tick spam the
previous paragraph forbids.

## Ingest candidates come from above the filters

The remove-half of a `fleet-queue-ingest` round-trip needs the population
its predicate targets. `fetch_task_queue` drops `fleet:plan-review`,
`fleet:needs-human` and `fleet:gated` before the task dict is built, then
splits what survives into `open` versus `in_progress` by claim state, so a
candidate derived from `tasks.open` is blind to three label populations and
to every claimed issue. This is the same reachability class as the defect
such a round-trip usually exists to fix — the input-set builder filtering
the target before the detector runs — which is why the candidate list is
re-derived from the raw issue list rather than reusing a section that looks
close enough, and why the test asserts the row is in the candidate list
*and* absent from every section: the former alone passes with the capture
moved back below a `continue`.

## The bootstrap heredoc

`fleet-up`'s bootstrap block is a standalone `python3 - <<'PY'` with no path
to `FLEET_LIB_DIR`, run under `|| true`. An `ImportError` from sys.path
plumbing is swallowed silently and takes the bootstrap trigger down for every
role at once, which is strictly worse than whatever the import was buying.
Inlining a constant or predicate is the accepted cost; the drift guard that
lifts the heredoc's definitions and asserts they still agree is the whole
price of that duplicate, and a copy with no guard is not allowed. Bash
reaches the Python modules the other way, through CLI arms
(`fleet_task_class.py --pick`, `fleet_completion.py assess`): the dispatcher
fetches, the module decides on files it is handed.

## Shared clones and the ff-only guard

`git merge --ff-only` refuses only when the incoming commits overlap the
dirty files; a disjoint dirty tree fast-forwards silently, which on a shared
clone is WIP-loss-adjacent. The guard is an explicit tracked-dirty check
before the fetch, on every branch arm — an on-master arm that fell through
to an unguarded advance is how the rule acquired its "every arm" clause. The
general form: an unattended mutation of shared state gates on liveness, not
on recoverability.

## Windows CRLF and byte-level guards

Native `jq` and native `python3` on MSYS2 CRLF-terminate stdout. `mapfile`
and `read` strip only the trailing `\n`, so a `\r` rides the last field of
every line into path lookups (`$CLAIMS_DIR/_prlabel-<tag>-<agent>\r` never
matches the real liveness marker, so a live claim reads as an orphan and is
swept) and into `gh … --remove-label "$label"`. Stripping at the producer
rather than the first comparison site matters because the captured value is
usually reused downstream. The guard must be byte-level because both GNU
grep on MSYS2 (which strips CRs from text input before matching) and
`$(...)` (which trims a trailing CR with the newline) read clean on the very
host that has the bug; a two-line fixture asserted on its first line with a
binary read is the shape that survives.

## `state.json` shape changes

`fetch_prs` serves a 304 fast path out of the on-disk `state.json`. A change
to the per-PR record shape that does not bump `PR_RECORD_SCHEMA` keeps
serving the pre-change shape after the deploy for an unbounded number of
ticks on a quiet repo, because nothing invalidates the cache until the
upstream data changes.
