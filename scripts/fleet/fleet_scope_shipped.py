"""Genuine-ship predicate for fleet-queue-ingest's scope-shipped pre-flight.

A merged PR is only evidence that an issue's scope "already landed" when the PR
genuinely *ships* the issue — not when it merely names it. Two layers of
incidental match have to be rejected:

1. No reference at all (#1304). GitHub's PR search (``gh pr list --search
   '#N'``) strips the ``#`` and matches the bare token ``N`` anywhere it
   appears — PR comments, source line numbers, even relevance-only hits with no
   literal ``N``. Trusting ``prs[0]`` mislabels unrelated issues (#1243's #1300
   line-number hit, #1160's #1284 relevance hit).

2. Mentioned but not shipped. A bare word-boundary ``#N`` in a PR *body* is
   still not proof: PRs routinely cite issues they explicitly do NOT fix —
   "bug-fixing is downstream issues (#1260, etc)" (#1260 ← #1282),
   "pre-existing #1269" / "filed as #1269" (#1269 ← #1265), "Refs #N". These
   slipped past the layer-1 fix because the literal ``#N`` token is present.

So a body ``#N`` counts only when a closing-action verb sits directly before it
(``Closes #N``, ``fixes (#N)``, ``supersedes #N``). A ``#N`` in the *title* is
trusted as-is: PRs in this repo are named ``#N: <desc>`` after the issue they
implement, and every observed false positive came from the body, never the
title. The asymmetry is deliberate.
"""
import re

# Closing-action verbs that signal a PR genuinely shipped an issue's scope.
# Mirrors GitHub's auto-close keyword set plus the engine's "implement / ship /
# supersede" idioms. Anchored with \b at the call site so "bug-fixing" never
# satisfies "fix" and "preclose" never satisfies "close".
_CLOSING_VERB = (
    r'clos(?:e|es|ed)|fix(?:es|ed)?|resolv(?:e|es|ed)'
    r'|implement(?:s|ed)?|ship(?:s|ped)?|supersed(?:e|es|ed)'
)
# Only whitespace, a colon, or a single opening bracket may sit between the verb
# and the ref ("Closes #N", "resolved: #N", "fixes (#N)"). The gap is bounded on
# purpose: a permissive ".*?" would let "Fixes #1258. Also see downstream #1260"
# bind the verb across to the unrelated #1260.
_VERB_TO_REF_GAP = r'[\s:]*[(\[]?\s*'


def _ref_pattern(n):
    # ``(?<!\w)`` rejects ``abc#1300``; ``(?!\d)`` stops ``#13000`` / ``#21300``
    # from satisfying ``n=1300``. Matches GitHub's own link-detection bounds.
    return r'(?<!\w)#' + str(int(n)) + r'(?!\d)'


def pr_references_issue(title, body, n):
    """True iff the PR genuinely ships issue ``n`` (see module docstring).

    Title: any word-boundary ``#n`` counts (the ``#N: <desc>`` PR-naming
    convention). Body: ``#n`` counts only when immediately preceded by a
    closing-action verb. A bare body mention ("downstream #n", "pre-existing
    #n", "Refs #n") is rejected.
    """
    if not n:
        return False
    try:
        ref = _ref_pattern(n)
    except (ValueError, TypeError):
        return False
    if re.search(ref, title or ''):
        return True
    body_re = re.compile(
        r'\b(?:' + _CLOSING_VERB + r')\b' + _VERB_TO_REF_GAP + ref,
        re.IGNORECASE,
    )
    return bool(body_re.search(body or ''))


def select_shipped_pr(prs, n):
    """First PR in ``prs`` that genuinely ships issue ``n``, else ``None``.

    Replaces the old ``prs[0]`` blind trust: a merged-PR search hit only counts
    as scope-shipped evidence when ``pr_references_issue`` confirms a genuine
    ship (title ref or closing-verb body ref), not an incidental mention.
    """
    for pr in prs:
        if pr_references_issue(pr.get('title', ''), pr.get('body', ''), n):
            return pr
    return None
