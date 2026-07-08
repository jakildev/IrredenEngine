"""fleet_task_class.py — pick the model class + effort for a worker dispatch.

The dispatcher can't change a claude process's model or effort after launch,
so per-task model routing has to be resolved *before* the iteration starts.
This module reads a worker lane's projection slice (written every scout tick
to ~/.fleet/state/projections/<role>.json) and answers: which model class
should the next iteration for this lane launch with, and at what effort?

Resolution order mirrors the worker role docs' pickup priority:

  1. feedback PRs   — class from review severity: ``fleet:fable`` on the PR
                      routes the fix to fable (reviewer judged the approach
                      itself wrong; fable is opus+ so it also serves a
                      fable-tagged design-unblocked resume — checked first);
                      ``fleet:design-unblocked`` otherwise routes to opus
                      (tier-4 resume is opus+-only work); a blocking label
                      (``fleet:needs-fix`` / ``human:needs-fix`` /
                      ``human:blocker``) routes to opus; nits-only feedback
                      routes to sonnet.
  2. open tasks     — the oldest claimable task's class (slices are sorted
                      by issue number; ``model`` comes from the
                      fleet:fable/opus/sonnet labels via the scout).
  3. needs_plan     — a `fleet:sonnet`-tagged needs-plan issue is a MECHANICAL
                      task the sonnet lane light-plans and self-queues (see
                      `_plan_class`); every other needs-plan issue is
                      architect-tier design work that prefers the fable class
                      (the same reason the architect panes run fable), falling
                      back to opus when the fable cap is saturated so planning
                      never stalls behind a long fable implementation iteration.

Effort: the task's ``**Effort:**`` field when present (scout validates it),
else the class default below.

The fable concurrency cap is enforced here by *skipping* fable items when
the cap is reached — the lane then serves its next non-fable item instead
of idling. When the only work left is non-actionable — cap-blocked fable,
tasks already covered by an open implementation PR (``inflight_pr``, #1726),
a ``blocked`` task with no valid stackable base (the scout left off
``stackable_blocker_pr`` because the blocker has no open PR or only a
parked/conflicting one), or backend-specific tasks this host can't run
(``needs_gl_host`` on a Metal-only host, #1998) — the verdict is ``defer``
(keep the trigger, dispatch nothing) rather than an empty result, so the
dispatcher doesn't burn an iteration on a lane whose only work a fresh
worker would refuse.

Output protocol (one line on stdout, consumed by fleet-dispatcher):

  ``<class> <effort> <more> <count> <plan>``
                                  — dispatch this class; ``more`` is 1 when
                                  claimable items of a *different* class
                                  remain (keep the trigger so the next tick
                                  serves them), else 0; ``count`` is the number
                                  of claimable items OF THIS CLASS right now, so
                                  the dispatcher can cap its fan-out at one
                                  worker per claimable item instead of one per
                                  idle pane; ``plan`` is 1 when that count
                                  includes the class's needs-plan yield — the
                                  dispatcher then pre-claims a specific issue
                                  (via ``--plan-pick`` + ``fleet-claim
                                  planning-claim``) and hands the assignment to
                                  the dispatch, so planning dispatches never
                                  contend for the same issue (#2197).
  ``defer``                     — queue isn't empty but nothing is claimable
                                  right now (only cap-blocked fable, tasks with
                                  an open implementation PR, a `blocked` task
                                  with no valid stackable base, or GL-only tasks
                                  on a non-GL host). Keep the trigger; dispatch
                                  nothing.
  (empty)                       — nothing class-routable in the slice; the
                                  dispatcher falls back to the lane default
                                  (this path covers reservation resumes and
                                  a missing/stale slice).

A second CLI mode, ``--plan-pick <slice.json> <class> <fable-blocked 0|1>``,
prints ordered ``repo:number`` lines — the slice's ``needs_plan[]`` entries
(already human-gate-filtered by the scout, engine-first / oldest-first) whose
`_plan_class` matches ``<class>``. The dispatcher walks these attempting
``fleet-claim planning-claim`` until one is granted; that issue becomes the
dispatch's pre-claimed planning assignment (#2197).
"""

