"""Structured-field validation for epic-decomposition stacks (#1312).

The architect files a multi-issue epic ("stack") via the ``file-epic`` skill:
an umbrella issue plus one child per phase, each child chaining
``**Blocked by:** #<prior>``. The scout's ``blocked_by`` parser and
``fleet-claim``'s ``find-stackable-blockers`` predicate only read the
**standalone** structured lines the template prescribes (SKILL.md step 5):

    **Model:** <opus|sonnet>
    **Part of epic:** #<umbrella>
    **Blocked by:** #<prior>            # non-root children only

Prose forms — a header bullet like ``**Epic:** #1307 · **Blocked on T1 + docs
PR #1306**`` — are invisible to both parsers, so a child filed that way
projects as Available (never stack-claims) and is undiscoverable by
``file-epic``'s own ``--search "Part of epic: #N"`` step. Multi-ref
``**Blocked by:** #A, #B`` lines defeat stack-claim too (the predicate wants
exactly one blocker). This module is the pure predicate half of
``fleet-validate-stack``; the executable supplies the ``gh`` I/O.

Severity split — a finding is an ``error`` only when it is an unambiguous
template violation (no false-positive possible); it is a ``warn`` when the
body is genuinely ambiguous. The one ambiguous case is a *missing* ``**Blocked
by:**`` line on a non-lowest-numbered child: that is either drift (forgot the
line) or a legitimate interior root of a multi-root epic (e.g. #226's T-220
sibling + later follow-ons), and the body alone cannot distinguish them.
``--strict`` in the CLI promotes that warning to an error for known linear
chains.
"""
import re

# A child belongs to umbrella N if a body line *starts* with either the
# canonical ``**Part of epic:**`` field or the condensed ``**Epic:**`` header
# bullet and references #N. Discovery accepts both forms so the drift case
# (``**Epic:**`` instead of the canonical line) is still found — and then
# flagged by validate_child — rather than silently skipped.
_EPIC_MEMBERSHIP_TMPL = r"^\*\*(?:Part of epic|Epic):\*\*\s+#{n}(?!\d)"

# Validation requires the canonical standalone ``**Part of epic:** #N`` line.
_PART_OF_EPIC_TMPL = r"^\*\*Part of epic:\*\*\s+#{n}(?!\d)"
_EPIC_HEADER_TMPL = r"^\*\*Epic:\*\*\s+#{n}(?!\d)"

_MODEL_RE = re.compile(r"^\*\*Model:\*\* (opus|sonnet)\b", re.MULTILINE)
_BLOCKED_BY_LINE_RE = re.compile(r"^\*\*Blocked by:\*\*.*$", re.MULTILINE)
_HASH_REF_RE = re.compile(r"#(\d+)\b")
# A well-formed single-blocker line: ``#N`` is the only ref, optionally
# followed by a `` (rationale)``. The trailing ``( \(|$)`` rejects a second
# ``, #M`` (multi-blocker) by requiring the char after the number to be the
# start of a parenthetical or end-of-line.
_SINGLE_BLOCKED_BY_RE = re.compile(r"^\*\*Blocked by:\*\* #\d+( \(|$)", re.MULTILINE)

ERROR = "error"
WARN = "warn"


def _norm(body):
    """Collapse CRLF/CR so ``^``/``$`` anchors land on real line breaks."""
    return (body or "").replace("\r\n", "\n").replace("\r", "\n")


def is_epic_child(body, umbrella):
    """True iff ``body`` carries an epic-membership line for ``#umbrella``.

    Matches both the canonical ``**Part of epic:** #N`` and the condensed
    ``**Epic:** #N`` header bullet, so a coarse ``gh`` search can be narrowed
    to genuine children of this umbrella regardless of which field form the
    architect used.
    """
    try:
        n = int(umbrella)
    except (TypeError, ValueError):
        return False
    return bool(re.search(_EPIC_MEMBERSHIP_TMPL.format(n=n), _norm(body),
                          re.MULTILINE))


