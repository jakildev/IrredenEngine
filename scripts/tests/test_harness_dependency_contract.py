"""The render harness's third-party dependency contract, executed.

The harness under ``scripts/`` is stdlib-only, with one registered exception
tier: the files in ``PILLOW_EXCEPTIONS`` may import ``PIL`` (installed by
``render-harness-tests.yml``, pinned) and nothing else third-party. This suite
holds that list as the list of record and derives the real importer set from
the source, so a script that grows a dependency the workflow does not install
fails here instead of on a bare host.

Population is exactly the workflow's trigger globs — ``scripts/*.py``,
``scripts/tests/*.py`` and ``scripts/perf/compare_perf_runs.py`` — so the
change that breaks the gate cannot skip it. ``compare_perf_runs`` is in the
population so its stem is a local module: ``occlusion-fire-verify.py`` imports
it after a ``sys.path`` insert, and without it the derivation would call it
third-party.

Imports are read with ``ast`` and never executed: importing a listed file on a
host without Pillow is the failure this contract exists to name.

Arms, each with a mutation control below:
  (a) the derived third-party-importer set equals ``PILLOW_EXCEPTIONS``
  (b) ``PIL`` is the only third-party top-level module
  (c) no file the gating path depends on is on the list
  (d) no unlisted file imports a listed local module (the transitive route)
"""
import ast
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

_REPO = Path(__file__).resolve().parents[2]

PILLOW_EXCEPTIONS = frozenset({
    "scripts/render-detached-face-metric.py",
    "scripts/render-detached-lighting-metric.py",
    "scripts/render-local-voxel-metric.py",
    "scripts/render-normal-facing-metric.py",
    "scripts/render-receiver-position-metric.py",
    "scripts/render-shadow-box-metric.py",
    "scripts/render-shadow-probes-metric.py",
    "scripts/render-sun-occlusion-metric.py",
    "scripts/render-visible-box-metric.py",
    "scripts/render_probe_geometry.py",
    "scripts/tests/test_render_shadow_box_edges.py",
})

ALLOWED_THIRD_PARTY = frozenset({"PIL"})

# render-verify must keep running on a bare host: these, every *-verify.py, and
# every metric a committed manifest names, are never eligible for the list.
_NEVER_ELIGIBLE = frozenset({
    "scripts/render-verify.py",
    "scripts/render-compare.py",
    "scripts/verify_common.py",
    "scripts/render_metric_util.py",
})

_POPULATION_GLOBS = ("scripts/*.py", "scripts/tests/*.py")
_POPULATION_FILES = ("scripts/perf/compare_perf_runs.py",)
_MANIFEST_GLOB = "creations/demos/*/test/references/manifest.json"


def _population(root: Path) -> list[Path]:
    files = []
    for pattern in _POPULATION_GLOBS:
        files.extend(root.glob(pattern))
    files.extend(root / rel for rel in _POPULATION_FILES if (root / rel).is_file())
    return sorted(files)


