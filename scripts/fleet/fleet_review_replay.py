"""fleet_review_replay.py — a submitted fleet review whose verdict edge never landed.

A reviewer posts its body, then stamps the verdict edge as the next call. When
the edge refuses (the reviewing claim was swept mid-iteration) the PR keeps a
current-head review and no verdict label newer than it: invisible to the
opus-recheck and merge lanes, and suppressed for the sonnet lane by the
declined `review:` target. This module is the one definition of that state,
shared by the scout projections, the sonnet-reviewer picker, fleet-survey and
fleet-review-verdict's `--replay` mode:

  head review    the newest submitted review on the PR's current head, from
                 any author. Only it is considered: a later comment-review by
                 someone else on the same head means a human is in the loop.
  fleet review   a head review authored by a FLEET_REVIEW_ACTORS login, with a
                 non-empty body carrying exactly the structure review-pr
                 writes: a `**Verdict:**` line and a `Reviewed by` footer.
  verdict event  a labeled or unlabeled event on one of the labels the four
                 verdict edges add. An unlabel counts because a verdict
                 cleared after the review was cleared deliberately.
  replay pending the head review is a fleet review submitted strictly after
                 the newest verdict event (or there is none). A verdict event
                 at the same second suppresses it.

Review records arrive in two spellings — the GraphQL list the scout fetches
(`author.login`, `commit.oid`, `submittedAt`) and the REST reviews endpoint
(`user.login`, `commit_id`, `submitted_at`); `_fields` reads both.

CLI (fleet-review-verdict is bash; it shells out here rather than re-parsing):

  fleet_review_replay.py inspect --reviews <rest-reviews.json> --head <oid>
      prints one tab-separated line for the head review:
      `<author>\\t<fleet 0|1>\\t<edge|->\\t<reason>\\t<dispatch|->\\t<agent|->`
      exit 1 when no submitted review pins <oid>.
"""
import argparse
import json
import os
import re
import sys

VERDICT_EVENT_LABELS = frozenset({
    "fleet:approved", "fleet:has-nits", "fleet:needs-fix", "fleet:needs-opus-recheck",
})
VERDICT_EVENT_TYPES = frozenset({"labeled", "unlabeled"})
# Dispatched panes post under the human's own login, so the actor alone
# cannot tell a fleet review from a human one; the body structure must.
DEFAULT_REVIEW_ACTORS = ("jakildev",)
UNSUBMITTED_STATES = frozenset({"PENDING", "DISMISSED"})

VERDICT_LINE_RE = re.compile(r"^[ \t]*\*\*Verdict:\*\*[ \t]*(?P<value>.*?)[ \t]*$", re.MULTILINE)
SIGNATURE_RE = re.compile(r"^[^\w\n]*Reviewed by\b", re.MULTILINE)
# Anchored at a line start: the opus-class ending `Opus recheck not
# required (reviewed at opus class).` must never read as an escalation.
OPUS_REQUIRED_RE = re.compile(r"^[ \t]*Opus recheck required:", re.MULTILINE)
NITS_HEADING_RE = re.compile(r"^###[ \t]+Nits[ \t]*$", re.MULTILINE)
HEADING_RE = re.compile(r"^#{1,6}[ \t]", re.MULTILINE)
PROVENANCE_RE = re.compile(
    r"<!--[ \t]*fleet-review[ \t]+dispatch=(?P<dispatch>\S+)[ \t]+agent=(?P<agent>\S+)[ \t]*-->")

EDGE_APPROVE = "verdict-approve"
EDGE_APPROVE_NITS = "verdict-approve-nits"
EDGE_NEEDS_FIX = "verdict-needs-fix"
EDGE_OPUS_RECHECK = "verdict-needs-opus-recheck"


def review_actors(env=None):
    env = os.environ if env is None else env
    raw = env.get("FLEET_REVIEW_ACTORS") or ""
    actors = tuple(a.strip() for a in raw.split(",") if a.strip())
    return actors or DEFAULT_REVIEW_ACTORS


def _fields(review):
    """(author, commit, submitted_at, state, body) across both API spellings."""
    author = review.get("author") if "author" in review else review.get("user")
    if isinstance(author, dict):
        author = author.get("login")
    commit = review.get("commit")
    commit = commit.get("oid") if isinstance(commit, dict) else review.get("commit_id")
    submitted = review.get("submittedAt") or review.get("submitted_at") or ""
    return (author or "", commit or "", submitted, str(review.get("state") or "").upper(),
            review.get("body") or "")


def head_review(reviews, head_oid):
    """The newest submitted review pinning `head_oid`, or None."""
    if not head_oid:
        return None
    best = None
    best_key = None
    for index, review in enumerate(reviews or ()):
        _author, commit, submitted, state, _body = _fields(review)
        if commit != head_oid or not submitted or state in UNSUBMITTED_STATES:
            continue
        key = (submitted, index)
        if best_key is None or key > best_key:
            best, best_key = review, key
    return best