import json
import os
import platform
import sys

CLASS_DEFAULT_EFFORT = {"fable": "xhigh", "opus": "xhigh", "sonnet": "high"}

# Review-severity labels that make a feedback fix opus-class. Nits-only
# feedback (fleet:has-nits) stays sonnet.
FEEDBACK_BLOCKING_LABELS = {"human:needs-fix", "human:blocker", "fleet:needs-fix"}

# Hosts that can build/run/verify the OpenGL backend. macOS GL is 4.1 < the
# shaders' required 4.5, so a Metal-only host genuinely cannot do GL work; a
# `fleet:needs-gl-host` task (scout field `needs_gl_host`) is unclaimable there
# and dispatching to it is a guaranteed no-op (#1998).
GL_CAPABLE_HOSTS = {"linux", "windows"}


def _current_host():
    # Canonical host key (mac | linux | windows | unknown), mirroring
    # fleet-claim's host_from_uname/derive_host: the FLEET_TEST_HOST seam plus
    # the same uname mapping (MINGW*/MSYS*/CYGWIN*/Windows* all = windows).
    # Inlined rather than imported — module resolution across the scout's ~/bin
    # symlink vs the dispatcher's FLEET_LIB_DIR is fragile (#1750/#1578), and
    # fleet-claim's mapping is bash. Fail-closed: an unrecognized host is
    # "unknown" (treated as not GL-capable); real dispatch hosts are always
    # mac/linux/windows.
    override = os.environ.get("FLEET_TEST_HOST")
    if override:
        return override
    sysname = platform.system()
    if sysname == "Darwin":
        return "mac"
    if sysname == "Linux":
        return "linux"
    if sysname.startswith(("Windows", "MINGW", "MSYS", "CYGWIN")):
        return "windows"
    return "unknown"


def _host_incompatible(task, host):
    # A `needs_gl_host` task on a non-GL host (macOS/Metal, or unknown) can
    # never be built/run/verified here — terminally unclaimable, like inflight.
    return bool(task.get("needs_gl_host")) and host not in GL_CAPABLE_HOSTS


def _task_claimable(task, host):
    # `inflight_pr` (set by the scout) means an open PR already implements this
    # issue — parked on a design question or otherwise in flight — so a fresh
    # worker can't start it off the queue. Skipping it here stops the dispatcher
    # churning no-op iterations on a head-of-queue task every candidate refuses,
    # which was starving lower-class work queued behind it (#1726, the #1640 /
    # design-blocked PR #1700 incident). The `needs_gl_host` clause does the
    # same for backend-specific tasks a Metal-only host can't run (#1998).
    #
    # A `blocked` task is claimable ONLY as a stack on its blocker's PR. The
    # scout sets `stackable_blocker_pr` exactly when that blocker has a single
    # open, non-parked, non-conflicting PR to base on — the same gate the
    # worker's stackable tier would otherwise re-run live. So a blocked task
    # WITH the field is claimable (stack it) and a blocked task WITHOUT it has
    # no valid base: a fresh worker refuses it on sight, so it's terminally
    # unclaimable like an inflight task. Electing it here (rather than dropping
    # to the '' lane-default fallthrough on any blocked task) is what stops the
    # dispatcher firing no-op iterations whose only "work" is re-deriving the
    # not-stackable verdict the scout already reached.
    if task.get("owner") not in (None, "", "free"):
        return False
    if task.get("inflight_pr") or _host_incompatible(task, host):
        return False
    if task.get("blocked"):
        return bool(task.get("stackable_blocker_pr"))
    return True


