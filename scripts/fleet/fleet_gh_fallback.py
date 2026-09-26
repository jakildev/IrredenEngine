#!/usr/bin/env python3
"""REST fallback for `gh pr|issue` calls that GitHub's GraphQL limiter refused.

`gh pr|issue` subcommands are GraphQL clients. When the GraphQL pool is
exhausted they fail with `GraphQL: API rate limit already exceeded ...` while
REST (`gh api repos/...`) still answers, so a scripted caller can finish its
work over REST instead of stalling until the hourly reset.

Two jobs, both keyed on one predicate (`is_refusal`, REFUSAL_RE):

- `latch_refusal()` records a refusal as `github-graphql.rejected.json` in the
  dispatcher's usage dir: `status:"rejected"` at 100% until `resetsAt`, which
  the dispatcher's usage gate reads as closed. The GraphQL self-report sampler
  (fleet-state-scout) keeps `github-graphql.json` and never touches this file,
  so a healthy-looking self-report cannot re-open the gate mid-refusal. The
  file expires through the evaluator's own `resetsAt + RESET_GRACE_SECONDS`.
- `rest -- <gh argv>` re-runs one refused call over REST and prints what the
  GraphQL call would have printed. Every call shape is either modeled
  exactly or refused with exit 3 (the caller then keeps the GraphQL error):

    pr|issue view N    --json <fields> [--jq P]
    pr list            --json <fields> [--jq P] [--state S] [--limit L]
                       [--base B] [--head H]
    issue list         --json <fields> [--jq P] [--state S] [--limit L]
                       [--label L]...
    pr|issue edit N    (--add-label L | --remove-label L)...
    pr|issue comment N (--body T | --body-file F)
    issue create       --title T (--body T | --body-file F) [--label L]...

  plus `--repo OWNER/REPO` everywhere (without it the REST path uses gh api's
  `{owner}/{repo}` placeholders). Fields: number, title, body, state, url,
  labels, createdAt, updatedAt; pr adds headRefName, headRefOid, baseRefName,
  isDraft, mergedAt, and `pr view` adds mergeable. `comments` is modeled only
  as the sole field of a view with `--jq`, on an item with at most 100
  comments. `author` is not modeled: gh's author object carries a `name` REST
  cannot supply.

  A `--jq` program runs inside one `gh api --jq` call, composed as
  `<mapping> | (<program>)`, so it sees exactly the object gh would have
  built and prints with gh's own encoder. A list call with `--jq` is modeled
  only when the first REST page holds the whole result; otherwise exit 3.

  Label writes keep gh's semantics: every named label must exist in the repo
  (REST would silently create it), a removal of a label the target does not
  carry is a no-op, and all removals run before the add.

CLI (exit codes):
  rest -- <gh argv>                       0 emulated stdout, 3 not modeled,
                                          4 a REST call failed (stderr)
  shim --stderr-file F -- <gh argv>       as `rest`, but first exits 3 unless
                                          F holds a refusal, and latches it

Source of truth: scripts/fleet/fleet_gh_fallback.py in the engine repo.
fleet-net.sh's gh() reaches it through the `shim` arm; fleet-state-scout
imports it. Not installed to ~/bin: callers resolve it beside themselves.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.parse
from pathlib import Path

from fleet_runtime import atomic_json

REFUSAL_RE = re.compile(r"GraphQL: API rate limit (already )?exceeded")
REJECTED_LATCH = "github-graphql.rejected.json"
SAMPLED_LATCH = "github-graphql.json"
# Used when no future hourly reset has been sampled: long enough to span a
# refusal burst, short enough that a wrong guess costs one gate window.
FALLBACK_WINDOW_SECONDS = 900

EXIT_UNMODELED = 3
EXIT_REST_FAILED = 4

PER_PAGE = 100
DEFAULT_LIST_LIMIT = 30  # gh pr|issue list default --limit

_LABEL_KEYS = ("id", "name", "description", "color")
_LABELS = (
    '[.labels[] | {id: .node_id, name: .name, '
    'description: (.description // ""), color: .color}]'
)
_COMMON_FIELDS = {
    "number": ".number",
    "title": ".title",
    "body": '(.body // "")',
    "url": ".html_url",
    "labels": _LABELS,
    "createdAt": ".created_at",
    "updatedAt": ".updated_at",
}
# An issue-route object for a PR carries `pull_request.merged_at`; gh reports
# such a PR as MERGED from `issue view` too.
ISSUE_FIELDS = dict(
    _COMMON_FIELDS,
    state='(if .pull_request.merged_at then "MERGED" '
          'elif .state == "open" then "OPEN" else "CLOSED" end)',
)
PR_FIELDS = dict(
    _COMMON_FIELDS,
    state='(if .merged_at then "MERGED" '
          'elif .state == "open" then "OPEN" else "CLOSED" end)',
    headRefName=".head.ref",
    headRefOid=".head.sha",
    baseRefName=".base.ref",
    isDraft=".draft",
    mergedAt=".merged_at",
)
# REST computes mergeability only on the single-PR route; list items lack it.
PR_VIEW_ONLY_FIELDS = {
    "mergeable": '(if .mergeable == true then "MERGEABLE" '
                 'elif .mergeable == false then "CONFLICTING" else "UNKNOWN" end)',
}
# Comment objects carry only what REST has; gh's minimization, reaction and
# viewer fields read as null.
_COMMENT = (
    '{id: .node_id, author: {login: .user.login}, '
    'authorAssociation: .author_association, body: (.body // ""), '
    'createdAt: .created_at, url: .html_url}'
)

_SHORT_FLAGS = {
    "-R": "--repo", "-q": "--jq", "-L": "--limit", "-s": "--state",
    "-l": "--label", "-b": "--body", "-F": "--body-file", "-t": "--title",
    "-B": "--base", "-H": "--head",
}
_ALLOWED_FLAGS = {
    ("pr", "view"): {"--repo", "--json", "--jq"},
    ("issue", "view"): {"--repo", "--json", "--jq"},
    ("pr", "list"): {"--repo", "--json", "--jq", "--state", "--limit", "--base", "--head"},
    ("issue", "list"): {"--repo", "--json", "--jq", "--state", "--limit", "--label"},
    ("pr", "edit"): {"--repo", "--add-label", "--remove-label"},
    ("issue", "edit"): {"--repo", "--add-label", "--remove-label"},
    ("pr", "comment"): {"--repo", "--body", "--body-file"},
    ("issue", "comment"): {"--repo", "--body", "--body-file"},
    ("issue", "create"): {"--repo", "--title", "--body", "--body-file", "--label"},
}
_REPEATABLE_FLAGS = {"--add-label", "--remove-label", "--label"}
_TAKES_NUMBER = {"view", "edit", "comment"}
_STATES = {
    "pr": ("open", "closed", "merged", "all"),
    "issue": ("open", "closed", "all"),
}
_REPO_RE = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
_NUMBER_RE = re.compile(r"^[1-9][0-9]*$")


class Unmodeled(Exception):
    """The call shape has no exact REST equivalent here."""


class RestFailed(Exception):
    """A REST call failed; `stderr` is what the caller should see."""

    def __init__(self, stderr):
        super().__init__(stderr)
        self.stderr = stderr if isinstance(stderr, bytes) else stderr.encode()


# --- refusal predicate and gate latch ----------------------------------------

def is_refusal(stderr):
    """True when a gh failure's stderr is the GraphQL rate-limit refusal."""
    if isinstance(stderr, bytes):
        stderr = stderr.decode("utf-8", "replace")
    return bool(REFUSAL_RE.search(stderr or ""))


