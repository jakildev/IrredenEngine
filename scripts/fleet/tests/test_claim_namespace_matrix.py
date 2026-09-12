"""Coverage ratchet for disjoint fleet claim-label namespaces.

The family inventory comes from executable fleet-claim entrypoints. Every
ordered pair is then either proved on the scout and claim-command surfaces or
accounted for by one exact policy exception with an exercised support fixture.
"""

from __future__ import annotations

import importlib.machinery
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from dataclasses import dataclass
from pathlib import Path

FLEET_DIR = Path(__file__).parent.parent
CLAIM_PATH = FLEET_DIR / "fleet-claim"
SCOUT_PATH = FLEET_DIR / "fleet-state-scout"
COMMON_PATH = FLEET_DIR / "fleet-common.sh"
SURFACES = ("scout", "claim")


@dataclass(frozen=True)
class Family:
    name: str
    prefix: str
    command: str
    function: str


@dataclass(frozen=True)
class ExceptionRecord:
    support: str
    reason: str


def _exception(support: str, reason: str) -> ExceptionRecord:
    return ExceptionRecord(support=support, reason=reason)


ISSUE_SCOPE = _exception(
    "issue_scope",
    "Planning/stewarding are issue-only locks, outside the PR-head mutation contract.",
)
FEEDBACK_FIRST = _exception(
    "feedback_first",
    "Feedback precedes conflict resolution via fleet verdict state, not claim prefixes (#2423).",
)
CONFLICT_PHASE = _exception(
    "conflict_phase",
    "Review stands down on semantic-conflict until the resolver push clears that state (#3001).",
)
HUMAN_PRIORITY = _exception(
    "human_priority",
    "Human feedback outranks a live fleet review on the scout surface (#2801).",
)


# Exact cells only: adding a family creates uncovered cells rather than
# silently inheriting a wildcard/default exemption.
EXCEPTIONS = {
    ("amending", "resolving", "scout"): FEEDBACK_FIRST,
    ("amending", "resolving", "claim"): FEEDBACK_FIRST,
    ("resolving", "amending", "scout"): FEEDBACK_FIRST,
    ("resolving", "amending", "claim"): FEEDBACK_FIRST,
    ("reviewing", "resolving", "scout"): CONFLICT_PHASE,
    ("reviewing", "resolving", "claim"): CONFLICT_PHASE,
    ("amending", "stewarding", "scout"): ISSUE_SCOPE,
    ("amending", "stewarding", "claim"): ISSUE_SCOPE,
    ("amending", "planning", "scout"): ISSUE_SCOPE,
    ("amending", "planning", "claim"): ISSUE_SCOPE,
    ("resolving", "stewarding", "scout"): ISSUE_SCOPE,
    ("resolving", "stewarding", "claim"): ISSUE_SCOPE,
    ("resolving", "planning", "scout"): ISSUE_SCOPE,
    ("resolving", "planning", "claim"): ISSUE_SCOPE,
    ("reviewing", "stewarding", "scout"): ISSUE_SCOPE,
    ("reviewing", "stewarding", "claim"): ISSUE_SCOPE,
    ("reviewing", "planning", "scout"): ISSUE_SCOPE,
    ("reviewing", "planning", "claim"): ISSUE_SCOPE,
    ("stewarding", "amending", "scout"): ISSUE_SCOPE,
    ("stewarding", "amending", "claim"): ISSUE_SCOPE,
    ("stewarding", "resolving", "scout"): ISSUE_SCOPE,
    ("stewarding", "resolving", "claim"): ISSUE_SCOPE,
    ("stewarding", "reviewing", "scout"): ISSUE_SCOPE,
    ("stewarding", "reviewing", "claim"): ISSUE_SCOPE,
    ("stewarding", "planning", "scout"): ISSUE_SCOPE,
    ("stewarding", "planning", "claim"): ISSUE_SCOPE,
    ("planning", "amending", "scout"): ISSUE_SCOPE,
    ("planning", "amending", "claim"): ISSUE_SCOPE,
    ("planning", "resolving", "scout"): ISSUE_SCOPE,
    ("planning", "resolving", "claim"): ISSUE_SCOPE,
    ("planning", "reviewing", "scout"): ISSUE_SCOPE,
    ("planning", "reviewing", "claim"): ISSUE_SCOPE,
    ("planning", "stewarding", "scout"): ISSUE_SCOPE,
    ("planning", "stewarding", "claim"): ISSUE_SCOPE,
}

