#!/usr/bin/env python3
"""Hermetic `gh` stub: GraphQL subcommands and REST routes over one state file.

Copied into a suite's PATH bin as `gh` (plus the `gh.bat` twin from
lib_hermetic.sh), never run as a suite. The name avoids the test_* pattern.

Env:
  GH_STUB_STATE     JSON state file (read, and rewritten by mutations)
  GH_STUB_LOG       one line per invocation: the argv joined by spaces
  GH_STUB_MISSES    one line per call shape the stub does not model
  GH_STUB_THROTTLE  non-empty: every `pr|issue` subcommand is refused with
                    GitHub's GraphQL rate-limit error; `api` still answers

State:
  {"repo": "o/r",
   "labels": {"<name>": {"description": str|null, "color": str}},
   "issues": {"<N>": {"title", "body": str|null, "state": "open"|"closed",
                      "labels": [names], "created_at", "updated_at",
                      "comments": [{"id", "body", "user", "created_at"}]}},
   "pulls":  {"<N>": issue keys plus "head", "sha", "base", "draft",
                     "merged_at": str|null, "mergeable": bool|null},
   "reviews": {"<N>": [{"commit_id", "state"}]}}

The GraphQL arm builds gh's objects straight from the records, independently
of fleet_gh_fallback.py's jq mappings. `--jq` runs through the real `jq`
binary and is printed the way gh prints it (strings raw, everything else
compact with sorted keys and HTML-escaped); without jq the stub fails closed.
"""

import json
import os
import shutil
import subprocess
import sys
import urllib.parse
import zlib

REFUSAL = "GraphQL: API rate limit already exceeded for user ID 1234567.\n"
LABEL_KEYS = ("id", "name", "description", "color")


def out(data):
    sys.stdout.buffer.write(data if isinstance(data, bytes) else data.encode())


def die(message, rc=1):
    sys.stderr.buffer.write(message.encode())
    sys.exit(rc)


def miss(argv, why):
    path = os.environ.get("GH_STUB_MISSES")
    if path:
        with open(path, "a", encoding="utf-8", newline="\n") as f:
            f.write(f"{why}: {' '.join(argv)}\n")
    die(f"stub gh: unmodeled ({why}): {' '.join(argv)}\n", 2)


def load():
    with open(os.environ["GH_STUB_STATE"], encoding="utf-8") as f:
        return json.load(f)


def save(state):
    with open(os.environ["GH_STUB_STATE"], "w", encoding="utf-8", newline="\n") as f:
        json.dump(state, f, indent=1, sort_keys=True)


# --- encoders ---------------------------------------------------------------------

def go_json(value):
    """Go's json.Marshal: compact, sorted keys, HTML-escaped."""
    text = json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"))
    for raw, esc in (("&", "\\u0026"), ("<", "\\u003c"), (">", "\\u003e"),
                     ("\u2028", "\\u2028"), ("\u2029", "\\u2029")):
        text = text.replace(raw, esc)
    return text


def export_json(value):
    """gh's `--json` exporter: top-level keys sorted, labels in gh field order."""
    def obj(o):
        items = []
        for key in sorted(o):
            v = o[key]
            if key == "labels":
                v = [{k: lab[k] for k in LABEL_KEYS} for lab in v]
            items.append((key, v))
        return dict(items)
    shaped = [obj(v) for v in value] if isinstance(value, list) else obj(value)
    text = json.dumps(shaped, ensure_ascii=False, separators=(",", ":"))
    return text.replace("\u2028", "\\u2028").replace("\u2029", "\\u2029") + "\n"


def run_jq(program, value):
    jq = shutil.which("jq")
    if not jq:
        die("stub gh: jq not on PATH; --jq is not modeled without it\n", 2)
    proc = subprocess.run([jq, "-c", program], input=json.dumps(value).encode(),
                          capture_output=True)
    if proc.returncode != 0:
        die(proc.stderr.decode("utf-8", "replace").replace("\r", ""), 1)
    text = ""
    for line in proc.stdout.decode("utf-8").replace("\r", "").split("\n"):
        if line:
            v = json.loads(line)
            text += (v if isinstance(v, str) else go_json(v)) + "\n"
    return text


