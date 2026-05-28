"""Genuine-reference predicate for fleet-queue-ingest's scope-shipped pre-flight.

A merged PR is only evidence that an issue's scope "already landed" when the PR
carries a genuine cross-reference to the issue — a word-boundary ``#N`` in its
title or body (a real ``Closes #N`` / ``supersedes #N`` / ``#N`` mention).

GitHub's PR search (``gh pr list --search '#N'``) strips the ``#`` and matches
the bare token ``N`` anywhere it appears, including PR *comments* and source
line numbers, and can even return relevance-only hits with no literal ``N`` at
all. Trusting ``prs[0]`` from that search mislabels unrelated issues as
``fleet:scope-shipped`` and posts a misleading "scope already landed" comment
(#1304: #1243's #1300 line-number hit, #1160's #1284 relevance hit). This
predicate is the verification step that rejects those incidental matches.
"""
import re


def pr_references_issue(title, body, n):
    """True iff a word-boundary ``#{n}`` appears in the PR title or body.

    The leading ``(?<!\\w)`` guard rejects ``abc#1300`` (alphanumeric immediately
    before ``#``); the trailing ``(?!\\d)`` guard stops ``#13000`` from satisfying
    ``n=1300``. Together they match GitHub's own link-detection boundaries.
    Requiring the literal ``#`` rejects bare line-number and relevance hits that
    carry no cross-reference at all.
    """
    if not n:
        return False
    try:
        pattern = re.compile(r'(?<!\w)#' + str(int(n)) + r'(?!\d)')
    except ValueError:
        return False
    return bool(pattern.search(title or '')) or bool(pattern.search(body or ''))


def select_shipped_pr(prs, n):
    """First PR in ``prs`` that genuinely references issue ``n``, else ``None``.

    Replaces the old ``prs[0]`` blind trust: a merged-PR search hit only counts
    as scope-shipped evidence when the PR actually cross-references the issue.
    """
    for pr in prs:
        if pr_references_issue(pr.get('title', ''), pr.get('body', ''), n):
            return pr
    return None