# Qualified carve-outs refine a guarded cell. The qualifier prevents the human
# priority rule from exempting fleet-owned feedback tiers.
QUALIFIED_EXCEPTIONS = {
    ("amending", "reviewing", "scout", "human-feedback"): HUMAN_PRIORITY,
}


def derive_families(source: str) -> dict[str, Family]:
    """Derive label families from dispatched cmd_*_claim implementations."""
    starts = list(re.finditer(r"(?m)^cmd_([a-z_]+_claim)\(\)\s*\{", source))
    functions: dict[str, str] = {}
    for index, match in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(source)
        functions[match.group(1)] = source[match.start() : end]

    families: dict[str, Family] = {}
    dispatches = re.finditer(
        r"(?ms)^\s{4}([a-z][a-z-]*-claim)\)\s*$"
        r"(.*?)^\s{8};;\s*$",
        source,
    )
    for dispatch in dispatches:
        command = dispatch.group(1)
        calls = re.findall(r"\bcmd_([a-z_]+_claim)\b", dispatch.group(2))
        if len(set(calls)) != 1:
            raise AssertionError(f"{command}: expected one dispatched cmd_*_claim, found {calls}")
        function = calls[0]
        body = functions.get(function)
        if body is None:
            raise AssertionError(f"{command}: cmd_{function} definition is missing")
        prefixes = re.findall(
            r'_cmd_pr_label_claim\s+"(fleet:[a-z]+-)"\s+"([a-z-]+-claim)"',
            body,
        )
        if len(prefixes) != 1:
            raise AssertionError(
                f"{command}: expected one literal _cmd_pr_label_claim prefix, found {prefixes}"
            )
        prefix, implementation_command = prefixes[0]
        if implementation_command != command:
            raise AssertionError(f"{command}: implementation names {implementation_command}")
        name = prefix.removeprefix("fleet:").removesuffix("-")
        if name in families:
            raise AssertionError(f"ambiguous family prefix: {prefix}")
        families[name] = Family(name, prefix, command, function)

    if not families:
        raise AssertionError("empty claim-family inventory")
    return families


def load_scout(path: Path):
    name = f"claim_matrix_scout_{abs(hash((str(path), path.read_text())))}"
    loader = importlib.machinery.SourceFileLoader(name, str(path))
    spec = importlib.util.spec_from_loader(name, loader)
    module = importlib.util.module_from_spec(spec)
    fleet_path = str(path.parent)
    sys.path.insert(0, fleet_path)
    try:
        loader.exec_module(module)
    finally:
        sys.path.remove(fleet_path)
    return module


def _conflict_pr() -> dict[str, object]:
    return {"mergeable": "CONFLICTING", "baseRefName": "master"}


def scout_guarded(module, candidate: str, held: str) -> bool:
    prefix = f"fleet:{held}-mac-probeB"
    if (candidate, held) == ("reviewing", "amending"):
        return (
            not module._review_skipped({"fleet:changes-made"})
            and module._review_skipped({"fleet:changes-made", prefix})
            and not module._review_skipped({"fleet:changes-made"})
        )
    if (candidate, held) == ("amending", "reviewing"):
        free = module.worker_feedback_labels({"fleet:has-nits"})
        blocked = module.worker_feedback_labels({"fleet:has-nits", prefix})
        reentered = module.worker_feedback_labels({"fleet:has-nits"})
        return bool(free) and not blocked and bool(reentered)
    if (candidate, held) == ("resolving", "reviewing"):
        free = module._semantic_conflict_claimable(_conflict_pr(), {"fleet:semantic-conflict"}, {})
        blocked = module._semantic_conflict_claimable(
            _conflict_pr(), {"fleet:semantic-conflict", prefix}, {}
        )
        reentered = module._semantic_conflict_claimable(
            _conflict_pr(), {"fleet:semantic-conflict"}, {}
        )
        return free and not blocked and reentered
    raise KeyError((candidate, held))