# --- records -----------------------------------------------------------------------

def record(state, number):
    """(kind, record) for a number, or (None, None)."""
    if number in state["pulls"]:
        return "pr", state["pulls"][number]
    if number in state["issues"]:
        return "issue", state["issues"][number]
    return None, None


def label_rest(state, name):
    lab = state["labels"][name]
    return {"id": zlib.crc32(name.encode()), "node_id": "LA_" + name, "name": name,
            "color": lab["color"], "default": False, "description": lab["description"]}


def label_gh(state, name):
    lab = state["labels"][name]
    return {"id": "LA_" + name, "name": name, "description": lab["description"] or "",
            "color": lab["color"]}


def html_url(state, kind, number):
    return f"https://github.com/{state['repo']}/{'pull' if kind == 'pr' else 'issues'}/{number}"


def comment_url(state, number, c):
    return f"{html_url(state, record(state, number)[0], number)}#issuecomment-{c['id']}"


def comment_rest(state, number, c):
    return {"id": c["id"], "node_id": f"IC_{c['id']}", "user": {"login": c["user"]},
            "author_association": "OWNER", "body": c["body"], "created_at": c["created_at"],
            "html_url": comment_url(state, number, c)}


def issue_rest(state, number):
    kind, rec = record(state, number)
    obj = {"number": int(number), "node_id": f"I_{number}", "title": rec["title"],
           "body": rec["body"], "state": rec["state"],
           "labels": [label_rest(state, n) for n in rec["labels"]],
           "html_url": html_url(state, kind, number), "user": {"login": "someone"},
           "comments": len(rec.get("comments", [])),
           "created_at": rec["created_at"], "updated_at": rec["updated_at"]}
    if kind == "pr":
        obj["pull_request"] = {"merged_at": rec["merged_at"]}
    return obj


def pull_rest(state, number, single=True):
    rec = state["pulls"][number]
    obj = issue_rest(state, number)
    obj.pop("pull_request")
    obj.update({"head": {"ref": rec["head"], "sha": rec["sha"]}, "base": {"ref": rec["base"]},
                "draft": rec["draft"], "merged_at": rec["merged_at"]})
    if single:
        obj["mergeable"] = rec["mergeable"]
    else:
        obj.pop("comments")
    return obj


def gh_object(state, kind, number):
    """Everything gh's GraphQL query could return for one issue or PR."""
    rec_kind, rec = record(state, number)
    if rec_kind == "pr":
        st = "MERGED" if rec["merged_at"] else rec["state"].upper()
    else:
        st = rec["state"].upper()
    obj = {"number": int(number), "title": rec["title"], "body": rec["body"] or "",
           "state": st, "url": html_url(state, rec_kind, number),
           "labels": [label_gh(state, n) for n in rec["labels"]],
           "createdAt": rec["created_at"], "updatedAt": rec["updated_at"],
           "author": {"id": "U_1", "is_bot": False, "login": "someone", "name": "Some One"},
           "comments": [{"id": f"IC_{c['id']}", "author": {"login": c["user"]},
                         "authorAssociation": "OWNER", "body": c["body"],
                         "createdAt": c["created_at"], "includesCreatedEdit": False,
                         "isMinimized": False, "minimizedReason": "",
                         "reactionGroups": [], "viewerDidAuthor": False,
                         "url": comment_url(state, number, c)}
                        for c in rec.get("comments", [])]}
    if kind == "pr":
        obj.update({"headRefName": rec["head"], "headRefOid": rec["sha"],
                    "baseRefName": rec["base"], "isDraft": rec["draft"],
                    "mergedAt": rec["merged_at"],
                    "mergeable": {True: "MERGEABLE", False: "CONFLICTING"}.get(
                        rec["mergeable"], "UNKNOWN"),
                    "statusCheckRollup": []})
    return obj


