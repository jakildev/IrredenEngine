"""The ingest hold family, DERIVED from fleet-queue-ingest, against the scout.

A label that holds an approved issue out of fleet rotation while it keeps
`human:approved` has to be honored on four surfaces, not on the one the
symptom points at:

  - `fleet-queue-ingest`'s per-issue loop, so `fleet:queued` is never stamped;
  - `_INGEST_SKIP_LABELS`, or the issue never leaves the ingest membership set
    and the human REMOVING the label is not a membership change — no hash
    flip, no ingest fire, and a live `gh issue view` every tick meanwhile;
  - `_ALREADY_QUEUED_LABELS`, or `fetch_human_approved` returns it forever;
  - `fetch_task_queue`, or a park landing on an ALREADY queued row leaves it
    pickable and the dispatcher re-claims it every tick.

The family is not written down here. A declared list would pass green on a
new park label added to zero surfaces AND omitted from the list, which is the
same membership blindness. Instead it is derived from the ingest's own source:
every string label tested by an `if` whose body bumps the hold counter. So the
only way to add a park the fleet honors nowhere is to not add it to the ingest
either, where it does nothing at all.

The derivation is fail-closed: a missing heredoc, an unparseable body, no hold
arm, or an empty family is an error, never a silent pass.

`_ALREADY_QUEUED_EXEMPT_LABELS` is the one sanctioned gap, asserted to be a
subset of the family so a retired label cannot linger in it.
"""
import ast
import importlib.machinery
import importlib.util
import re
import unittest
from pathlib import Path

_FLEET_DIR = Path(__file__).parent.parent
_INGEST = _FLEET_DIR / "fleet-queue-ingest"
_SCOUT = _FLEET_DIR / "fleet-state-scout"

# The per-issue stamping pass is a quoted heredoc, so its body is plain Python
# with no shell interpolation and `ast.parse` reads it directly.
_HEREDOC_RE = re.compile(r"<<\s*'STAMP_PY'[^\n]*\n(.*?)\nSTAMP_PY\n", re.S)

# The counter every hold arm bumps. It is the hold's only structural marker —
# the arms share no other shape — so losing it must fail loudly (below).
_HOLD_COUNTER = "skipped_already"

# Every label-keyed `continue` arm in the per-issue loop: the holds that bump
# the counter above plus the reconcile arms (override, plan-gate, unblock,
# retract) that do not. The counter is the only discriminator between the two,
# so an arm that skips under a different counter would shrink the derived
# family silently; this count forces the classification.
_LABEL_KEYED_ARMS = 7


class FamilyDerivationError(Exception):
    """The hold family could not be read out of the ingest source."""


def _module_label_constants(tree):
    """Module-scope names bound to a literal collection of label strings.

    Lets the walker read a hold arm written as `if labels & HOLD_LABELS:` as
    well as today's chained `or`. Scoped to module level so a local list of
    command arguments inside the loop can never be mistaken for a label set.
    """
    constants = {}
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        value = node.value
        if (isinstance(value, ast.Call) and isinstance(value.func, ast.Name)
                and value.func.id in ("frozenset", "set") and value.args):
            value = value.args[0]
        if not isinstance(value, (ast.Set, ast.List, ast.Tuple)):
            continue
        labels = {e.value for e in value.elts
                  if isinstance(e, ast.Constant) and _is_label(e.value)}
        if not labels:
            continue
        for target in node.targets:
            if isinstance(target, ast.Name):
                constants[target.id] = labels
    return constants


def _is_label(value):
    """Label shape: `<namespace>:<name>`. Keeps a non-label string in a hold
    arm's test (a state name, a repo slug) out of the family."""
    return (isinstance(value, str)
            and re.fullmatch(r"[a-z]+:[a-z0-9][a-z0-9:-]*", value) is not None)


def _parse_stamp_body(ingest_text):
    match = _HEREDOC_RE.search(ingest_text)
    if not match:
        raise FamilyDerivationError(
            "no quoted STAMP_PY heredoc in fleet-queue-ingest; the hold family "
            "cannot be derived and no membership below is being checked")
    try:
        return ast.parse(match.group(1))
    except SyntaxError as exc:
        raise FamilyDerivationError(
            f"the STAMP_PY heredoc body does not parse as Python: {exc}") from exc


def label_keyed_continue_arms(ingest_text):
    """Line numbers of every `continue`-bearing `if` keyed on `labels`."""
    lines = []
    for node in ast.walk(_parse_stamp_body(ingest_text)):
        if not isinstance(node, ast.If):
            continue
        if not any(isinstance(sub, ast.Continue)
                   for stmt in node.body for sub in ast.walk(stmt)):
            continue
        if any(isinstance(sub, ast.Name) and sub.id == "labels"
               for sub in ast.walk(node.test)):
            lines.append(node.lineno)
    return sorted(lines)