def refusal_reason(argv, stderr):
    """`gh <subcommand>: <first stderr line>` for the latch and log lines."""
    if isinstance(stderr, bytes):
        stderr = stderr.decode("utf-8", "replace")
    first = next((ln.strip() for ln in (stderr or "").splitlines() if ln.strip()), "")
    sub = " ".join(a for a in list(argv)[:2] if not a.startswith("-"))
    return f"gh {sub}: {first}"


def default_usage_dir():
    state = os.environ.get("FLEET_STATE_DIR") or str(Path.home() / ".fleet" / "state")
    return Path(state) / "usage"


def _json_file(path):
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def latch_refusal(reason, usage_dir=None, now=None):
    """Write the rejected latch; True when no live latch existed before.

    `resetsAt` is the sampled hourly reset when that is still ahead, else
    `observed_at + FALLBACK_WINDOW_SECONDS`. Raises OSError when the usage
    dir is unwritable.
    """
    usage = Path(usage_dir) if usage_dir is not None else default_usage_dir()
    now = int(time.time()) if now is None else int(now)
    prior_reset = _json_file(usage / REJECTED_LATCH).get("resetsAt")
    was_live = isinstance(prior_reset, int) and prior_reset > now
    sampled_reset = _json_file(usage / SAMPLED_LATCH).get("resetsAt")
    if isinstance(sampled_reset, int) and sampled_reset > now:
        resets_at = sampled_reset
    else:
        resets_at = now + FALLBACK_WINDOW_SECONDS
    atomic_json(usage / REJECTED_LATCH, {
        "rateLimitType": "github_graphql",
        "status": "rejected",
        "utilization": 1.0,
        "observed_at": now,
        "resetsAt": resets_at,
        "reason": reason,
    })
    return not was_live


