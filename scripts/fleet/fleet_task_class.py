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
  3. needs_plan     — planning runs the opus class.

Effort: the task's ``**Effort:**`` field when present (scout validates it),
else the class default below.

The fable concurrency cap is enforced here by *skipping* fable items when
the cap is reached — the lane then serves its next non-fable item instead
of idling. When the only work left is non-actionable — cap-blocked fable,
tasks already covered by an open implementation PR (``inflight_pr``, #1726),
or backend-specific tasks this host can't run (``needs_gl_host`` on a
Metal-only host, #1998) — the verdict is ``defer`` (keep the trigger,
dispatch nothing) rather than an empty result, so the dispatcher doesn't
burn an iteration on a lane whose only work a fresh worker would refuse.

Output protocol (one line on stdout, consumed by fleet-dispatcher):

  ``<class> <effort> <more>``  — dispatch this class; ``more`` is 1 when
                                  claimable items of a *different* class
                                  remain (keep the trigger so the next tick
                                  serves them), else 0.
  ``defer``                     — queue isn't empty but nothing is claimable
                                  right now (only cap-blocked fable, tasks with
                                  an open implementation PR, or GL-only tasks on
                                  a non-GL host). Keep the trigger; dispatch
                                  nothing.
  (empty)                       — nothing class-routable in the slice; the
                                  dispatcher falls back to the lane default
                                  (this path covers reservation resumes and
                                  a missing/stale slice).
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
    return (
        task.get("owner") in (None, "", "free")
        and not task.get("blocked")
        and not task.get("inflight_pr")
        and not _host_incompatible(task, host)
    )


def _only_unclaimable_tasks(slice_data, host):
    """True when tasks_open is non-empty and EVERY item is non-actionable for a
    *terminal* reason — an open PR already implements it (`inflight_pr`) or this
    host can't build/run/verify it (`needs_gl_host` on a non-GL host, #1998).

    Used to pick 'go quiet' (defer) over the lane-default dispatch fallthrough:
    in this shape a fresh worker can claim nothing, so a dispatch is a no-op.
    Requiring *all* items to be terminally-unclaimable keeps a mixed slice (a
    terminal head plus a stackable `blocked` but host-compatible task behind it)
    on the '' fallthrough so the worker's stackable tier still gets its dispatch.
    """
    tasks = slice_data.get("tasks_open") or []
    return bool(tasks) and all(
        t.get("inflight_pr") or _host_incompatible(t, host) for t in tasks
    )


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


def _candidates(slice_data, lane_default, host):
    """Yield (class, effort) for each actionable item, pickup-priority order.

    Feedback PRs come first regardless of count — mirrors the worker role
    docs, which fix review feedback before claiming new queue work.
    """
    for pr in slice_data.get("feedback_prs", []) or []:
        cls = feedback_pr_class(pr.get("labels", []))
        yield cls, CLASS_DEFAULT_EFFORT[cls]
    for task in slice_data.get("tasks_open", []) or []:
        if not _task_claimable(task, host):
            continue
        cls = (task.get("model") or lane_default).lower()
        if cls not in CLASS_DEFAULT_EFFORT:
            cls = lane_default
        effort = task.get("effort") or CLASS_DEFAULT_EFFORT[cls]
        yield cls, effort
    if slice_data.get("needs_plan"):
        yield "opus", CLASS_DEFAULT_EFFORT["opus"]


def resolve(slice_data, lane_default, fable_blocked):
    """Return 'cls effort more', 'defer', or '' per the output protocol."""
    if lane_default not in CLASS_DEFAULT_EFFORT:
        lane_default = "opus"
    host = _current_host()
    chosen = None
    skipped_fable = False
    servable_classes = set()
    for cls, effort in _candidates(slice_data, lane_default, host):
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
        if chosen is None:
            chosen = (cls, effort)
    if chosen is not None:
        more = 1 if servable_classes - {chosen[0]} else 0
        return f"{chosen[0]} {chosen[1]} {more}"
    if skipped_fable:
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


def main(argv):
    if len(argv) != 4:
        print("usage: fleet_task_class.py <slice.json> <lane-default> <fable-blocked 0|1>",
              file=sys.stderr)
        return 2
    slice_path, lane_default, fable_blocked = argv[1], argv[2], argv[3] == "1"
    try:
        with open(slice_path) as f:
            slice_data = json.load(f)
    except (OSError, json.JSONDecodeError):
        return 0  # empty output -> lane default
    out = resolve(slice_data, lane_default, fable_blocked)
    if out:
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