GH_STUB = r"""#!/usr/bin/env python3
import json
import os
import sys
from pathlib import Path

state_path = Path(os.environ["CLAIM_MATRIX_STATE"])
post_path = Path(os.environ["CLAIM_MATRIX_POSTS"])


def labels():
    return [line for line in state_path.read_text().splitlines() if line]


def store(values):
    state_path.write_text("".join(f"{value}\n" for value in values))


args = sys.argv[1:]
if args[:2] == ["issue", "view"]:
    print(json.dumps({
        "state": "OPEN",
        "labels": [{"name": value} for value in labels()],
        "body": "",
        "comments": [],
    }))
    raise SystemExit(0)
if args and args[0] == "api":
    posted = ""
    for index, arg in enumerate(args):
        if arg == "-f" and index + 1 < len(args):
            field = args[index + 1]
            if field.startswith("labels[]="):
                posted = field.removeprefix("labels[]=")
    if not posted:
        print("[]")
        raise SystemExit(0)
    current = labels()
    if posted not in current:
        current.append(posted)
        store(current)
    with post_path.open("a") as stream:
        stream.write(posted + "\n")
    print(json.dumps([{"name": value} for value in current]))
    raise SystemExit(0)
if args[:2] == ["issue", "edit"]:
    removed = args[args.index("--remove-label") + 1]
    store([value for value in labels() if value != removed])
    raise SystemExit(0)
raise SystemExit(f"unexpected gh call: {args!r}")
"""


def run_claim(claim_path: Path, command: str, labels: list[str], agent: str):
    with tempfile.TemporaryDirectory(prefix="claim-matrix-") as temp_name:
        temp = Path(temp_name)
        bin_dir = temp / "bin"
        bin_dir.mkdir()
        state = temp / "labels"
        posts = temp / "posts"
        state.write_text("".join(f"{label}\n" for label in labels))
        posts.write_text("")
        gh = bin_dir / "gh"
        gh.write_text(GH_STUB)
        gh.chmod(0o755)
        for directory in ("claims", "heartbeats", "reservations", "orphans"):
            (temp / directory).mkdir()

        env = {
            key: value
            for key, value in os.environ.items()
            if not key.startswith("FLEET_DISPATCH_")
            and key not in {"FLEET_ROLE", "FLEET_ROLE_MODEL", "GH_TOKEN"}
        }
        env.update(
            {
                "PATH": f"{bin_dir}{os.pathsep}{env['PATH']}",
                "HOME": str(temp),
                "FLEET_TEST_HOST": "mac",
                "FLEET_CLAIM_NO_SLEEP": "1",
                "FLEET_CLAIM_ACQUIRE_RETRIES": "1",
                "FLEET_CLAIMS_DIR": str(temp / "claims"),
                "FLEET_HEARTBEATS_DIR": str(temp / "heartbeats"),
                "FLEET_RESERVATIONS_DIR": str(temp / "reservations"),
                "FLEET_ORPHANS_DIR": str(temp / "orphans"),
                "CLAIM_MATRIX_STATE": str(state),
                "CLAIM_MATRIX_POSTS": str(posts),
            }
        )
        result = subprocess.run(
            ["bash", str(claim_path), command, "990001", agent],
            capture_output=True,
            check=False,
            env=env,
            text=True,
            timeout=20,
        )
        return result, state.read_text().splitlines(), posts.read_text().splitlines()