# --- gh invocation -------------------------------------------------------------

def _gh_argv():
    """argv prefix that launches gh without a cmd.exe re-parse.

    A native-Windows `shutil.which()` hit on a `.bat` goes through cmd.exe,
    which splits a REST query string at its `&`. The only `.bat` reachable
    here safely is the hermetic test twin beside an extensionless Python
    sibling (tests/lib_hermetic.sh); run that sibling through the interpreter.
    """
    gh = shutil.which("gh") or "gh"
    if os.name == "nt" and gh.lower().endswith(".bat"):
        sibling = gh[: -len(".bat")]
        try:
            with open(sibling, encoding="utf-8") as f:
                first_line = f.readline()
        except OSError:
            first_line = ""
        if first_line.startswith("#!") and "python" in first_line:
            return [sys.executable, sibling]
    return [gh]


def _timeout():
    try:
        return max(1, int(os.environ.get("FLEET_NET_TIMEOUT", "120")))
    except ValueError:
        return 120


def _api(args, payload=None):
    """Run `gh api <args>`; returns (rc, stdout bytes, stderr bytes)."""
    cmd = [*_gh_argv(), "api", *args]
    if payload is not None:
        cmd += ["--input", "-"]
    try:
        proc = subprocess.run(
            cmd,
            input=json.dumps(payload).encode() if payload is not None else None,
            stdin=None if payload is not None else subprocess.DEVNULL,
            capture_output=True,
            timeout=_timeout(),
        )
    except subprocess.TimeoutExpired:
        raise RestFailed(f"gh api {args[0]}: timed out after {_timeout()}s\n")
    except OSError as e:
        raise RestFailed(f"gh api {args[0]}: {e}\n")
    return proc.returncode, proc.stdout, proc.stderr


def _api_ok(args, payload=None):
    rc, out, err = _api(args, payload)
    if rc != 0:
        raise RestFailed(err or f"gh api {args[0]}: exit {rc}\n".encode())
    return out


def _is_not_found(err):
    return b"HTTP 404" in err


# --- argv parsing ----------------------------------------------------------------

class Call:
    def __init__(self, kind, verb):
        self.kind = kind
        self.verb = verb
        self.number = None
        self.flags = {}

    def one(self, flag):
        return self.flags.get(flag, [None])[-1]

    @property
    def repo_path(self):
        return "repos/" + (self.one("--repo") or "{owner}/{repo}")