def derive_park_family(ingest_text):
    """The labels fleet-queue-ingest holds an approved issue on."""
    tree = _parse_stamp_body(ingest_text)
    constants = _module_label_constants(tree)
    family = set()
    arms = 0
    for node in ast.walk(tree):
        if not isinstance(node, ast.If):
            continue
        bumps_hold = any(
            isinstance(stmt, ast.AugAssign)
            and isinstance(stmt.target, ast.Name)
            and stmt.target.id == _HOLD_COUNTER
            and isinstance(stmt.op, ast.Add)
            for stmt in node.body)
        if not bumps_hold:
            continue
        arms += 1
        for sub in ast.walk(node.test):
            if isinstance(sub, ast.Constant) and _is_label(sub.value):
                family.add(sub.value)
            elif isinstance(sub, ast.Name) and sub.id in constants:
                family |= constants[sub.id]

    if not arms:
        raise FamilyDerivationError(
            f"no `if` in the STAMP_PY per-issue loop bumps `{_HOLD_COUNTER}`; "
            "the hold arms were renamed or restructured and this suite is "
            "checking nothing")
    if not family:
        raise FamilyDerivationError(
            f"{arms} hold arm(s) found but no label string in any of their "
            "tests; the family is empty and every membership check below "
            "would pass vacuously")
    return frozenset(family)