def claim_guarded(claim_path: Path, family: Family, held: str) -> bool:
    foreign = f"fleet:{held}-mac-probeB"
    cross_host = f"fleet:{held}-linux-probeB"
    own = f"fleet:{held}-mac-probeA"
    free, _, free_posts = run_claim(claim_path, family.command, [], "probeA")
    blocked, blocked_labels, blocked_posts = run_claim(
        claim_path, family.command, [foreign], "probeA"
    )
    same, same_labels, _ = run_claim(claim_path, family.command, [own], "probeA")
    cross, cross_labels, cross_posts = run_claim(claim_path, family.command, [cross_host], "probeA")
    reentered, _, _ = run_claim(claim_path, family.command, [], "probeA")
    passed = (
        free.returncode == 0
        and len(free_posts) == 1
        and blocked.returncode == 1
        and blocked_labels == [foreign]
        and not blocked_posts
        and same.returncode == 0
        and own in same_labels
        and cross.returncode == 1
        and cross_labels == [cross_host]
        and not cross_posts
        and reentered.returncode == 0
    )
    if (family.name, held) not in {
        ("amending", "reviewing"),
        ("reviewing", "amending"),
    }:
        return passed

    incumbent = f"{family.prefix}mac-probeA"
    replay, replay_labels, replay_posts = run_claim(
        claim_path, family.command, [incumbent, foreign], "probeA"
    )
    return (
        passed
        and replay.returncode == 0
        and replay_labels == [incumbent, foreign]
        and not replay_posts
    )


GUARDED = {
    ("reviewing", "amending", "scout"),
    ("reviewing", "amending", "claim"),
    ("amending", "reviewing", "scout"),
    ("amending", "reviewing", "claim"),
    ("resolving", "reviewing", "scout"),
    ("resolving", "reviewing", "claim"),
}


def support_fixtures(
    module,
    common_source: str,
    claim_source: str,
    claim_path: Path,
    families: dict[str, Family],
) -> dict[str, bool]:
    feedback_labels = {
        "fleet:semantic-conflict",
        "fleet:has-nits",
        "fleet:needs-opus-recheck",
    }
    amend_under_resolve, _, _ = run_claim(
        claim_path,
        families["amending"].command,
        ["fleet:resolving-mac-probeB"],
        "probeA",
    )
    resolve_under_amend, _, _ = run_claim(
        claim_path,
        families["resolving"].command,
        ["fleet:amending-mac-probeB"],
        "probeA",
    )
    raw_routes_feedback = (
        not module.worker_feedback_labels(feedback_labels)
        and not module._semantic_conflict_claimable(_conflict_pr(), feedback_labels, {})
        and amend_under_resolve.returncode == 0
        and resolve_under_amend.returncode == 0
    )
    review_under_resolve, _, _ = run_claim(
        claim_path,
        families["reviewing"].command,
        ["fleet:resolving-mac-probeB"],
        "probeA",
    )
    conflict_phase = (
        module._review_skipped({"fleet:semantic-conflict"})
        and not module._semantic_conflict_claimable(
            _conflict_pr(),
            {"fleet:semantic-conflict", "fleet:resolving-mac-probeA"},
            {},
        )
        and review_under_resolve.returncode == 0
    )
    issue_scope = (
        all(
            token in common_source
            for token in (
                '[plan]="fleet:planning-"',
                '[review]="fleet:reviewing-"',
                '[planreview]="fleet:reviewing-"',
                '[smoke]="fleet:reviewing-"',
            )
        )
        and bool(
            re.search(
                r'cmd_steward_claim\(\).*?_cmd_pr_label_claim.*?"issue"',
                claim_source,
            )
        )
        and bool(
            re.search(
                r'cmd_planning_claim\(\)[\s\S]*?_cmd_pr_label_claim.*?"issue"',
                claim_source,
            )
        )
    )
    human_priority = module.worker_feedback_labels(
        {"human:needs-fix", "fleet:reviewing-mac-probeB"}
    ) == frozenset({"human:needs-fix"})
    return {
        "feedback_first": raw_routes_feedback,
        "conflict_phase": conflict_phase,
        "issue_scope": issue_scope,
        "human_priority": human_priority,
    }