def parse_call(argv):
    """Parse a gh argv into a Call, or raise Unmodeled."""
    if len(argv) < 2 or (argv[0], argv[1]) not in _ALLOWED_FLAGS:
        raise Unmodeled("subcommand")
    call = Call(argv[0], argv[1])
    allowed = _ALLOWED_FLAGS[(call.kind, call.verb)]
    positionals = []
    i = 2
    while i < len(argv):
        tok = argv[i]
        if tok.startswith("-") and tok != "-":
            name, eq, value = tok.partition("=")
            name = _SHORT_FLAGS.get(name, name)
            if name not in allowed:
                raise Unmodeled(f"flag {tok}")
            if not eq:
                if i + 1 >= len(argv):
                    raise Unmodeled(f"flag {tok} without a value")
                i += 1
                value = argv[i]
            if value == "" and name != "--body":
                raise Unmodeled(f"empty {name}")
            if name in call.flags and name not in _REPEATABLE_FLAGS:
                raise Unmodeled(f"repeated {name}")
            call.flags.setdefault(name, []).append(value)
        else:
            positionals.append(tok)
        i += 1
    if call.verb in _TAKES_NUMBER:
        if len(positionals) != 1 or not _NUMBER_RE.match(positionals[0]):
            raise Unmodeled("positional")
        call.number = positionals[0]
    elif positionals:
        raise Unmodeled("positional")
    repo = call.one("--repo")
    if repo is not None and not _REPO_RE.match(repo):
        raise Unmodeled("repo form")
    return call


def _split_labels(values):
    names = []
    for value in values or []:
        for name in value.split(","):
            if not name:
                raise Unmodeled("empty label")
            if name not in names:
                names.append(name)
    return names


def _fields(call, table):
    raw = call.one("--json")
    if raw is None:
        raise Unmodeled("no --json")
    fields = []
    for f in raw.split(","):
        if f not in table:
            raise Unmodeled(f"field {f}")
        if f not in fields:
            fields.append(f)
    return fields


def _mapping(table, fields):
    return "{" + ", ".join(f"{f}: {table[f]}" for f in fields) + "}"


def _compose(mapping, program):
    # Newlines, not spaces: a trailing `# comment` in the caller's program
    # must not swallow the closing paren.
    return f"{mapping} | (\n{program}\n)"


# --- gh's `--json` exporter encoding ---------------------------------------------

def _dump(value):
    text = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    return text.replace("\u2028", "\\u2028").replace("\u2029", "\\u2029")


def _export_object(obj):
    """gh's no-`--jq` object: top-level keys sorted, label objects in gh order."""
    out = {}
    for key in sorted(obj):
        value = obj[key]
        if key == "labels" and isinstance(value, list):
            value = [{k: label.get(k) for k in _LABEL_KEYS} for label in value]
        out[key] = value
    return out


def render_export(value):
    """Bytes gh prints for `--json` without `--jq` (compact, no HTML escaping)."""
    if isinstance(value, list):
        body = "[" + ",".join(_dump(_export_object(v)) for v in value) + "]"
    else:
        body = _dump(_export_object(value))
    return (body + "\n").encode()


# --- translations ----------------------------------------------------------------

def _view(call):
    pr = call.kind == "pr"
    table = dict(PR_FIELDS, **PR_VIEW_ONLY_FIELDS) if pr else ISSUE_FIELDS
    table = dict(table, comments=None)
    fields = _fields(call, table)
    route = f"{call.repo_path}/{'pulls' if pr else 'issues'}/{call.number}"
    program = call.one("--jq")
    if "comments" in fields:
        if fields != ["comments"] or program is None:
            raise Unmodeled("comments beside other fields, or without --jq")
        count = json.loads(_api_ok([route, "--jq", ".comments"]) or b"null")
        if not isinstance(count, int) or count > PER_PAGE:
            raise Unmodeled("comments span more than one page")
        comments = f"{call.repo_path}/issues/{call.number}/comments?per_page={PER_PAGE}"
        return _api_ok([comments, "--jq", _compose(f"{{comments: map({_COMMENT})}}", program)])
    mapping = _mapping(table, fields)
    if program is not None:
        return _api_ok([route, "--jq", _compose(mapping, program)])
    return render_export(json.loads(_api_ok([route, "--jq", mapping])))


