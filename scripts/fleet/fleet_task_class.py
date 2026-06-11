"""fleet_task_class.py — pick the model class + effort for a worker dispatch.

The dispatcher can't change a claude process's model or effort after launch,
so per-task model routing has to be resolved *before* the iteration starts.
This module reads a worker lane's projection slice (written every scout tick
to ~/.fleet/state/projections/<role>.json) and answers: which model class
should the next iteration for this lane launch with, and at what effort?

Resolution order mirrors the worker role docs' pickup priority:

  1. feedback PRs   — class from review severity: ``fleet:fable`` on the PR
                      routes the fix to fable (reviewer judged the approach
                      itself wrong); a blocking label (``fleet:needs-fix`` /
                      ``human:needs-fix`` / ``human:blocker``) routes to
                      opus; nits-only feedback routes to sonnet.
  2. open tasks     — the oldest claimable task's class (slices are sorted
                      by issue number; ``model`` comes from the
                      fleet:fable/opus/sonnet labels via the scout).
  3. needs_plan     — planning runs the opus class.

Effort: the task's ``**Effort:**`` field when present (scout validates it),
else the class default below.

The fable concurrency cap is enforced here by *skipping* fable items when
the cap is reached — the lane then serves its next non-fable item instead
of idling. When fable items were skipped and nothing else is actionable,
the verdict is ``defer`` (keep the trigger, dispatch nothing) rather than
an empty result, so the dispatcher doesn't burn an iteration on a lane
whose only work is gate-refused.

Output protocol (one line on stdout, consumed by fleet-dispatcher):

  ``<class> <effort> <more>``  — dispatch this class; ``more`` is 1 when
                                  claimable items of a *different* class
                                  remain (keep the trigger so the next tick
                                  serves them), else 0.
  ``defer``                     — only cap-blocked fable work right now.
  (empty)                       — nothing class-routable in the slice; the
                                  dispatcher falls back to the lane default
                                  (this path covers reservation resumes and
                                  a missing/stale slice).
"""

import json
import sys

CLASS_DEFAULT_EFFORT = {"fable": "xhigh", "opus": "xhigh", "sonnet": "high"}

# Review-severity labels that make a feedback fix opus-class. Nits-only
# feedback (fleet:has-nits) stays sonnet.
FEEDBACK_BLOCKING_LABELS = {"human:needs-fix", "human:blocker", "fleet:needs-fix"}


def _task_claimable(task):
    return task.get("owner") in (None, "", "free") and not task.get("blocked")


def feedback_pr_class(labels):
    label_set = set(labels or [])
    if "fleet:fable" in label_set:
        return "fable"
    if label_set & FEEDBACK_BLOCKING_LABELS:
        return "opus"
    return "sonnet"


def _candidates(slice_data, lane_default):
    """Yield (class, effort) for each actionable item, pickup-priority order.

    Feedback PRs come first regardless of count — mirrors the worker role
    docs, which fix review feedback before claiming new queue work.
    """
    for pr in slice_data.get("feedback_prs", []) or []:
        cls = feedback_pr_class(pr.get("labels", []))
        yield cls, CLASS_DEFAULT_EFFORT[cls]
    for task in slice_data.get("tasks_open", []) or []:
        if not _task_claimable(task):
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
    chosen = None
    skipped_fable = False
    servable_classes = set()
    for cls, effort in _candidates(slice_data, lane_default):
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
    if chosen is None:
        return "defer" if skipped_fable else ""
    more = 1 if servable_classes - {chosen[0]} else 0
    return f"{chosen[0]} {chosen[1]} {more}"


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
