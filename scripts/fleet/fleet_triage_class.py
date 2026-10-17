"""Graduated triage classes: the verdicts an agent may stamp as
fleet:agent-approved, and the bar each one must cite.

triage-protocol.md § Graduation names the classes; this module is the one
judge of whether a staged entry or a `## Triage` comment meets its class's
bar. fleet-triage-sweep (apply and stamp) is the only writer and calls
check() before any approval label leaves it. Direction-shaped work has no
class here and stays recommend-only.
"""

import re

import fleet_model_field

AGENT_APPROVED = "fleet:agent-approved"
RECOMMEND = "fleet:triage-recommend"
DIRECTION = "direction"
MODELS = fleet_model_field.MODEL_CLASSES
FILERS = ("architect", "steward")

# class -> (required bar keys, the bar in one line)
CLASSES = {
    "rebless": (
        ("measurement", "host", "confinement"),
        "a measured table on the named host and a confinement rule in the criteria"),
    "docs-capture": (
        ("ruling", "grep"),
        "the posted ruling it captures and a grep showing the doc lacks it on master"),
    "plan-residual": (
        ("plan", "decision"),
        "the approved ## Plan comment and the decision line that carves it out"),
    "verification-run": (
        ("criteria", "host", "no_plan"),
        "runnable criteria, a named host, and fleet:no-plan"),
    "objective-row": (
        ("objective", "row", "filer"),
        "the active objective file, the row verbatim, filed by the architect or the steward"),
}
GRADUATED = tuple(CLASSES)
NEGATIVE = ("no", "false", "0", "none")


def check(class_name, bar):
    """(ok, reason) for stamping fleet:agent-approved on an entry of
    `class_name` citing `bar`, a mapping of bar key to citation."""
    if class_name == DIRECTION:
        return False, "direction-shaped work stays recommend-only"
    if class_name not in CLASSES:
        return False, (f"unknown class {class_name!r} "
                       f"(graduated: {', '.join(GRADUATED)}; else {DIRECTION})")
    keys, statement = CLASSES[class_name]
    bar = bar if isinstance(bar, dict) else {}
    missing = [key for key in keys if not str(bar.get(key) or "").strip()]
    if missing:
        return False, (f"{class_name} bar incomplete: missing {', '.join(missing)} "
                       f"({statement})")
    if class_name == "objective-row" and str(bar["filer"]).strip().lower() not in FILERS:
        return False, (f"objective-row filer must be one of {', '.join(FILERS)}, "
                       f"got {bar['filer']!r}")
    if class_name == "verification-run" and str(bar["no_plan"]).strip().lower() in NEGATIVE:
        return False, "verification-run needs fleet:no-plan"
    return True, f"{class_name}: every bar line cited"


def labels_for(model):
    """The label set a graduated stamp writes."""
    if model not in MODELS:
        raise ValueError(f"model must be one of {', '.join(MODELS)}, got {model!r}")
    return [AGENT_APPROVED, f"fleet:{model}"]


_FIELD_RE = re.compile(r"^\*\*(?P<key>[A-Za-z ]+):\*\*\s*(?P<value>.*)$")
_BAR_LINE_RE = re.compile(r"^\s*[-*]\s*(?P<key>[a-z_]+)\s*:\s*(?P<value>.+)$")
# One class name, decorated at most with backticks, brackets or asterisks.
# Prose beside the name refuses: fleet_model_field.declared_class would take
# the first token out of "sonnet — if the owner agrees", a stamp nobody made.
_MODEL_VALUE_RE = re.compile(rf"[`*\[\]\s]*({'|'.join(MODELS)})[`*\[\]\s]*", re.IGNORECASE)


def model_of(value):
    """The class a suggested-model value names, or None."""
    match = _MODEL_VALUE_RE.fullmatch(value or "")
    return match.group(1).lower() if match else None


def parse_triage_comment(text):
    """The fields of a `## Triage` comment: verdict, class, model, objective,
    basis (first line) and the bar mapping. A field the comment lacks is an
    absent key; `bar` is always present. The model field is read through
    fleet_model_field, the grammar every routing lane shares."""
    fields, bar, in_bar = {}, {}, False
    for raw in text.splitlines():
        line = raw.rstrip()
        field = _FIELD_RE.match(line)
        if field:
            key = field.group("key").strip().lower()
            in_bar = key == "bar"
            if not in_bar:
                fields[key] = field.group("value").strip()
            continue
        if not in_bar:
            continue
        bar_line = _BAR_LINE_RE.match(line)
        if bar_line:
            bar[bar_line.group("key")] = bar_line.group("value").strip()
        elif line.strip():
            in_bar = False
    fields["bar"] = bar
    model = fleet_model_field.model_field_value(text)
    if model is not None:
        fields["model"] = model
    return fields


def decide(fields):
    """(labels, reason) for a parsed comment: the graduated stamp when the
    verdict is recommend-approve and the class meets its bar, else the
    recommend label."""
    verdict = fields.get("verdict", "").strip()
    if not verdict.startswith("recommend-approve"):
        return [RECOMMEND], f"verdict {verdict or '(none)'!r} is not recommend-approve"
    ok, reason = check(fields.get("class", DIRECTION).strip().lower(), fields.get("bar"))
    if not ok:
        return [RECOMMEND], reason
    model = model_of(fields.get("model"))
    if model is None:
        return [RECOMMEND], (f"suggested model {fields.get('model') or '(none)'!r} is not "
                             f"exactly one of {', '.join(MODELS)}")
    return labels_for(model), reason