def _list_query(call):
    """(route without page, jq item filter) for a list call."""
    state = call.one("--state") or "open"
    if state not in _STATES[call.kind]:
        raise Unmodeled(f"state {state}")
    if call.kind == "pr":
        params = {"state": "closed" if state in ("closed", "merged") else state}
        base = call.one("--base")
        if base is not None:
            params["base"] = base
        # gh's `closed` includes merged PRs, as REST's does.
        filters = [".merged_at != null"] if state == "merged" else []
        head = call.one("--head")
        if head is not None:
            filters.append(f".head.ref == {json.dumps(head)}")
        endpoint = "pulls"
    else:
        params = {"state": state}
        labels = _split_labels(call.flags.get("--label"))
        if labels:
            params["labels"] = ",".join(labels)
        filters = ['(has("pull_request") | not)']
        endpoint = "issues"
    params["per_page"] = str(PER_PAGE)
    query = urllib.parse.urlencode(params, quote_via=urllib.parse.quote, safe=":,")
    return f"{call.repo_path}/{endpoint}?{query}", " and ".join(filters) or "true"


def _list(call):
    table = PR_FIELDS if call.kind == "pr" else ISSUE_FIELDS
    fields = _fields(call, table)
    limit = call.one("--limit")
    if limit is None:
        limit = DEFAULT_LIST_LIMIT
    elif re.fullmatch(r"[1-9][0-9]*", limit):
        limit = int(limit)
    else:
        raise Unmodeled("limit")
    route, item_filter = _list_query(call)
    mapping = _mapping(table, fields)
    program = call.one("--jq")
    if program is not None:
        # One page, three outputs: the page size, the matches on it, then the
        # caller's program over the first `limit` matches. The page is the
        # whole result iff it was the last page or already held `limit`.
        matches = f"[.[] | select({item_filter})]"
        composed = (
            f"(length | tostring), ({matches} | length | tostring), "
            f"({matches}[:{limit}] | map({mapping}) | (\n{program}\n))"
        )
        out = _api_ok([f"{route}&page=1", "--jq", composed])
        head, _, rest = out.partition(b"\n")
        matched, _, rest = rest.partition(b"\n")
        if int(head) >= PER_PAGE and int(matched) < limit:
            raise Unmodeled("--jq list spans more than one page")
        return rest
    items = []
    page = 1
    while True:
        out = _api_ok([
            f"{route}&page={page}", "--jq",
            f"(length | tostring), (.[] | select({item_filter}) | {mapping})",
        ])
        lines = out.split(b"\n")
        items += [json.loads(line) for line in lines[1:] if line.strip()]
        if len(items) >= limit or int(lines[0]) < PER_PAGE:
            break
        page += 1
    return render_export(items[:limit])


def _target_url(call):
    """html_url of the edit/comment target; fails the way gh does on a wrong kind."""
    endpoint = "pulls" if call.kind == "pr" else "issues"
    out = _api_ok([f"{call.repo_path}/{endpoint}/{call.number}", "--jq", ".html_url"])
    return out.decode().strip()


def _require_labels(call, names, not_found):
    """Fail with `not_found(name)` unless every label exists in the repo."""
    for name in names:
        rc, _out, err = _api([f"{call.repo_path}/labels/{urllib.parse.quote(name, safe='')}",
                              "--jq", ".name"])
        if rc != 0:
            raise RestFailed(not_found(name) if _is_not_found(err) else err)