def validate_matrix(
    claim_path: Path = CLAIM_PATH,
    scout_path: Path = SCOUT_PATH,
    common_path: Path = COMMON_PATH,
    exceptions: dict[tuple[str, str, str], ExceptionRecord] | None = None,
    qualified_exceptions: dict[tuple[str, str, str, str], ExceptionRecord] | None = None,
) -> tuple[dict[str, Family], int, int]:
    registry = EXCEPTIONS if exceptions is None else exceptions
    qualified = QUALIFIED_EXCEPTIONS if qualified_exceptions is None else qualified_exceptions
    claim_source = claim_path.read_text()
    families = derive_families(claim_source)
    population = {
        (candidate, held, surface)
        for candidate in families
        for held in families
        if candidate != held
        for surface in SURFACES
    }

    unknown = set(registry) - population
    if unknown:
        raise AssertionError(f"obsolete exception keys: {sorted(unknown)}")
    qualified_population = {(candidate, held, surface) for candidate, held, surface, _ in qualified}
    unknown_qualified = qualified_population - population
    if unknown_qualified:
        raise AssertionError(f"obsolete qualified exception keys: {sorted(unknown_qualified)}")
    blank = [key for key, record in registry.items() if not record.reason.strip()]
    blank.extend(key for key, record in qualified.items() if not record.reason.strip())
    if blank:
        raise AssertionError(f"blank exception reasons: {sorted(blank)}")
    uncovered = (population - GUARDED) - set(registry)
    if uncovered:
        candidate, held, surface = sorted(uncovered)[0]
        raise AssertionError(f"{candidate} under {held} [{surface}]: unaccounted cell")

    module = load_scout(scout_path)
    supports = support_fixtures(module, common_path.read_text(), claim_source, claim_path, families)

    guarded_count = 0
    exception_count = 0
    failures = [
        f"{candidate} under {held} [{surface}/{qualifier}]: support {record.support} failed"
        for (candidate, held, surface, qualifier), record in qualified.items()
        if not supports.get(record.support, False)
    ]
    for key in sorted(population):
        candidate, held, surface = key
        if key in GUARDED:
            guarded_count += 1
            if key in registry:
                failures.append(f"{candidate} under {held} [{surface}]: obsolete exception")
                continue
            if surface == "scout":
                passed = scout_guarded(module, candidate, held)
            else:
                passed = claim_guarded(claim_path, families[candidate], held)
            if not passed:
                failures.append(f"{candidate} under {held} [{surface}]: guard failed")
            continue

        record = registry.get(key)
        if record is None:
            failures.append(f"{candidate} under {held} [{surface}]: unaccounted cell")
            continue
        exception_count += 1
        if record.support not in supports:
            failures.append(
                f"{candidate} under {held} [{surface}]: unknown support {record.support}"
            )
        elif not supports[record.support]:
            failures.append(
                f"{candidate} under {held} [{surface}]: support {record.support} failed"
            )

    if failures:
        raise AssertionError("\n".join(failures))
    if guarded_count + exception_count != len(population):
        raise AssertionError("coverage accounting does not match matrix population")
    return families, guarded_count, exception_count