def _terminally_unclaimable(task, host):
    """True when no dispatch can ever let a fresh worker claim this task as it
    stands: an open PR already implements it (`inflight_pr`), this host can't
    build/run/verify it (`needs_gl_host` on a non-GL host, #1998), or it's
    `blocked` with no valid stackable base (the scout omits
    `stackable_blocker_pr` when the blocker has no open PR or only a
    parked/conflicting one). Each clears only via a merge, a host change, or a
    design answer — never via dispatching a worker — so a queue of only these is
    a no-op to dispatch into."""
    return bool(
        task.get("inflight_pr")
        or _host_incompatible(task, host)
        or (task.get("blocked") and not task.get("stackable_blocker_pr"))
    )


def _only_unclaimable_tasks(slice_data, host):
    """True when tasks_open is non-empty and EVERY item is terminally
    unclaimable (see `_terminally_unclaimable`).

    Used to pick 'go quiet' (defer) over the lane-default dispatch fallthrough:
    in this shape a fresh worker can claim nothing, so a dispatch is a no-op.
    Requiring *all* items to be terminal keeps a mixed slice — a terminal head
    plus an owner-held or otherwise non-terminal task — on the '' fallthrough so
    reservation resumes and the like still get their dispatch. A genuinely
    stackable `blocked` task is NOT terminal (it carries `stackable_blocker_pr`)
    and is elected as a candidate above, so it never reaches this gate."""
    tasks = slice_data.get("tasks_open") or []
    return bool(tasks) and all(_terminally_unclaimable(t, host) for t in tasks)


def feedback_pr_class(labels):
    label_set = set(labels or [])
    # fleet:fable is checked first: fable is an opus+ class (role-worker.md
    # "opus+ = opus or fable") and so it handles tier-4 design-unblocked
    # resumes too. Checking it before fleet:design-unblocked keeps the rare
    # both-tagged PR (an architect unblocked a fable-tier task) on fable
    # rather than silently downgrading it to opus.
    if "fleet:fable" in label_set:
        return "fable"
    # Design-unblocked resume is opus+-only work (FLEET-FEEDBACK-HANDLING
    # tier 4: "Opus+ classes only; sonnet-class iterations skip this tier").
    # Routed to opus so the dispatched worker can serve tier 4: without this
    # clause the scout surfaces a design-unblocked PR as the top feedback item,
    # this resolver routes it to sonnet, and the dispatched sonnet worker
    # refuses tier 4 — a no-op every tick, with opus needs_plan starved behind
    # it (engine #1885).
    if "fleet:design-unblocked" in label_set:
        return "opus"
    if label_set & FEEDBACK_BLOCKING_LABELS:
        return "opus"
    return "sonnet"


def _plan_class(issue, fable_blocked):
    """Model class that should author this needs-plan issue's plan.

    A `fleet:sonnet`-tagged needs-plan issue is a MECHANICAL task the
    human/architect judged bounded enough for a lightweight plan — the sonnet
    lane authors a thin `## Plan` comment ("basically the issue itself") and
    self-queues on `fleet-plan-lint` pass (PLANNING-PROTOCOL.md §"Lightweight
    plan for mechanical (fleet:sonnet) tasks"), skipping BOTH the fable/opus
    planning pass and the opus plan-review pass. Stakes are a human signal —
    `fleet:sonnet` on a needs-plan issue IS that signal — so there is nothing
    to compute here; the label is authoritative.

    Everything else is architect-tier design planning: fable while the cap has
    headroom, degrading to opus when it is saturated (`fable_blocked`). The
    downgrade is pre-resolved here (never yields a cap-blocked `fable`) exactly
    as the prior single-class needs_plan election did.
    """
    if "fleet:sonnet" in (issue.get("labels") or []):
        return "sonnet"
    return "opus" if fable_blocked else "fable"