def is_structured(body):
    return bool(body and body.strip() and VERDICT_LINE_RE.search(body)
                and SIGNATURE_RE.search(body))


def is_fleet_review(review, actors=None):
    if review is None:
        return False
    author, _commit, _submitted, _state, body = _fields(review)
    return author in (actors or review_actors()) and is_structured(body)


def _has_substantive_nits(body):
    match = NITS_HEADING_RE.search(body)
    if match is None:
        return False
    following = HEADING_RE.search(body, match.end())
    section = body[match.end():following.start() if following else len(body)]
    return any(line.strip().lstrip("-*").strip() not in ("", "none", "None", "(none)")
               for line in section.splitlines())


def verdict_edge(body):
    """(edge, reason): the verdict edge an existing review body names, or
    (None, why) when the body is ambiguous or names no known verdict.

    `needs-fix` maps to verdict-needs-fix. `approve` maps to
    verdict-needs-opus-recheck when the body escalates (a line starting
    `Opus recheck required:`, or the historical `approve pending Opus
    recheck` verdict), else verdict-approve-nits with a substantive
    `### Nits` section, else verdict-approve."""
    values = VERDICT_LINE_RE.findall(body or "")
    if len(values) != 1:
        return None, f"expected one **Verdict:** line, found {len(values)}"
    token = re.sub(r"[`*_]", "", values[0]).strip().rstrip(".;:!").strip().lower()
    token = re.sub(r"\s+", " ", token)
    if token in ("needs-fix", "needs fix"):
        return EDGE_NEEDS_FIX, "needs-fix verdict"
    if token not in ("approve", "approve pending opus recheck"):
        return None, f"unrecognised verdict {values[0].strip()!r}"
    if token != "approve" or OPUS_REQUIRED_RE.search(body):
        return EDGE_OPUS_RECHECK, "approve with an Opus recheck required"
    if _has_substantive_nits(body):
        return EDGE_APPROVE_NITS, "approve with a ### Nits section"
    return EDGE_APPROVE, "approve"


def provenance(body):
    """{"dispatch", "agent"} from the body's fleet-review marker, or None."""
    match = PROVENANCE_RE.search(body or "")
    if match is None:
        return None
    return {"dispatch": match.group("dispatch"), "agent": match.group("agent")}


def newest_verdict_event(events):
    """The newest verdict-label event's `created_at`, "" when there is none."""
    newest = ""
    for event in events or ():
        label = event.get("label") or {}
        name = label.get("name") if isinstance(label, dict) else label
        if event.get("event") in VERDICT_EVENT_TYPES and name in VERDICT_EVENT_LABELS:
            newest = max(newest, event.get("created_at") or event.get("createdAt") or "")
    return newest


def fleet_review_at(reviews, head_oid, actors=None):
    """The head review's `submittedAt` when it is a fleet review, else ""."""
    review = head_review(reviews, head_oid)
    return _fields(review)[2] if is_fleet_review(review, actors) else ""


def replay_pending(pr):
    """True for a scout PR record whose current-head fleet review postdates
    every verdict event. `verdict_event_at` None means the events were not
    read (not needed, or the fetch failed), which is never pending."""
    reviewed = pr.get("fleet_review_at") or ""
    event = pr.get("verdict_event_at")
    return bool(reviewed) and event is not None and reviewed > event


def _cmd_inspect(args):
    try:
        with open(args.reviews, encoding="utf-8") as handle:
            reviews = json.load(handle)
    except (OSError, ValueError) as exc:
        print(f"fleet_review_replay: cannot read reviews: {exc}", file=sys.stderr)
        return 2
    if isinstance(reviews, list) and reviews and isinstance(reviews[0], list):
        reviews = [r for page in reviews for r in page]
    review = head_review(reviews, args.head)
    if review is None:
        print(f"fleet_review_replay: no submitted review pins {args.head}", file=sys.stderr)
        return 1
    author, _commit, _submitted, _state, body = _fields(review)
    fleet = is_fleet_review(review)
    edge, reason = verdict_edge(body) if fleet else (None, "head review is not a fleet review")
    prov = provenance(body) or {}
    fields = [author or "-", "1" if fleet else "0", edge or "-", reason.replace("\t", " "),
              prov.get("dispatch") or "-", prov.get("agent") or "-"]
    print("\t".join(fields))
    return 0


def main(argv):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(newline="\n")
    parser = argparse.ArgumentParser(prog="fleet_review_replay.py")
    sub = parser.add_subparsers(dest="cmd", required=True)
    inspect = sub.add_parser("inspect", help="describe the head review of a PR")
    inspect.add_argument("--reviews", required=True, help="REST pulls/<N>/reviews JSON")
    inspect.add_argument("--head", required=True, help="the PR's current headRefOid")
    args = parser.parse_args(argv[1:])
    return _cmd_inspect(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
