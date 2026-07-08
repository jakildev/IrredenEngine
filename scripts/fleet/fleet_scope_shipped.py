"""Genuine-ship predicate for fleet-queue-ingest's scope-shipped pre-flight.

A merged PR is only evidence that an issue's scope "already landed" when the PR
genuinely *ships* the issue — not when it merely names it. Three layers of
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

3. Range endpoint, not implemented (#1602 / #1612 ← #1614). An epic-planning PR
   names the children it *files* as a range in its title — "docs: re-plan ...
   — file children #1602-#1612". The title-trust below then matched the two
   range *endpoints* (#1602 and #1612; the middle children #1603-#1611 were
   untouched because only the endpoints are literally written with a ``#``). A
   ``#N`` that is an endpoint of a ``#A-#B`` range is enumerating issues, never
   shipping one, so range endpoints are rejected in both title and body.

4. Plan/design-doc title (#1807 ← #1809, #1802 ← #1805, #1354 ← #1411). A plan
   or design PR is titled after the issue it *plans/designs*, not one it ships:
   "docs: plan rotation-profiling task (#1807)", "docs: plan #1052 …
   (#1802/#1803/#1804)", "docs/design: … (#1354)". The title-trust below then
   read the planned issue — or a filed child named with a ``/`` separator,
   which the layer-3 dash guard doesn't cover — as shipped. A ``docs:
   (re-)plan`` / ``docs/design:`` title commits the plan, never the
   implementation, so its title ref is NOT trusted: the issue ships only if a
   body closing-verb says so (a doc PR whose deliverable genuinely IS the doc
   change still ships via ``Closes #N`` in the body).

5. Closing verb inside a code span (#1824 ← #1854). Layer 4 stops a plan-doc
   *title* from shipping, but a plan PR's *body* routinely quotes the closing
   verb the future implementation PR will use: "the structured plan for #1824
   (planning step output; impl PR will carry the code + ``Closes #1824``)" and
   "Plan doc for #1824 — does not close the issue (the implementation PR
   will)". That ``Closes #1824`` sits in an inline code span — it is *showing*
   the literal text the impl PR will write, not performing a close — so the
   layer-4 title guard fires while the body closing-verb check still matches.
   Markdown code spans (fenced ```` ``` ```` blocks and inline `` `…` `` spans)
   are therefore stripped from the body before the closing-verb scan: a verb
   quoted in code is documentation, never an action. A genuine ship writes
   ``Closes #N`` as prose, never in backticks, so the strip costs no true
   positive.

6. Deferral marker (#1640 ← #1700). A PR may name ``#N`` in a trusted (non-plan)
   title only to mark it *deferred*: "render: doc the ... invariant
   (#1640 deferred)" is a doc-only, design-blocked PR that documents the gap and
   escalates the fix — it does NOT land #1640's scope. Because it is titled
   ``render:`` (not ``docs:``) the layer-4 plan-doc guard does not fire, and the
   bare ``#1640`` in the title satisfies title-trust, so ingest false-stamped
   ``fleet:scope-shipped`` on #1640 — and re-stamped it every pass after the
   architect removed the label, an un-winnable label fight. A ``#N`` carrying an
   adjacent deferral word ("(#N deferred)", "#N — deferred", "defers #N") is an
   explicit non-ship signal, so a deferral-marked title ref is NOT trusted (the
   deferral word must sit directly on one side of the ref, within a bounded gap
   like the range/verb guards, so "fix #1234 and defer #1235" still ships #1234).

7. Prep / partial marker (#2258 ← #2266). A PR may name ``#N`` in a trusted
   (non-plan) title only to mark it *preparatory* — a narrowed refactor that
   lands groundwork for #N without shipping its scope: "render: extract
   sunBakeFrustumUVBounds shared helper (#2258 prep)" is linked ``Part of``,
   not ``Closes``, and its body states it "keeps only the independently-correct
   refactor and drops the dead plumbing". Because it is titled ``render:`` (not
   ``docs:``) the layer-4 plan-doc guard does not fire, and the bare ``#2258``
   in the title satisfies title-trust, so ingest false-stamped
   ``fleet:scope-shipped`` on #2258 — clobbering the issue's re-queue the instant
   its plan cleared review. A ``#N`` carrying an adjacent prep word ("(#N prep)",
   "prep for #N", "preparatory #N") is an explicit not-yet-shipped signal, so a
   prep-marked title ref is NOT trusted — same bounded-gap rule as layer 6's
   deferral marker (the prep word must sit directly on one side of the ref, so
   "ship #1234, prep #1235" still ships #1234).

So a body ``#N`` counts only when a closing-action verb sits directly before it
in prose (``Closes #N``, ``fixes (#N)``, ``supersedes #N``) — code spans are
stripped first (layer 5), so a verb quoted in backticks does not count. A
``#N`` in the *title* is trusted as-is — PRs in this repo are named
``#N: <desc>`` after the issue they implement — EXCEPT (layer 3) when it is a
range endpoint, (layer 4) when the title is a plan/design-doc title, (layer 6)
when the ref is marked deferred, or (layer 7) when the ref is marked prep: all
four shapes name issues they enumerate, plan, defer, or prepare without
implementing.
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


# Range dashes (hyphen-minus, en dash, em dash) — the separators an epic-planning
# PR uses to name a span of filed children in its title: "file children #1602-#1612".
_RANGE_DASH = r'[-–—]'


# Plan/design-doc PR titles (layer 4). A ``docs:``-scoped PR whose subject is
# plan / re-plan / design, or any ``docs/design:`` PR, names the issue it plans
# or designs — never one it implements. The match is anchored to the ``docs``
# commit scope so a normal implementation PR titled ``docs: #N fix the snippet``
# (deliverable IS the doc) is unaffected; only plan/design subjects are caught.
_PLAN_DOC_TITLE = re.compile(
    r'^\s*docs/design\b'
    r'|^\s*docs(?:/[\w-]+)?:\s*(?:re-?)?(?:plan|design)\b',
    re.IGNORECASE,
)


# Markdown code spans — layer 5 (strips before the closing-verb scan; layer 4
# stopped title refs, not body code spans). A closing verb inside a code span is
# literal text being quoted — a plan PR documenting the ``Closes #N`` its future
# impl PR will write — not an action. Fenced ``` blocks are stripped first (so an
# inline backtick inside a fenced block can't desync the inline pass), then
# inline `…` spans. Both collapse to a space so adjacent words don't fuse.
_FENCED_CODE = re.compile(r'```.*?```', re.DOTALL)
_INLINE_CODE = re.compile(r'`[^`]*`')


def _strip_code_spans(s):
    return _INLINE_CODE.sub(' ', _FENCED_CODE.sub(' ', s))


def _ref_pattern(n):
    # ``(?<!\w)`` rejects ``abc#1300``; ``(?!\d)`` stops ``#13000`` / ``#21300``
    # from satisfying ``n=1300``. The two range guards reject a ``#N`` that is an
    # endpoint of a ``#A-#B`` range (an epic-planning PR enumerating the children
    # it FILES, not implementing one): the ``(?<!RANGE_DASH)`` lookbehind drops a
    # range END (``...-#1612``) and the trailing ``(?!\s*RANGE_DASH\s*#?\d)``
    # lookahead drops a range START (``#1602`` directly before ``-#1612``).
    # Matches GitHub's own link-detection bounds, minus range endpoints.
    return (
        r'(?<!\w)(?<!' + _RANGE_DASH + r')#' + str(int(n))
        + r'(?!\d)(?!\s*' + _RANGE_DASH + r'\s*#?\d)'
    )


# Non-ship marker words (layers 6-7) — the vocabulary a PR uses beside a trusted
# title ref to mark that it does NOT land ``#N``'s scope:
#   layer 6 — deferral: "deferred", "defers", "deferring" (a doc-and-defer PR
#     that escalates the fix).
#   layer 7 — prep/partial: "prep", "preps", "prepping", "preparatory" (a
#     narrowed refactor that lands groundwork for #N without shipping it).
# ``prep`` is bounded by ``\b`` on both sides at the call site, so a legitimate
# ship whose title merely *starts* with the letters ("#N prepend the header")
# does not match — mirrors layer 6's "deferential" guard.
_NONSHIP_MARKER = r'defer(?:s|red|ring)?|prep(?:s|ping|aratory)?'
# Bounded gap between the ref and the marker word — whitespace plus the light
# punctuation that brackets a parenthetical marker ("(#N deferred)", "(#N prep)",
# "#N — deferred"). Kept small (like _VERB_TO_REF_GAP) so a marker word only binds
# to an *adjacent* ref: "fix #1234 and defer #1235" leaves #1234 shippable.
_MARKER_GAP = r'[\s():.,;—–-]{0,3}'


def _ref_is_nonship_marked(text, n):
    """True iff a ``#n`` reference in ``text`` carries an adjacent non-ship
    marker — a deferral (layer 6: "(#n deferred)", "defers #n") or a prep/partial
    marker (layer 7: "(#n prep)", "prep for #n", "preparatory #n"). Such a ref is
    an explicit "this PR does NOT ship #n" signal, so it must not be trusted even
    in a title. The marker word must sit directly on one side of the ref (bounded
    gap) — a far-away marker aimed at some other ``#m`` does not suppress ``#n``.

    Trade-off (deliberate — matches layers 3-5's bias toward under-stamping):
    the gap is purely positional, so a marker word aimed at a *different* issue
    that happens to land adjacent to ``#n`` — "close #1640 (deferred from
    #1600)" — also reads as marking #1640. That under-stamps (the title ref
    falls through to the body, which still ships on a genuine ``Closes #n``)
    rather than risk a false ship.
    """
    if not n:
        return False
    try:
        ref = _ref_pattern(n)
    except (ValueError, TypeError):
        return False
    text = text or ''
    # The leading form allows an optional ``for`` connector ("prep for #n") on top
    # of the bare adjacency ("defers #n", "prep #n"); the connector is the only
    # word permitted in the gap, so "prep the sun bake, then land #n" never binds.
    trailing = ref + _MARKER_GAP + r'\b(?:' + _NONSHIP_MARKER + r')\b'
    leading = (r'\b(?:' + _NONSHIP_MARKER + r')\b(?:\s+for)?'
               + _MARKER_GAP + ref)
    return bool(re.search(trailing, text, re.IGNORECASE)
                or re.search(leading, text, re.IGNORECASE))


def pr_references_issue(title, body, n):
    """True iff the PR genuinely ships issue ``n`` (see module docstring).

    Title: any word-boundary ``#n`` counts (the ``#N: <desc>`` PR-naming
    convention) UNLESS it is a range endpoint (``#1602-#1612`` — a planning PR
    naming filed children), the whole title is a plan/design-doc title
    (``docs: plan …`` / ``docs/design: …`` / ``docs: design …`` — a PR that plans/designs ``n``,
    never ships it), the ref is marked deferred ("(#n deferred)" / "defers #n"
    — layer 6, a doc-and-defer PR that escalates ``n`` rather than shipping it),
    OR the ref is marked prep ("(#n prep)" / "prep for #n" — layer 7, a narrowed
    refactor that prepares ``n`` rather than shipping it).
    Body: ``#n`` counts only when immediately preceded by a
    closing-action verb, and likewise never as a range endpoint. A bare body
    mention ("downstream #n", "pre-existing #n", "Refs #n") is rejected, as is
    a closing verb quoted inside a markdown code span (layer 5): code spans are
    stripped before the body scan so ``... + `Closes #n` `` in a plan PR's body
    does not read as a ship.
    """
    if not n:
        return False
    try:
        ref = _ref_pattern(n)
    except (ValueError, TypeError):
        return False
    title = title or ''
    # Title-trust (the ``#N: <desc>`` PR-naming convention) — UNLESS the title
    # is a plan/design-doc title (layer 4): a plan/design PR names the issue it
    # plans, not one it ships, so its title ref falls through to the body
    # closing-verb check below (where a genuine doc-ship still says ``Closes #N``).
    # Layers 6-7: a title ref marked deferred ("(#N deferred)") or prep
    # ("(#N prep)") is an explicit non-ship, so it is likewise not trusted and
    # falls through to the body.
    if (not _PLAN_DOC_TITLE.search(title)
            and re.search(ref, title)
            and not _ref_is_nonship_marked(title, n)):
        return True
    body_re = re.compile(
        r'\b(?:' + _CLOSING_VERB + r')\b' + _VERB_TO_REF_GAP + ref,
        re.IGNORECASE,
    )
    return bool(body_re.search(_strip_code_spans(body or '')))


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