# --- GraphQL arm (gh pr|issue ...) ---------------------------------------------------

VALUE_FLAGS = {"--repo": "repo", "-R": "repo", "--json": "json", "--jq": "jq", "-q": "jq",
               "--state": "state", "-s": "state", "--limit": "limit", "-L": "limit",
               "--base": "base", "-B": "base", "--head": "head", "-H": "head",
               "--search": "search", "-S": "search", "--title": "title", "-t": "title",
               "--body": "body", "-b": "body", "--body-file": "body_file", "-F": "body_file"}
LIST_FLAGS = {"--label": "label", "-l": "label", "--add-label": "add", "--remove-label": "remove"}


def parse(argv):
    opts, lists, pos = {}, {}, []
    i = 0
    while i < len(argv):
        tok = argv[i]
        name, eq, value = tok.partition("=")
        if name in VALUE_FLAGS or name in LIST_FLAGS:
            if not eq:
                i += 1
                if i >= len(argv):
                    die(f"flag needs an argument: {name}\n", 1)
                value = argv[i]
            if name in LIST_FLAGS:
                lists.setdefault(LIST_FLAGS[name], []).extend(value.split(","))
            else:
                opts[VALUE_FLAGS[name]] = value
        elif tok.startswith("-"):
            die(f"unknown flag: {tok}\n", 1)
        else:
            pos.append(tok)
        i += 1
    return opts, lists, pos


def select_fields(obj, fields):
    return {f: obj[f] for f in fields}


def emit(opts, value):
    fields = opts["json"].split(",")
    shaped = ([select_fields(v, fields) for v in value] if isinstance(value, list)
              else select_fields(value, fields))
    out(run_jq(opts["jq"], shaped) if "jq" in opts else export_json(shaped))


def gql_view(state, kind, opts, number):
    rec_kind, _rec = record(state, number)
    if rec_kind is None or (kind == "pr" and rec_kind != "pr"):
        what = "PullRequest" if kind == "pr" else "issue or pull request"
        die(f"GraphQL: Could not resolve to a {what} with the number of {number}. "
            f"(repository.{'pullRequest' if kind == 'pr' else 'issue'})\n")
    emit(opts, gh_object(state, kind, number))


def gql_list(state, kind, opts, lists):
    want = opts.get("state", "open")
    labels = lists.get("label", [])
    rows = []
    source = state["pulls"] if kind == "pr" else state["issues"]
    for number in sorted(source, key=int, reverse=True):
        obj = gh_object(state, kind, number)
        if want == "open" and obj["state"] != "OPEN":
            continue
        if want == "closed" and obj["state"] == "OPEN":
            continue
        if want == "merged" and obj["state"] != "MERGED":
            continue
        if any(lab not in [x["name"] for x in obj["labels"]] for lab in labels):
            continue
        if "base" in opts and obj.get("baseRefName") != opts["base"]:
            continue
        if "head" in opts and obj.get("headRefName") != opts["head"]:
            continue
        rows.append(obj)
    emit(opts, rows[: int(opts.get("limit", "30"))])


def gql_edit(state, kind, opts, lists, number):
    rec_kind, rec = record(state, number)
    if rec_kind is None or (kind == "pr" and rec_kind != "pr"):
        die(f"GraphQL: Could not resolve to a PullRequest with the number of {number}.\n")
    url = html_url(state, rec_kind, number)
    for name in lists.get("add", []) + lists.get("remove", []):
        if name not in state["labels"]:
            die(f"failed to update {url}: '{name}' not found\nfailed to update 1 issue\n")
    rec["labels"] = [n for n in rec["labels"] if n not in lists.get("remove", [])]
    rec["labels"] += [n for n in lists.get("add", []) if n not in rec["labels"]]
    save(state)
    out(url + "\n")


def body_of(opts):
    if "body_file" in opts:
        with open(opts["body_file"], encoding="utf-8") as f:
            return f.read()
    return opts.get("body", "")