def _candidates(slice_data, lane_default, host, fable_blocked=False):
    """Yield (class, effort, kind) per actionable item, pickup-priority order.

    Feedback PRs come first (the worker fixes review feedback before new work),
    then unblocked open tasks, then stackable `blocked` tasks (a fallback tier,
    claimable only as a stack on the blocker's PR), then needs_plan. This
    mirrors the worker role docs so the *elected* class matches what the worker
    actually picks up, and one yield per item lets the caller count claimable
    work per class for the dispatcher's fan-out cap. ``kind`` is "plan" for a
    needs_plan yield and "work" for everything else — `resolve` uses it to flag
    the elected class's planning candidate so the dispatcher pre-claims a
    specific issue for the dispatch (#2197).

    needs_plan yields once PER PLANNING CLASS, not once per issue: one planning
    assignment per class per tick is a deliberate serialization — planning is
    not the throughput bottleneck — while parallelism across classes (a sonnet
    light-plan alongside a fable/opus heavy plan) is preserved. A
    `fleet:sonnet`-tagged (mechanical) needs-plan issue is a light plan the
    sonnet lane authors; everything else is architect-tier design planning
    (fable, or opus when the fable cap is saturated). The dispatcher turns the
    per-class yield into a single pre-claimed assignment (`--plan-pick` +
    `fleet-claim planning-claim` before launch), so same-class planning
    dispatches never contend for one issue. Iteration order is
    oldest-within-each-repo, engine-repo-first — not a true global sort by
    issue number — because `slice_worker()` in `fleet-state-scout` appends all
    of `repos.engine.needs_plan[]` before any of `repos.game.needs_plan[]`
    (same pre-existing composition order `tasks_open`/`feedback_prs` already
    inherit above). See `_plan_class` and PLANNING-PROTOCOL.md §"Lightweight
    plan for mechanical (fleet:sonnet) tasks".
    """
    def _class_effort(task):
        cls = (task.get("model") or lane_default).lower()
        if cls not in CLASS_DEFAULT_EFFORT:
            cls = lane_default
        return cls, task.get("effort") or CLASS_DEFAULT_EFFORT[cls]

    for pr in slice_data.get("feedback_prs", []) or []:
        cls = feedback_pr_class(pr.get("labels", []))
        yield cls, CLASS_DEFAULT_EFFORT[cls], "work"
    tasks = slice_data.get("tasks_open", []) or []
    for task in tasks:
        if _task_claimable(task, host) and not task.get("blocked"):
            yield (*_class_effort(task), "work")
    for task in tasks:
        if _task_claimable(task, host) and task.get("blocked"):
            yield (*_class_effort(task), "work")
    seen_plan_classes = set()
    for issue in slice_data.get("needs_plan") or []:
        pcls = _plan_class(issue, fable_blocked)
        if pcls in seen_plan_classes:
            continue
        seen_plan_classes.add(pcls)
        yield pcls, CLASS_DEFAULT_EFFORT[pcls], "plan"


def plan_pick(slice_data, cls, fable_blocked):
    """Ordered ``repo:number`` planning candidates for one class.

    The slice's ``needs_plan[]`` entries whose `_plan_class` routes to ``cls``,
    in slice order (engine-first, oldest-first within repo — the priority the
    per-class yield in `_candidates` elects from). The dispatcher iterates
    these attempting ``fleet-claim planning-claim`` until one is granted; a
    line held by a cross-host dispatcher or the architect just falls through
    to the next, so a lost race assigns the *next* issue instead of burning
    the dispatch (#2197).
    """
    picks = []
    for issue in slice_data.get("needs_plan") or []:
        if _plan_class(issue, fable_blocked) != cls:
            continue
        number = issue.get("number")
        if number is None:
            continue
        picks.append(f"{issue.get('repo') or 'engine'}:{number}")
    return picks


