"""fleet_completion.py — the completion contract on a dispatch target.

Every live dispatch of a target-bound role is launched after ONE pre-claimed
item (fleet-dispatcher assign_for_pane). When the pane returns to its shell
the dispatcher asks: did the iteration discharge that item? The answer is
read off the target's own state — its labels and comments on GitHub — never
off the pane's behavior (wall-clock, a marker file), because those cannot
tell "worked it" from "read the body and walked away":

  declined   the iteration posted a `declined:` comment from THIS host+agent
             after it was dispatched (``fleet-claim decline`` writes it) — an
             honest exit; the dispatcher remembers the item off this verdict
  abandoned  the lane's claim label for this host+agent still stands, and the
             iteration left no record of why. When the caller supplies the
             repo's open PRs (the kinds whose label legitimately rides a PR's
             review/merge lifecycle), an open PR referencing the item means
             the label is riding it, not abandoned
  finished   otherwise — the claim was released, or a task's PR is open

The decision is a pure function of what it is handed (``assess``): the
dispatcher composes the claim label to look for from fleet-common.sh's
FLEET_TARGET_LABEL and fetches the JSON, so this module holds no kind
vocabulary, and the same verdicts are unit-testable against fixtures. The
CLI prints ``<verdict>\\t<updated_at>\\t<detail>`` — the issue's post-release
``updated_at`` rides along because the dispatcher stamps the decline memory
with it. Grammar of the decline line, read past code spans like the claim
grammar it mirrors:

    declined: <role>/<class> @<host>-<agent> <reason>
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
from fleet_branch_match import issue_pr_state  # noqa: E402
from fleet_poll_topology import parse_generated_at  # noqa: E402
from fleet_scope_shipped import strip_code_spans  # noqa: E402

VERDICT_FINISHED = "finished"
VERDICT_DECLINED = "declined"
VERDICT_ABANDONED = "abandoned"

# `declined:` at the start of a line (optionally bold), then who, then the
# `@<host>-<agent>` the claim was taken under, then free text. Anchored to a
# line start so a quoted grammar inside prose never counts; code spans are
# stripped before matching for the same reason.
DECLINE_RE = re.compile(
    r"^[ \t]*(?:\*\*)?declined:\**[ \t]*(?P<who>\S+)[ \t]+@(?P<agent>\S+)[ \t]*(?P<reason>.*)$",
    re.MULTILINE,
)


def declined_since(comments, host_agent, since_epoch):
    """The reason of a `declined:` comment from `@<host_agent>` created at or
    after `since_epoch`, else None. Comments are `{createdAt|created_at, body}`
    records (either GitHub spelling)."""
    for comment in comments or []:
        created = comment.get("createdAt") or comment.get("created_at") or ""
        if (parse_generated_at(created) or 0) < since_epoch:
            continue
        match = DECLINE_RE.search(strip_code_spans(comment.get("body") or ""))
        if match and match.group("agent") == host_agent:
            return match.group("reason").strip() or "no reason given"
    return None


def assess(claim_label, labels, comments, host_agent, since_epoch,
           open_prs=None, number=None, repo="engine"):
    """(verdict, detail) for one target at exit. `claim_label` is the lane
    claim the dispatcher took for this pane (`<prefix><host>-<agent>`),
    `labels` the target's current label names, `comments` its comments since
    dispatch, `open_prs` the repo's open PRs when the label may ride one
    (None skips the coverage test). A parked PR still counts as coverage: the
    iteration produced a PR, so the work is on it, not in a worktree to
    salvage."""
    reason = declined_since(comments, host_agent, since_epoch)
    if reason is not None:
        return VERDICT_DECLINED, f"declined this iteration: {reason}"
    if claim_label not in (labels or []):
        return VERDICT_FINISHED, f"{claim_label} released"
    if open_prs is not None and issue_pr_state(open_prs, str(number), repo) != "none":
        return VERDICT_FINISHED, f"an open PR references #{number}; {claim_label} rides it"
    return VERDICT_ABANDONED, f"{claim_label} still standing with no record of why"


def _load(path):
    if not path:
        return None
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def label_names(labels):
    """GitHub spells labels as `{name}` objects on the REST issue and as
    strings in the scout's projections; accept both."""
    return [lbl.get("name", "") if isinstance(lbl, dict) else str(lbl)
            for lbl in labels or []]


def main(argv):
    parser = argparse.ArgumentParser(prog="fleet_completion.py")
    sub = parser.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("assess",
                       help="print `<verdict>\\t<updated_at>\\t<detail>` for one target")
    a.add_argument("--claim-label", required=True,
                   help="the lane claim label the dispatcher took, `<prefix><host>-<agent>`")
    a.add_argument("--host-agent", required=True, help="`<host>-<agent>`")
    a.add_argument("--since", type=int, required=True, help="dispatch epoch")
    a.add_argument("--issue-json", required=True,
                   help="the REST issue object (labels + updated_at), "
                        "or a JSON array of labels")
    a.add_argument("--comments-json", required=True,
                   help="a JSON array of {created_at|createdAt, body}")
    a.add_argument("--prs-json", default=None,
                   help="open PRs as {number, headRefName, body, labels}; "
                        "for the kinds whose label rides a PR")
    a.add_argument("--number", default=None)
    a.add_argument("--repo", default="engine")
    args = parser.parse_args(argv[1:])
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(newline="\n")
    issue = _load(args.issue_json)
    if isinstance(issue, dict):
        labels, updated = issue.get("labels"), issue.get("updated_at") or ""
    else:
        labels, updated = issue, ""
    verdict, detail = assess(
        args.claim_label, label_names(labels), _load(args.comments_json) or [],
        args.host_agent, args.since, open_prs=_load(args.prs_json),
        number=args.number, repo=args.repo)
    print(f"{verdict}\t{updated}\t{detail}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