def validate_child(body, umbrella, is_head):
    """Return a list of ``{"severity", "msg"}`` findings (empty == clean).

    ``is_head`` exempts the chain head (lowest-numbered child — see
    ``validate_stack``) from the ``**Blocked by:**`` expectation. A *missing*
    line on a non-head child is a ``warn`` (could be a legitimate interior
    root), not an ``error``, because the body alone cannot tell drift from a
    real root. Malformed / multi-ref lines and a wrong epic field are
    ``error`` — those are unambiguous.
    """
    try:
        n = int(umbrella)
    except (TypeError, ValueError):
        return [{"severity": ERROR, "msg": "invalid umbrella issue number: %r"
                 % (umbrella,)}]

    b = _norm(body)
    findings = []

    def err(msg):
        findings.append({"severity": ERROR, "msg": msg})

    def warn(msg):
        findings.append({"severity": WARN, "msg": msg})

    if not _MODEL_RE.search(b):
        err("missing standalone `**Model:** opus|sonnet` line")

    if not re.search(_PART_OF_EPIC_TMPL.format(n=n), b, re.MULTILINE):
        if re.search(_EPIC_HEADER_TMPL.format(n=n), b, re.MULTILINE):
            err("uses `**Epic:** #%d` header bullet instead of a standalone "
                "`**Part of epic:** #%d` line (file-epic's `--search \"Part of "
                "epic: #N\"` discovery and the queue rely on the canonical "
                "line)" % (n, n))
        else:
            err("missing standalone `**Part of epic:** #%d` line" % n)

    if not is_head:
        bb_lines = _BLOCKED_BY_LINE_RE.findall(b)
        if not bb_lines:
            warn("no standalone `**Blocked by:** #N` line — drift if this "
                 "child chains on a sibling (prose `Blocked on ...` headers "
                 "are invisible to the scout / fleet-claim parsers); fine if "
                 "it is a genuine independent root")
        elif len(bb_lines) > 1:
            err("multiple separate `**Blocked by:**` lines — use a single "
                "`**Blocked by:** #N` line; stack-claim requires exactly one")
        else:
            refs = _HASH_REF_RE.findall(bb_lines[0])
            if len(refs) > 1:
                err("multi-blocker `**Blocked by:** #%s` line — stack-claim "
                    "needs exactly one ref (strip satisfied refs once "
                    "upstreams merge)" % ", #".join(refs))
            elif not _SINGLE_BLOCKED_BY_RE.search(b):
                err("malformed `**Blocked by:**` line (expected `**Blocked "
                    "by:** #N` or `**Blocked by:** #N (rationale)`)")

    return findings


def validate_stack(children, umbrella):
    """Validate every child of a stack; return a structured result dict.

    ``children`` is a list of ``{"number": int, "body": str, "title"?: str}``.
    The lowest-numbered child is treated as the chain head (file-epic files
    sequentially, so creation order == ascending issue number). Result shape::

        {"ok": bool,            # no errors (warnings do not clear ok)
         "n_errors": int, "n_warnings": int, "empty": bool,
         "children": [{"number", "title", "is_head",
                       "findings": [{"severity","msg"}],
                       "n_errors", "n_warnings", "ok"}, ...]}
    """
    if not children:
        return {"ok": True, "empty": True, "n_errors": 0, "n_warnings": 0,
                "children": []}

    ordered = sorted(children, key=lambda c: c["number"])
    head_num = ordered[0]["number"]
    results = []
    n_errors = n_warnings = 0
    for c in ordered:
        is_head = c["number"] == head_num
        findings = validate_child(c.get("body", ""), umbrella, is_head)
        e = sum(1 for f in findings if f["severity"] == ERROR)
        w = sum(1 for f in findings if f["severity"] == WARN)
        n_errors += e
        n_warnings += w
        results.append({
            "number": c["number"],
            "title": c.get("title", ""),
            "is_head": is_head,
            "findings": findings,
            "n_errors": e,
            "n_warnings": w,
            "ok": e == 0,
        })
    return {
        "ok": n_errors == 0,
        "empty": False,
        "n_errors": n_errors,
        "n_warnings": n_warnings,
        "children": results,
    }
