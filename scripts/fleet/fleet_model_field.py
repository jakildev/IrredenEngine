"""Shared ``Model:`` field parsing for fleet routing.

The validator, scout, queue minters, and claim gates all resolve the same
declared task class.  Keeping the grammar here prevents a decorated value from
passing one lane while defaulting or routing differently in another (#2833).

The first class token in the first matching field value is authoritative.
The field may be a list item, use the ``Suggested Model`` alias, carry
backtick/bracket decoration, or appear after other metadata on the same line.
Later class names are qualifier prose, not competing declarations.  An absent
field or a value without a class token resolves to ``None`` so each consumer
can preserve its own documented default.
"""
import re

MODEL_CLASSES = ("fable", "opus", "sonnet")

_MODEL_FIELD_RE = re.compile(
    r"\*\*(?:Suggested\s+)?Model:\*\*\s*(.+?)(?:\n|$)",
    re.IGNORECASE,
)
_MODEL_TOKEN_RE = re.compile(
    rf"\b({'|'.join(MODEL_CLASSES)})\b",
    re.IGNORECASE,
)


def model_field_value(body):
    """Return the first model field's raw value, or ``None`` when absent."""
    match = _MODEL_FIELD_RE.search(body or "")
    return match.group(1).strip() if match else None


def class_tokens(body):
    """Return model-class tokens from the first field value, in order."""
    value = model_field_value(body)
    if value is None:
        return []
    return [match.lower() for match in _MODEL_TOKEN_RE.findall(value)]


def declared_class(body):
    """Return the first positional class token, or ``None`` if unresolved."""
    value = model_field_value(body)
    match = _MODEL_TOKEN_RE.search(value or "")
    return match.group(1).lower() if match else None