def next_comment_id(state):
    ids = [c["id"] for group in ("issues", "pulls") for rec in state[group].values()
           for c in rec.get("comments", [])]
    return max(ids, default=900) + 1


def add_comment(state, number, body):
    _kind, rec = record(state, number)
    c = {"id": next_comment_id(state), "body": body, "user": "someone",
         "created_at": "2026-01-02T00:00:00Z"}
    rec.setdefault("comments", []).append(c)
    save(state)
    return c


def gql_comment(state, kind, opts, number):
    rec_kind, _rec = record(state, number)
    if rec_kind is None or (kind == "pr" and rec_kind != "pr"):
        die(f"GraphQL: Could not resolve to a PullRequest with the number of {number}.\n")
    c = add_comment(state, number, body_of(opts))
    out(comment_url(state, number, c) + "\n")


def create_issue(state, title, body, labels):
    number = str(max((int(n) for g in ("issues", "pulls") for n in state[g]), default=0) + 1)
    state["issues"][number] = {"title": title, "body": body, "state": "open",
                               "labels": labels, "created_at": "2026-01-02T00:00:00Z",
                               "updated_at": "2026-01-02T00:00:00Z", "comments": []}
    save(state)
    return number


def gql_create(state, opts, lists):
    for name in lists.get("label", []):
        if name not in state["labels"]:
            die(f"could not add label: '{name}' not found\n")
    number = create_issue(state, opts["title"], body_of(opts), lists.get("label", []))
    out(html_url(state, "issue", number) + "\n")


def graphql(argv):
    if os.environ.get("GH_STUB_THROTTLE"):
        die(REFUSAL)
    kind, verb = argv[0], argv[1]
    opts, lists, pos = parse(argv[2:])
    state = load()
    if "repo" in opts and opts["repo"] != state["repo"]:
        die(f"GraphQL: Could not resolve to a Repository with the name '{opts['repo']}'.\n")
    if "search" in opts:
        miss(argv, "search")
    if verb in ("view", "edit", "comment"):
        if len(pos) != 1 or not pos[0].isdigit():
            miss(argv, "non-numeric target")
        number = pos[0]
    if verb == "view":
        gql_view(state, kind, opts, number)
    elif verb == "list":
        gql_list(state, kind, opts, lists)
    elif verb == "edit":
        if set(opts) - {"repo"}:
            miss(argv, "edit flag")
        gql_edit(state, kind, opts, lists, number)
    elif verb == "comment":
        gql_comment(state, kind, opts, number)
    elif (kind, verb) == ("issue", "create"):
        gql_create(state, opts, lists)
    else:
        miss(argv, "subcommand")


# --- REST arm (gh api ...) ---------------------------------------------------------------

def not_found():
    die("gh: Not Found (HTTP 404)\n")


def page(items, query):
    per_page = int(query.get("per_page", ["30"])[0])
    start = (int(query.get("page", ["1"])[0]) - 1) * per_page
    return items[start:start + per_page]


def rest_get(state, parts, query):
    if parts == ["issues"]:
        want = query.get("state", ["open"])[0]
        labels = [x for x in query.get("labels", [""])[0].split(",") if x]
        numbers = sorted(list(state["issues"]) + list(state["pulls"]), key=int, reverse=True)
        rows = [issue_rest(state, n) for n in numbers]
        rows = [r for r in rows if want == "all" or r["state"] == want]
        rows = [r for r in rows if all(lab in [x["name"] for x in r["labels"]] for lab in labels)]
        return page(rows, query)
    if parts == ["pulls"]:
        want = query.get("state", ["open"])[0]
        rows = [pull_rest(state, n, single=False)
                for n in sorted(state["pulls"], key=int, reverse=True)]
        rows = [r for r in rows if want == "all" or r["state"] == want]
        if "base" in query:
            rows = [r for r in rows if r["base"]["ref"] == query["base"][0]]
        return page(rows, query)
    if len(parts) == 2 and parts[0] == "labels":
        if parts[1] not in state["labels"]:
            not_found()
        return label_rest(state, parts[1])
    if len(parts) >= 2 and parts[0] in ("issues", "pulls") and parts[1].isdigit():
        kind, rec = record(state, parts[1])
        if kind is None or (parts[0] == "pulls" and kind != "pr"):
            not_found()
        if len(parts) == 2 and parts[0] == "issues":
            return issue_rest(state, parts[1])
        if len(parts) == 2:
            return pull_rest(state, parts[1])
        if parts[2:] == ["comments"] and parts[0] == "issues":
            return page([comment_rest(state, parts[1], c) for c in rec.get("comments", [])], query)
        if parts[2:] == ["reviews"] and parts[0] == "pulls":
            return state.get("reviews", {}).get(parts[1], [])
    return None


