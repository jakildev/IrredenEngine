"""The "GitHub never started this job" reading, shared by the scout, fleet-decisions and tests.

When GitHub refuses to start a job at the account level (billing, spending
limit) the job completes `failure` with no steps and a single failure-level
annotation. Both halves are required: a cancelled or queued job also has no
steps, and a job that ran steps is a real failure whatever it annotated.
"""

import re

_WHITESPACE_RE = re.compile(r"\s+")


def job_never_started(job):
    return job.get("conclusion") == "failure" and job.get("steps") == []


def not_started_text(annotations):
    """The first failure-level annotation's message, whitespace-collapsed and
    safe for a single-quoted alert field; None when no failure-level
    annotation carries one. A run's notice-level annotations ride alongside
    the failure one, so "first annotation" is never the key."""
    for annotation in annotations or ():
        if annotation.get("annotation_level") != "failure":
            continue
        text = _WHITESPACE_RE.sub(" ", str(annotation.get("message") or "")).strip()
        if text:
            # fleet_alerts.parse_fields ends a single-quoted value at the next quote.
            return text.replace("'", '"')
    return None


def run_not_started_text(jobs, read_annotations):
    """The annotation text when every job of a run was never started, else None.

    `read_annotations(job_id)` returns that job's annotation list, or None when
    it could not be read. Anything short of a fully readable run fails closed
    to None: a missing job list, an unreadable annotation response, or one job
    that ran or lacks the annotation. The text of the lowest job id is returned
    so a multi-job run renders deterministically.
    """
    if not jobs:
        return None
    texts = {}
    for job in jobs:
        if not job_never_started(job):
            return None
        annotations = read_annotations(job.get("id"))
        text = None if annotations is None else not_started_text(annotations)
        if text is None:
            return None
        texts[job.get("id")] = text
    return texts[min(texts)]
