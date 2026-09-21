"""Genuine-ship predicate for fleet-queue-ingest's scope-shipped pre-flight.

A merged PR is only evidence that an issue's scope "already landed" when the PR
genuinely *ships* the issue — not when it merely names it. Every layer below
rejects one shape of incidental match:

1. No reference at all. GitHub's PR search (``gh pr list --search '#N'``)
   strips the ``#`` and matches the bare token ``N`` anywhere it appears — PR
   comments, source line numbers, even relevance-only hits with no literal
   ``N``. Trusting ``prs[0]`` mislabels unrelated issues through a line-number
   or relevance hit.

2. Mentioned but not shipped. A bare word-boundary ``#N`` in a PR *body* is
   still not proof: PRs routinely cite issues they explicitly do NOT fix —
   "bug-fixing is downstream issues (#N, etc)", "pre-existing #N" /
   "filed as #N", "Refs #N". The literal ``#N`` token is present in all of
   them, so a token-presence check alone accepts them.

3. Range endpoint, not implemented. An epic-planning PR names the children it
   *files* as a range in its title — "docs: re-plan ... — file children
   #A-#B". Title-trust would match the two range *endpoints* (the middle
   children are untouched because only the endpoints are literally written
   with a ``#``). A ``#N`` that is an endpoint of a ``#A-#B`` range is
   enumerating issues, never shipping one, so range endpoints are rejected in
   both title and body.

4. Plan/design-doc title. A plan or design PR is titled after the issue it
   *plans/designs*, not one it ships: "docs: plan rotation-profiling task
   (#N)", "docs: plan #N … (#A/#B/#C)", "docs/design: … (#N)". Title-trust
   would read the planned issue — or a filed child named with a ``/``
   separator, which the layer-3 dash guard doesn't cover — as shipped. A
   ``docs: (re-)plan`` / ``docs/design:`` title commits the plan, never the
   implementation, so its title ref is NOT trusted: the issue ships only if a
   body closing-verb says so (a doc PR whose deliverable genuinely IS the doc
   change still ships via ``Closes #N`` in the body).

5. Closing verb inside a code span. Layer 4 stops a plan-doc *title* from
   shipping, but a plan PR's *body* routinely quotes the closing verb the
   future implementation PR will use: "the structured plan for #N (planning
   step output; impl PR will carry the code + ``Closes #N``)". That
   ``Closes #N`` sits in an inline code span — it is *showing* the literal text
   the impl PR will write, not performing a close — so without a strip the
   layer-4 title guard fires while the body closing-verb check still matches.
   Markdown code spans (fenced ```` ``` ```` blocks and inline `` `…` `` spans)
   are therefore stripped from the body before the closing-verb scan: a verb
   quoted in code is documentation, never an action. A genuine ship writes
   ``Closes #N`` as prose, never in backticks, so the strip costs no true
   positive.

6. Deferral marker. A PR may name ``#N`` in a trusted (non-plan) title only to
   mark it *deferred*: "render: doc the ... invariant (#N deferred)" is a
   doc-only, design-blocked PR that documents the gap and escalates the fix —
   it does NOT land #N's scope. Because it is titled ``render:`` (not
   ``docs:``) the layer-4 plan-doc guard does not fire, and the bare ``#N`` in
   the title satisfies title-trust; ingest would stamp
   ``fleet:scope-shipped`` on #N and re-stamp it every pass after a human
   removes the label. A ``#N`` carrying an adjacent deferral word ("(#N
   deferred)", "#N — deferred", "defers #N") is an explicit non-ship signal, so
   a deferral-marked title ref is NOT trusted (the deferral word must sit
   directly on one side of the ref, within a bounded gap like the range/verb
   guards, so "fix #A and defer #B" still ships #A).

7. Prep / partial marker. A PR may name ``#N`` in a trusted (non-plan) title
   only to mark it *preparatory* — a narrowed refactor that lands groundwork
   for #N without shipping its scope: "render: extract
   sunBakeFrustumUVBounds shared helper (#N prep)" is linked ``Part of``, not
   ``Closes``. Because it is titled ``render:`` (not ``docs:``) the layer-4
   plan-doc guard does not fire, and the bare ``#N`` in the title satisfies
   title-trust; ingest would stamp ``fleet:scope-shipped`` on #N and clobber
   the issue's re-queue the instant its plan clears review. A ``#N`` carrying
   an adjacent prep word ("(#N prep)", "prep for #N", "preparatory #N") is an
   explicit not-yet-shipped signal, so a prep-marked title ref is NOT trusted
   — same bounded-gap rule as layer 6's deferral marker (the prep word must
   sit directly on one side of the ref, so "ship #A, prep #B" still ships #A).

8. Bookkeeping diff — an all-``.fleet/`` PR. Layers 1-7 read the PR's
   title/body text; this one reads its *diff*. An epic-steward bookkeeping PR —
   a ledger rollup, a plan adoption, a projection edit — is titled *about* the
   issues it accounts for ("docs/fleet: epic-steward — #A rollup + #B adoption
   (#C)"), but its diff is entirely ``.fleet/`` files. Its subject is neither
   ``plan`` nor ``design``, so the layer-4 guard does not fire, and the bare
   ``#B`` in the title satisfies title-trust; ingest would stamp
   ``fleet:scope-shipped`` on an issue with no impl PR anywhere, and the
   reconcile loop would then bounce it to ``fleet:needs-plan``. A PR whose
   changed files are *all* under ``.fleet/`` maintains fleet state and never
   ships an issue's code scope, so its title ref is NOT trusted: like layers
   4/6/7 it falls through to the body closing-verb check, where a
   ``.fleet/``-only PR whose deliverable genuinely IS the fleet change still
   ships via a prose ``Closes #N``. The file list is optional — a caller that
   does not supply it gets title-trust without the diff check.

9. Documentation diff — an all-documentation PR. A PR can use an
   implementation scope in its title while only documenting verification or
   an outstanding phase: "render: GL Phase-0 verification ... (#N)" changing
   one file under ``docs/`` and deliberately omitting ``Closes #N`` because a
   later phase remains. ``docs/`` sits outside layer 8's bookkeeping prefix,
   so layer 8 alone would let title-trust strand the issue. A diff made
   entirely from the union of ``.fleet/`` and ``docs/`` paths ships no
   implementation artifact and therefore withholds title-trust. It still falls
   through to the body closing-verb check, preserving documentation issues that
   genuinely ship via prose ``Closes #N``. Repo-root Markdown is deliberately
   excluded: only directory-scoped documentation paths participate. A
   screenshots-only PR also withholds title-trust, as intended.

So a body ``#N`` counts only when a closing-action verb sits directly before it
in prose (``Closes #N``, ``fixes (#N)``, ``supersedes #N``) — code spans are
stripped first (layer 5), so a verb quoted in backticks does not count. A
``#N`` in the *title* is trusted as-is — PRs in this repo are named
``#N: <desc>`` after the issue they implement — EXCEPT (layer 3) when it is a
range endpoint, (layer 4) when the title is a plan/design-doc title, (layer 6)
when the ref is marked deferred, (layer 7) when the ref is marked prep, or
(layers 8-9) when the PR's diff contains only ``.fleet/`` and ``docs/`` paths:
all six
shapes name issues they enumerate, plan, defer, prepare, account for, or document
without implementing.
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
# purpose: a permissive ".*?" would let "Fixes #A. Also see downstream #B"
# bind the verb across to the unrelated #B.
_VERB_TO_REF_GAP = r'[\s:]*[(\[]?\s*'


# Range dashes (hyphen-minus, en dash, em dash) — the separators an epic-planning
# PR uses to name a span of filed children in its title: "file children #A-#B".
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


def strip_code_spans(s):
    """Blank fenced blocks and inline code spans — quoted grammar never counts."""
    return _INLINE_CODE.sub(' ', _FENCED_CODE.sub(' ', s))


def _ref_pattern(n):
    # ``(?<!\w)`` rejects ``abc#N``; ``(?!\d)`` stops ``#N0`` / ``#MN`` from
    # satisfying ``n=N``. The two range guards reject a ``#N`` that is an
    # endpoint of a ``#A-#B`` range (an epic-planning PR enumerating the children
    # it FILES, not implementing one): the ``(?<!RANGE_DASH)`` lookbehind drops a
    # range END (``...-#B``) and the trailing ``(?!\s*RANGE_DASH\s*#?\d)``
    # lookahead drops a range START (``#A`` directly before ``-#B``).
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
# to an *adjacent* ref: "fix #A and defer #B" leaves #A shippable.
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
    that happens to land adjacent to ``#n`` — "close #n (deferred from
    #m)" — also reads as marking #n. That under-stamps (the title ref
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


# Fleet-internal bookkeeping prefix (layer 8). A merged PR whose changed files
# are ALL under ``.fleet/`` — an epic-steward ledger rollup, a plan adoption, a
# projection edit — maintains fleet state; it never ships an issue's code scope.
_FLEET_BOOKKEEPING_PREFIX = '.fleet/'
_DOCUMENTATION_PREFIX = 'docs/'
_NON_SHIPPING_PREFIXES = (_FLEET_BOOKKEEPING_PREFIX, _DOCUMENTATION_PREFIX)


def _is_non_shipping_diff(files):
    """True iff every changed path is fleet bookkeeping or documentation.

    Such a PR ships no implementation artifact (layers 8-9). Root Markdown is
    deliberately outside the directory-scoped prefix set.

    ``files`` is the ``gh pr list --json files`` shape (a list of
    ``{'path': ...}`` dicts); a plain list of path strings is accepted too.
    Returns False when ``files`` is falsy/empty (no diff info supplied) so a
    caller that omits the file list keeps the earlier title-trust behavior.
    """
    if not files:
        return False
    paths = []
    for f in files:
        p = (f.get('path') if isinstance(f, dict) else f) or ''
        p = p.strip()
        if p:
            paths.append(p)
    if not paths:
        return False
    return all(p.startswith(_NON_SHIPPING_PREFIXES) for p in paths)


def pr_references_issue(title, body, n, files=None):
    """True iff the PR genuinely ships issue ``n`` (see module docstring).

    Title: any word-boundary ``#n`` counts (the ``#N: <desc>`` PR-naming
    convention) UNLESS it is a range endpoint (``#A-#B`` — a planning PR
    naming filed children), the whole title is a plan/design-doc title
    (``docs: plan …`` / ``docs/design: …`` / ``docs: design …`` — a PR that plans/designs ``n``,
    never ships it), the ref is marked deferred ("(#n deferred)" / "defers #n"
    — layer 6, a doc-and-defer PR that escalates ``n`` rather than shipping it),
    OR the ref is marked prep ("(#n prep)" / "prep for #n" — layer 7, a narrowed
    refactor that prepares ``n`` rather than shipping it), OR (layers 8-9) the
    PR's ``files`` diff contains only ``.fleet/`` bookkeeping and ``docs/``
    documentation paths (it accounts for or documents ``n`` rather than
    implementing it).
    Body: ``#n`` counts only when immediately preceded by a
    closing-action verb, and likewise never as a range endpoint. A bare body
    mention ("downstream #n", "pre-existing #n", "Refs #n") is rejected, as is
    a closing verb quoted inside a markdown code span (layer 5): code spans are
    stripped before the body scan so ``... + `Closes #n` `` in a plan PR's body
    does not read as a ship.

    ``files`` is the optional ``gh pr list --json files`` list for this PR; when
    omitted the non-shipping-diff guard is inert and title-trust is
    unchanged.
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
    # falls through to the body. Layers 8-9: a PR whose diff contains only fleet
    # bookkeeping and documentation paths accounts for or documents the issue
    # without implementing it, so its title ref falls through too.
    if (not _PLAN_DOC_TITLE.search(title)
            and re.search(ref, title)
            and not _ref_is_nonship_marked(title, n)
            and not _is_non_shipping_diff(files)):
        return True
    body_re = re.compile(
        r'\b(?:' + _CLOSING_VERB + r')\b' + _VERB_TO_REF_GAP + ref,
        re.IGNORECASE,
    )
    return bool(body_re.search(strip_code_spans(body or '')))


def select_shipped_pr(prs, n):
    """First PR in ``prs`` that genuinely ships issue ``n``, else ``None``.

    Replaces the old ``prs[0]`` blind trust: a merged-PR search hit only counts
    as scope-shipped evidence when ``pr_references_issue`` confirms a genuine
    ship (title ref or closing-verb body ref), not an incidental mention. The
    PR's ``files`` list (from ``gh pr list --json files``) feeds the layers 8-9
    non-shipping-diff guard; a candidate without it falls back to text-only.
    """
    for pr in prs:
        if pr_references_issue(pr.get('title', ''), pr.get('body', ''), n,
                               pr.get('files')):
            return pr
    return None