def rest(argv):
    route, method, jq, stdin, paginate = None, "GET", None, None, False
    i = 1
    while i < len(argv):
        tok = argv[i]
        if tok == "-X":
            method = argv[i + 1]
            i += 2
        elif tok == "--jq":
            jq = argv[i + 1]
            i += 2
        elif tok == "--input" and argv[i + 1] == "-":
            stdin = json.loads(sys.stdin.buffer.read() or b"null")
            i += 2
        elif tok == "--paginate":
            paginate = True
            i += 1
        elif tok.startswith("-") or route is not None:
            miss(argv, "api flag")
        else:
            route = tok
            i += 1
    state = load()
    path, _, qs = route.lstrip("/").partition("?")
    query = urllib.parse.parse_qs(qs)
    prefix = "repos/" + state["repo"] + "/"
    path = path.replace("repos/{owner}/{repo}/", prefix)
    if not path.startswith(prefix):
        not_found()
    parts = [urllib.parse.unquote(p) for p in path[len(prefix):].split("/")]
    if method == "GET":
        if paginate and parts[-1] != "reviews":
            miss(argv, "paginate")
        body = rest_get(state, parts, query)
        if body is None:
            miss(argv, "route")
    elif method == "DELETE" and len(parts) == 4 and parts[0] == "issues" and parts[2] == "labels":
        _kind, rec = record(state, parts[1])
        if rec is None or parts[3] not in rec["labels"]:
            die("gh: Label does not exist (HTTP 404)\n")
        rec["labels"].remove(parts[3])
        save(state)
        body = [label_rest(state, n) for n in rec["labels"]]
    elif method == "POST" and parts[2:] == ["labels"] and parts[0] == "issues":
        _kind, rec = record(state, parts[1])
        if rec is None:
            not_found()
        for name in stdin["labels"]:
            # REST creates a label it has never seen; gh refuses it.
            state["labels"].setdefault(name, {"description": None, "color": "ededed"})
            if name not in rec["labels"]:
                rec["labels"].append(name)
        save(state)
        body = [label_rest(state, n) for n in rec["labels"]]
    elif method == "POST" and parts[2:] == ["comments"] and parts[0] == "issues":
        if record(state, parts[1])[0] is None:
            not_found()
        body = comment_rest(state, parts[1], add_comment(state, parts[1], stdin["body"]))
    elif method == "POST" and parts == ["issues"]:
        for name in stdin.get("labels", []):
            state["labels"].setdefault(name, {"description": None, "color": "ededed"})
        body = issue_rest(state, create_issue(state, stdin["title"], stdin["body"],
                                              stdin.get("labels", [])))
    else:
        miss(argv, "api method/route")
    out(run_jq(jq, body) if jq is not None else json.dumps(body) + "\n")


def main():
    argv = sys.argv[1:]
    with open(os.environ["GH_STUB_LOG"], "a", encoding="utf-8", newline="\n") as f:
        f.write(" ".join(argv) + "\n")
    if argv[:1] == ["api"]:
        rest(argv)
    elif len(argv) >= 2 and argv[0] in ("pr", "issue"):
        graphql(argv)
    else:
        miss(argv, "command")


if __name__ == "__main__":
    main()