def _load_scout():
    loader = importlib.machinery.SourceFileLoader("fleet_state_scout", str(_SCOUT))
    spec = importlib.util.spec_from_loader("fleet_state_scout", loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


_scout = _load_scout()
_FAMILY = derive_park_family(_INGEST.read_text())

_PROBE_ISSUE = 9001


def _fetch_task_queue(labels):
    """fetch_task_queue over a single `fleet:queued` row carrying `labels`."""
    issue = {"number": _PROBE_ISSUE, "title": "probe",
             "body": "**Model:** opus\n**Blocked by:** (none)",
             "labels": [{"name": n} for n in labels],
             "updated_at": "2026-09-20T00:00:00Z"}
    original = _scout._rest_list
    _scout._rest_list = lambda *a, **k: [issue]
    try:
        return _scout.fetch_task_queue("jakildev/IrredenEngine")
    finally:
        _scout._rest_list = original


# ---------------------------------------------------------------------------
# The three invariants, as functions of the family, so a synthetic family can
# be pushed through the same code the live checks use.
# ---------------------------------------------------------------------------

def ingest_skip_gaps(family):
    return sorted(set(family) - set(_scout._INGEST_SKIP_LABELS))


def already_queued_gaps(family):
    return sorted(set(family)
                  - set(_scout._ALREADY_QUEUED_LABELS)
                  - set(_scout._ALREADY_QUEUED_EXEMPT_LABELS))


def task_queue_gaps(family):
    """Members a `fleet:queued` row can still be dispatched or claimed under.

    Behavioral, not a read of the skip constant, so the skip's code shape stays
    free. `fleet:queued` itself is excluded: it is the fetch's own selector, so
    every row here carries it. A member that reaches a section is a gap unless
    it was captured as a retract candidate, which removes `fleet:queued` and
    takes the row out of the fetch's population on the next pass.
    """
    gaps = []
    for label in sorted(set(family) - {"fleet:queued"}):
        sections = _fetch_task_queue(["fleet:queued", "fleet:opus", label])
        reached = [section for section in ("open", "in_progress")
                   if any(task["issue"] == f"#{_PROBE_ISSUE}"
                          for task in sections[section])]
        if reached and _PROBE_ISSUE not in sections["plan_gated"]:
            gaps.append(f"{label} (reached tasks.{'/'.join(reached)}, not retracted)")
    return gaps


class Derivation(unittest.TestCase):
    def test_family_is_non_empty_and_label_shaped(self):
        self.assertTrue(_FAMILY)
        for label in _FAMILY:
            self.assertTrue(_is_label(label), f"{label!r} is not label-shaped")

    def test_every_label_keyed_arm_is_classified(self):
        arms = label_keyed_continue_arms(_INGEST.read_text())
        self.assertEqual(
            _LABEL_KEYED_ARMS, len(arms),
            f"label-keyed `continue` arms at STAMP_PY lines {arms}: a new "
            "label-keyed arm was added or one removed. Classify it as a hold "
            f"(bump `{_HOLD_COUNTER}` so it joins the family) or a reconcile "
            "arm, then update _LABEL_KEYED_ARMS")

    def test_queued_is_a_member(self):
        # task_queue_gaps carves fleet:queued out as the fetch's own selector.
        # If the ingest stops holding on it that carve-out is unjustified and
        # the member needs a real check instead.
        self.assertIn("fleet:queued", _FAMILY)


class Membership(unittest.TestCase):
    def test_ingest_skip_labels_covers_the_family(self):
        self.assertEqual(
            [], ingest_skip_gaps(_FAMILY),
            "missing from fleet-state-scout's _INGEST_SKIP_LABELS: an issue "
            "held by the ingest but counted in its membership set never "
            "settles, and removing the label fires no ingest")

    def test_already_queued_labels_covers_the_family(self):
        self.assertEqual(
            [], already_queued_gaps(_FAMILY),
            "missing from fleet-state-scout's _ALREADY_QUEUED_LABELS (and not "
            "exempted): fetch_human_approved returns the parked issue on every "
            "tick, forever")

    def test_exemptions_are_family_members(self):
        self.assertEqual(
            [], sorted(set(_scout._ALREADY_QUEUED_EXEMPT_LABELS) - set(_FAMILY)),
            "_ALREADY_QUEUED_EXEMPT_LABELS names a label the ingest no longer "
            "holds on; a retired label left in the exemption silently widens it")

    def test_fetch_task_queue_drops_the_family(self):
        self.assertEqual(
            [], task_queue_gaps(_FAMILY),
            "still pickable from tasks.open / tasks.in_progress: a park landing "
            "on an already-queued row leaves the dispatcher re-claiming it")


class DerivedFamilySeesANewPark(unittest.TestCase):
    """The reason the family is derived rather than declared: a park label
    added to the ingest and nowhere else fails all three invariants, naming
    itself, with nothing in this file edited."""

    UNKNOWN = "fleet:parked-nowhere"

    def setUp(self):
        self.family = _FAMILY | {self.UNKNOWN}

    def _added(self, check):
        """What the extra member alone adds to a check's findings. Compared
        against the live family so the proof holds whatever else is red."""
        added = set(check(self.family)) - set(check(_FAMILY))
        return sorted(added)

    def test_ingest_skip_invariant_names_it(self):
        self.assertEqual([self.UNKNOWN], self._added(ingest_skip_gaps))

    def test_already_queued_invariant_names_it(self):
        self.assertEqual([self.UNKNOWN], self._added(already_queued_gaps))

    def test_task_queue_invariant_names_it(self):
        added = self._added(task_queue_gaps)
        self.assertEqual(1, len(added), added)
        self.assertTrue(added[0].startswith(self.UNKNOWN), added)


class FailsClosed(unittest.TestCase):
    """A derivation that stops finding the family must fail, never pass."""

    LIVE = _INGEST.read_text()

    def _assert_raises(self, text, fragment):
        with self.assertRaises(FamilyDerivationError) as ctx:
            derive_park_family(text)
        self.assertIn(fragment, str(ctx.exception))

    def test_missing_heredoc(self):
        self._assert_raises(self.LIVE.replace("STAMP_PY", "STAMP_PY_RENAMED"),
                            "no quoted STAMP_PY heredoc")

    def test_renamed_hold_counter(self):
        self._assert_raises(self.LIVE.replace(_HOLD_COUNTER, "held_back"),
                            f"bumps `{_HOLD_COUNTER}`")

    def test_hold_arm_with_no_label(self):
        source = (
            "python3 - << 'STAMP_PY'\n"
            f"{_HOLD_COUNTER} = 0\n"
            "for n in issues:\n"
            "    if n.get('state') == 'OPEN':\n"
            f"        {_HOLD_COUNTER} += 1\n"
            "        continue\n"
            "STAMP_PY\n"
        )
        self._assert_raises(source, "no label string in any of their tests")

    def test_unparseable_body(self):
        source = "python3 - << 'STAMP_PY'\ndef (:\nSTAMP_PY\n"
        self._assert_raises(source, "does not parse as Python")

    def test_named_constant_arm_is_read(self):
        source = (
            "python3 - << 'STAMP_PY'\n"
            "HOLD = frozenset({'fleet:queued', 'fleet:novel-park'})\n"
            f"{_HOLD_COUNTER} = 0\n"
            "for n in issues:\n"
            "    if labels & HOLD:\n"
            f"        {_HOLD_COUNTER} += 1\n"
            "        continue\n"
            "STAMP_PY\n"
        )
        self.assertEqual(frozenset({"fleet:queued", "fleet:novel-park"}),
                         derive_park_family(source))


if __name__ == "__main__":
    unittest.main()