class ClaimNamespaceMatrix(unittest.TestCase):
    def test_real_tree_matrix(self):
        families, guarded, exceptions = validate_matrix()
        ordered_pairs = len(families) * (len(families) - 1)
        print(f"derived families: {', '.join(sorted(families))}")
        print(
            f"claim namespace matrix: {ordered_pairs} ordered pairs, "
            f"{ordered_pairs * len(SURFACES)} cells; "
            f"{guarded} guarded, {exceptions} exceptions, "
            f"{len(QUALIFIED_EXCEPTIONS)} qualified carve-out"
        )

    def test_population_mutation_is_rejected(self):
        with _mutated_tree() as fleet_dir:
            claim = fleet_dir / "fleet-claim"
            source = claim.read_text()
            source = source.replace(
                "cmd_review_claim() {",
                'cmd_auditing_claim() { _cmd_pr_label_claim "fleet:auditing-" '
                '"auditing-claim" "$@"; }\n'
                "cmd_review_claim() {",
                1,
            )
            source = source.replace(
                "    review-claim)\n",
                "    auditing-claim)\n"
                '        cmd_auditing_claim "$2" "$3"\n'
                "        ;;\n"
                "    review-claim)\n",
                1,
            )
            claim.write_text(source)
            with self.assertRaisesRegex(AssertionError, "auditing.*unaccounted cell"):
                validate_matrix(
                    claim, fleet_dir / "fleet-state-scout", fleet_dir / "fleet-common.sh"
                )

    def test_missing_scout_guard_is_rejected(self):
        with _mutated_tree() as fleet_dir:
            scout = fleet_dir / "fleet-state-scout"
            source = scout.read_text().replace(
                'WORKER_SKIP_PREFIXES = ("fleet:reviewing-",)',
                "WORKER_SKIP_PREFIXES = ()",
                1,
            )
            scout.write_text(source)
            with self.assertRaisesRegex(
                AssertionError, r"amending under reviewing \[scout\]: guard failed"
            ):
                validate_matrix(fleet_dir / "fleet-claim", scout, fleet_dir / "fleet-common.sh")

    def test_missing_claim_guard_is_rejected(self):
        with _mutated_tree() as fleet_dir:
            common = fleet_dir / "fleet-common.sh"
            source = common.read_text().replace('    [fleet:reviewing-]="fleet:amending-"\n', "", 1)
            common.write_text(source)
            with self.assertRaisesRegex(
                AssertionError, r"reviewing under amending \[claim\]: guard failed"
            ):
                validate_matrix(
                    fleet_dir / "fleet-claim",
                    fleet_dir / "fleet-state-scout",
                    common,
                )

    def test_missing_exception_is_rejected(self):
        registry = dict(EXCEPTIONS)
        registry.pop(("planning", "stewarding", "claim"))
        with self.assertRaisesRegex(AssertionError, "planning under stewarding.*unaccounted"):
            validate_matrix(exceptions=registry)

    def test_blank_exception_reason_is_rejected(self):
        registry = dict(EXCEPTIONS)
        key = ("planning", "stewarding", "claim")
        registry[key] = ExceptionRecord("issue_scope", "   ")
        with self.assertRaisesRegex(AssertionError, "blank exception reasons"):
            validate_matrix(exceptions=registry)

    def test_obsolete_exception_key_is_rejected(self):
        registry = dict(EXCEPTIONS)
        registry[("retired", "planning", "claim")] = ISSUE_SCOPE
        with self.assertRaisesRegex(AssertionError, "obsolete exception keys"):
            validate_matrix(exceptions=registry)

    def test_raw_feedback_substitution_is_rejected(self):
        with _mutated_tree() as fleet_dir:
            scout = fleet_dir / "fleet-state-scout"
            source = scout.read_text().replace(
                "if labels & _WORKER_RELEVANT_LABELS:",
                "if worker_feedback_labels(labels):",
                1,
            )
            scout.write_text(source)
            with self.assertRaisesRegex(AssertionError, "feedback_first failed"):
                validate_matrix(fleet_dir / "fleet-claim", scout, fleet_dir / "fleet-common.sh")


class _mutated_tree:
    def __enter__(self) -> Path:
        self._temp = tempfile.TemporaryDirectory(prefix="claim-matrix-source-")
        self.path = Path(self._temp.name) / "fleet"
        shutil.copytree(FLEET_DIR, self.path)
        return self.path

    def __exit__(self, exc_type, exc_value, traceback):
        self._temp.cleanup()


if __name__ == "__main__":
    unittest.main(verbosity=2)