def resolve(slice_data, lane_default, fable_blocked, exclude=()):
    """Return 'cls effort more count plan', 'defer', or '' per the protocol.

    `exclude` is a set of classes the caller has already served-and-saturated
    this tick (the dispatcher's cross-class fan-out: when the elected class is
    fully cap-covered but another class is claimable, it re-resolves excluding
    the covered class to serve the next one instead of deferring the whole tick).
    An excluded class is dropped from consideration entirely — not chosen, not
    counted toward `more`. If excluding leaves nothing electable but a claimable
    (excluded) class existed, the verdict is `defer`, NOT '' — there is real work,
    just none the caller can serve right now, so it must not fall through to a
    lane-default dispatch.
    """
    if lane_default not in CLASS_DEFAULT_EFFORT:
        lane_default = "opus"
    exclude = set(exclude or ())
    host = _current_host()
    chosen = None
    skipped_fable = False
    excluded_any = False
    servable_classes = set()
    class_counts = {}
    plan_classes = set()
    for cls, effort, kind in _candidates(slice_data, lane_default, host,
                                         fable_blocked):
        if cls in exclude:
            excluded_any = True
            continue
        if cls == "fable" and fable_blocked:
            # Cap-blocked fable is not currently servable: don't let it
            # hold the trigger open via `more` (that would re-dispatch
            # every tick to no effect). When the in-flight fable
            # iteration finishes, its label changes re-fire the scout
            # projection, and the periodic worker safety re-arm covers
            # the quiescent-scout corner.
            skipped_fable = True
            continue
        servable_classes.add(cls)
        class_counts[cls] = class_counts.get(cls, 0) + 1
        if kind == "plan":
            plan_classes.add(cls)
        if chosen is None:
            chosen = (cls, effort)
    if chosen is not None:
        more = 1 if servable_classes - {chosen[0]} else 0
        # `count` = claimable items of the elected class right now. The
        # dispatcher caps its idle-pane fan-out to this so it never spins up
        # more workers of a class than there is work for them to claim (the
        # surplus would only iterate-and-exit, a no-op opus iteration is not
        # free). `more` still drives the *other-class* follow-up dispatch.
        # `plan` = that count includes the class's (single, per-class-deduped)
        # needs-plan yield — the dispatcher's cue to pre-claim an assignment.
        count = class_counts[chosen[0]]
        plan = 1 if chosen[0] in plan_classes else 0
        return f"{chosen[0]} {chosen[1]} {more} {count} {plan}"
    if skipped_fable or excluded_any:
        return "defer"
    # Nothing servable AND no cap-blocked fable. If the queue's only content is
    # tasks already implemented by an open PR (every tasks_open item carries
    # `inflight_pr`), a lane-default dispatch is a guaranteed no-op — a fresh
    # worker refuses every one on sight — so the lane goes quiet (defer) instead
    # of churning that no-op every tick (#1726). Any other shape (a claimable
    # task would have been chosen above; a plain `blocked` host-compatible task
    # may still be stackable; an owned task, an empty/missing slice) returns ''
    # so the dispatcher's lane-default fallthrough keeps covering the stackable
    # tier, reservation resumes, and missing slices.
    if _only_unclaimable_tasks(slice_data, host):
        return "defer"
    return ""


def _load_slice(slice_path):
    try:
        with open(slice_path) as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return None


def main(argv):
    if argv[1:2] == ["--plan-pick"]:
        # --plan-pick <slice.json> <class> <fable-blocked 0|1>: print the
        # ordered repo:number planning candidates for <class> (see plan_pick).
        if len(argv) != 5:
            print("usage: fleet_task_class.py --plan-pick <slice.json> "
                  "<class> <fable-blocked 0|1>", file=sys.stderr)
            return 2
        slice_data = _load_slice(argv[2])
        if slice_data is None:
            return 0  # empty output -> nothing to assign
        for line in plan_pick(slice_data, argv[3], argv[4] == "1"):
            print(line)
        return 0
    # Optional 4th arg: comma-separated classes to exclude (the dispatcher's
    # cross-class fan-out re-resolve). Absent -> exclude nothing.
    if len(argv) not in (4, 5):
        print("usage: fleet_task_class.py <slice.json> <lane-default> "
              "<fable-blocked 0|1> [exclude-classes]", file=sys.stderr)
        return 2
    slice_path, lane_default, fable_blocked = argv[1], argv[2], argv[3] == "1"
    exclude = [c for c in (argv[4].split(",") if len(argv) == 5 else []) if c]
    slice_data = _load_slice(slice_path)
    if slice_data is None:
        return 0  # empty output -> lane default
    out = resolve(slice_data, lane_default, fable_blocked, exclude)
    if out:
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