def _edit(call):
    add = _split_labels(call.flags.get("--add-label"))
    remove = _split_labels(call.flags.get("--remove-label"))
    if not add and not remove:
        raise Unmodeled("edit without a label flag")
    url = _target_url(call)
    _require_labels(
        call, remove + [n for n in add if n not in remove],
        lambda name: f"failed to update {url}: '{name}' not found\nfailed to update 1 issue\n",
    )
    issue = f"{call.repo_path}/issues/{call.number}"
    for name in remove:
        label = urllib.parse.quote(name, safe="")
        rc, _out, err = _api(["-X", "DELETE", f"{issue}/labels/{label}"])
        if rc != 0 and not _is_not_found(err):
            raise RestFailed(err)
    if add:
        _api_ok(["-X", "POST", f"{issue}/labels"], {"labels": add})
    return (url + "\n").encode()


def _body(call):
    text, path = call.one("--body"), call.one("--body-file")
    if (text is None) == (path is None):
        raise Unmodeled("needs exactly one of --body / --body-file")
    if text is not None:
        return text
    if path == "-":
        raise Unmodeled("--body-file - (stdin was consumed by the refused call)")
    try:
        return Path(path).read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError):
        raise Unmodeled("unreadable --body-file")


def _comment(call):
    body = _body(call)
    if call.kind == "pr":
        _target_url(call)
    return _api_ok(
        ["-X", "POST", f"{call.repo_path}/issues/{call.number}/comments", "--jq", ".html_url"],
        {"body": body},
    )


def _create(call):
    title = call.one("--title")
    if title is None:
        raise Unmodeled("create without --title")
    body = _body(call)
    labels = _split_labels(call.flags.get("--label"))
    _require_labels(call, labels, lambda name: f"could not add label: '{name}' not found\n")
    payload = {"title": title, "body": body}
    if labels:
        payload["labels"] = labels
    return _api_ok(["-X", "POST", f"{call.repo_path}/issues", "--jq", ".html_url"], payload)


_HANDLERS = {"view": _view, "list": _list, "edit": _edit, "comment": _comment, "create": _create}


def translate(argv):
    """Run one gh call over REST; returns its stdout bytes.

    Raises Unmodeled when the shape is not modeled — before any REST call for
    an argument-level shape, after read-only calls for a size-dependent one
    (a comment count or a list page) — and RestFailed when a REST call fails.
    """
    call = parse_call(list(argv))
    return _HANDLERS[call.verb](call)


# --- CLI ---------------------------------------------------------------------------

def _run_rest(argv):
    try:
        out = translate(argv)
    except Unmodeled as e:
        sys.stderr.write(f"fleet_gh_fallback: not modeled over REST: {e}\n")
        return EXIT_UNMODELED
    except RestFailed as e:
        sys.stderr.buffer.write(e.stderr)
        return EXIT_REST_FAILED
    # Bytes, not print(): native-Windows text mode would turn \n into \r\n.
    sys.stdout.buffer.write(out)
    sys.stdout.buffer.flush()
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(prog="fleet_gh_fallback.py")
    sub = parser.add_subparsers(dest="cmd", required=True)
    rest = sub.add_parser("rest")
    rest.add_argument("gh_argv", nargs=argparse.REMAINDER)
    shim = sub.add_parser("shim")
    shim.add_argument("--stderr-file", required=True)
    shim.add_argument("gh_argv", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)

    gh_argv = args.gh_argv[1:] if args.gh_argv[:1] == ["--"] else args.gh_argv
    if args.cmd == "shim":
        try:
            stderr = Path(args.stderr_file).read_bytes()
        except OSError:
            return EXIT_UNMODELED
        if not is_refusal(stderr):
            return EXIT_UNMODELED
        try:
            latch_refusal(refusal_reason(gh_argv, stderr))
        except OSError as e:
            sys.stderr.write(f"fleet_gh_fallback: latch failed: {e}\n")
    return _run_rest(gh_argv)


if __name__ == "__main__":
    sys.exit(main())
