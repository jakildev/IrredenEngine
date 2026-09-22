"""Shared epic-membership grammar for fleet child discovery.

The validator, ``fleet-epic-status`` and the scout's epic-steward adoption
scan all decide "is issue X a child of umbrella #N" from the same body line.
Keeping the grammar here prevents one consumer from seeing a child another
drops — the unsynchronized-parser class ``fleet_model_field`` exists for.

Two readings of the same lines:

- ``epic_refs`` — the tolerant discovery grammar. Every field-shaped spelling
  the issue population actually uses is membership, so a variant-spelled
  child is found (and then flagged by the validator) instead of skipped.
- ``canonical_epic_refs`` — only the standalone ``**Part of epic:** #N`` line
  the ``file-epic`` template emits and the validator requires.

A membership line is line-anchored after optional indentation and an optional
``-`` / ``*`` / ``+`` bullet. Its label is one of ``Part of epic``,
``Part of``, ``Parent epic``, ``Parent``, ``Epic umbrella`` or ``Epic``
(case-insensitive, bold markers optional around label and colon), followed by
a colon and a value. The word ``epic`` must appear in the label or as the
value's leading word, so ``**Part of:** #N`` alone is not membership. The
colon-less line-leading ``**Part of epic #N**`` is accepted too.

Only the value's leading ref list counts: ``#N`` items joined by ``,`` ``/``
``&`` or ``and``, each optionally followed by one parenthetical. The list ends
at the first other token (``·``, ``—``, ``;``, prose), and a ref inside a
parenthetical is never a member — ``#604 · Umbrella: #213`` names only #604,
``#1881 (via sub-epic #1884)`` only #1881.
"""
import re

_LEAD = r"^[ \t]*(?:[-*+][ \t]+)?"

_FIELD_RE = re.compile(
    _LEAD
    + r"(?:\*\*)?(?P<label>part of epic|part of|parent epic|parent"
      r"|epic umbrella|epic)(?:\*\*)?[ \t]*:(?:\*\*)?[ \t]*(?P<value>.*)$",
    re.IGNORECASE | re.MULTILINE,
)
_COLONLESS_RE = re.compile(
    _LEAD + r"\*\*part of epic[ \t]+(?P<value>#[^*\n]*)\*\*",
    re.IGNORECASE | re.MULTILINE,
)
_CANONICAL_RE = re.compile(r"^\*\*Part of epic:\*\*[ \t]*(?P<value>.*)$",
                           re.MULTILINE)

_EPIC_WORD_RE = re.compile(r"epic\b[ \t]*", re.IGNORECASE)
_REF_ITEM_RE = re.compile(r"#(\d+)(?!\d)[ \t]*(?:\([^()\n]*\)[ \t]*)?")
_REF_SEP_RE = re.compile(r"(?:,|/|&|and\b)[ \t]*", re.IGNORECASE)


def _norm(body):
    """Collapse CRLF/CR so ``^``/``$`` anchors land on real line breaks."""
    return (body or "").replace("\r\n", "\n").replace("\r", "\n")


def _leading_refs(value):
    refs = []
    pos = 0
    while True:
        item = _REF_ITEM_RE.match(value, pos)
        if item is None:
            break
        refs.append(int(item.group(1)))
        sep = _REF_SEP_RE.match(value, item.end())
        if sep is None:
            break
        pos = sep.end()
    return refs


def membership_lines(body):
    """``[(line, refs)]`` for every membership line, in body order.

    ``line`` is the stripped source line (for quoting in a finding); ``refs``
    is its leading ref list, never empty.
    """
    text = _norm(body)
    found = []
    for m in _FIELD_RE.finditer(text):
        value = m.group("value")
        epic_word = _EPIC_WORD_RE.match(value)
        if epic_word:
            value = value[epic_word.end():]
        elif "epic" not in m.group("label").lower():
            continue
        refs = _leading_refs(value)
        if refs:
            found.append((m.start(), m.group(0).strip(), refs))
    for m in _COLONLESS_RE.finditer(text):
        refs = _leading_refs(m.group("value"))
        if refs:
            line_end = text.find("\n", m.start())
            line = text[m.start():line_end if line_end >= 0 else len(text)]
            found.append((m.start(), line.strip(), refs))
    found.sort(key=lambda f: f[0])
    return [(line, refs) for _pos, line, refs in found]


def epic_refs(body):
    """Sorted, deduped umbrella numbers ``body`` declares membership in."""
    return sorted({n for _line, refs in membership_lines(body) for n in refs})


def canonical_epic_refs(body):
    """Umbrellas named by a standalone ``**Part of epic:** #N`` line only."""
    return sorted({n for m in _CANONICAL_RE.finditer(_norm(body))
                   for n in _leading_refs(m.group("value"))})