def _imported_top_level_names(path: Path) -> set[str]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    names: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            names.update(alias.name.split(".")[0] for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            names.add(node.module.split(".")[0])
    return names


def derive_third_party(root: Path) -> dict[str, set[str]]:
    """Map repo-relative path -> third-party top-level modules it imports."""
    files = _population(root)
    local = {p.stem for p in files}
    found: dict[str, set[str]] = {}
    for path in files:
        third = {n for n in _imported_top_level_names(path)
                 if n not in sys.stdlib_module_names and n not in local}
        if third:
            found[path.relative_to(root).as_posix()] = third
    return found


def _manifest_metrics(root: Path) -> set[str]:
    metrics: set[str] = set()
    for manifest in root.glob(_MANIFEST_GLOB):
        stack = [json.loads(manifest.read_text(encoding="utf-8"))]
        while stack:
            node = stack.pop()
            if isinstance(node, dict):
                for key, value in node.items():
                    if key == "structural" and isinstance(value, dict):
                        for entries in value.values():
                            metrics.update(e["metric"] for e in entries
                                           if isinstance(e, dict) and "metric" in e)
                    stack.append(value)
            elif isinstance(node, list):
                stack.extend(node)
    return metrics


def _never_eligible(root: Path) -> set[str]:
    gating = set(_NEVER_ELIGIBLE)
    gating.update(p.relative_to(root).as_posix()
                  for p in root.glob("scripts/*-verify.py"))
    gating.update(f"scripts/render-{m}-metric.py" for m in _manifest_metrics(root))
    return gating


def check_contract(root: Path, exceptions=PILLOW_EXCEPTIONS) -> list[str]:
    """Return one violation string per breach, prefixed with its arm."""
    violations = []
    derived = derive_third_party(root)
    for rel in sorted(set(derived) - exceptions):
        violations.append(f"(a) unlisted third-party importer: {rel} {sorted(derived[rel])}")
    for rel in sorted(exceptions - set(derived)):
        violations.append(f"(a) listed but imports nothing third-party: {rel}")
    for rel, names in sorted(derived.items()):
        for name in sorted(names - ALLOWED_THIRD_PARTY):
            violations.append(f"(b) third-party module {name} in {rel}")
    for rel in sorted(exceptions & _never_eligible(root)):
        violations.append(f"(c) gating-path file is on the exception list: {rel}")
    listed_locals = {Path(rel).stem for rel in exceptions
                     if Path(rel).stem.isidentifier()}
    for path in _population(root):
        rel = path.relative_to(root).as_posix()
        if rel in exceptions:
            continue
        for name in sorted(_imported_top_level_names(path) & listed_locals):
            violations.append(f"(d) {rel} imports listed module {name}")
    return violations


def _scratch_copy(dest: Path) -> Path:
    """Copy exactly the population and manifests, preserving the layout."""
    for src in _population(_REPO):
        target = dest / src.relative_to(_REPO)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, target)
    for manifest in _REPO.glob(_MANIFEST_GLOB):
        target = dest / manifest.relative_to(_REPO)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(manifest, target)
    return dest


def _append(path: Path, text: str) -> None:
    path.write_text(path.read_text(encoding="utf-8") + "\n" + text + "\n",
                    encoding="utf-8")


class HarnessDependencyContractTest(unittest.TestCase):

    def test_repo_satisfies_the_contract(self):
        self.assertEqual(check_contract(_REPO), [])

    def test_only_pillow_is_third_party(self):
        found = set().union(*derive_third_party(_REPO).values())
        self.assertEqual(found, ALLOWED_THIRD_PARTY)

    def test_perf_report_parser_is_local_not_third_party(self):
        # occlusion-fire-verify.py reaches compare_perf_runs through a sys.path
        # insert; the population must make it local, not special-case the name.
        self.assertNotIn("scripts/occlusion-fire-verify.py", derive_third_party(_REPO))

    def test_gating_path_is_never_eligible(self):
        gating = _never_eligible(_REPO)
        self.assertIn("scripts/render-verify.py", gating)
        self.assertIn("scripts/render-shadow-metric.py", gating)  # manifests name shadow
        self.assertIn("scripts/occlusion-fire-verify.py", gating)
        self.assertFalse(gating & PILLOW_EXCEPTIONS)


class ContractMutationControlTest(unittest.TestCase):
    """One mutation per arm, run in a scratch copy — nothing here is committed."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = _scratch_copy(Path(self._tmp.name))

    def test_unmutated_copy_is_clean(self):
        self.assertEqual(check_contract(self.root), [])

    def test_arm_a_unlisted_importer_is_named(self):
        _append(self.root / "scripts/render-compare.py", "from PIL import Image")
        violations = check_contract(self.root)
        self.assertTrue(any(v.startswith("(a)") and "render-compare.py" in v
                            for v in violations), violations)

    def test_arm_b_other_third_party_module_is_named(self):
        _append(self.root / "scripts/render-shadow-box-metric.py",
                "def _f():\n    import numpy")
        violations = check_contract(self.root)
        self.assertTrue(any(v.startswith("(b)") and "numpy" in v
                            for v in violations), violations)

    def test_arm_c_gating_path_file_on_the_list_is_named(self):
        target = "scripts/render-shadow-metric.py"
        _append(self.root / target, "from PIL import Image")
        violations = check_contract(self.root, PILLOW_EXCEPTIONS | {target})
        self.assertTrue(any(v.startswith("(c)") and target in v
                            for v in violations), violations)

    def test_arm_d_transitive_import_of_a_listed_module_is_named(self):
        target = "scripts/render-shadow-metric.py"
        _append(self.root / target, "from render_probe_geometry import convex_hull")
        violations = check_contract(self.root)
        self.assertTrue(any(v.startswith("(d)") and target in v
                            for v in violations), violations)


if __name__ == "__main__":
    unittest.main()
