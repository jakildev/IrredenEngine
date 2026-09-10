import sys
import tempfile
import unittest
from pathlib import Path

FLEET_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(FLEET_DIR))

import fleet_plans  # noqa: E402


class FleetPlanPathsTest(unittest.TestCase):
    def test_same_issue_number_has_distinct_repo_paths(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            engine = fleet_plans.plan_path("engine", 310, temp_dir)
            game = fleet_plans.plan_path("game", 310, temp_dir)
            self.assertNotEqual(engine, game)
            engine.parent.mkdir(parents=True)
            game.parent.mkdir(parents=True)
            engine.write_text("engine", encoding="utf-8")
            game.write_text("game", encoding="utf-8")
            self.assertEqual(fleet_plans.repo_of(engine, temp_dir), "engine")
            self.assertEqual(fleet_plans.repo_of(game, temp_dir), "game")

    def test_flat_file_has_no_repo_and_is_listed(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = fleet_plans.plans_root(temp_dir)
            root.mkdir(parents=True)
            flat = root / "issue-310.md"
            flat.write_text("# Plan: ambiguous", encoding="utf-8")
            (root / "notes.md").write_text("ignored", encoding="utf-8")
            self.assertIsNone(fleet_plans.repo_of(flat, temp_dir))
            self.assertEqual(fleet_plans.legacy_flat_files(temp_dir), [flat])

    def test_repo_slug_is_accepted(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            expected = fleet_plans.plans_root(temp_dir) / "game" / "issue-7.md"
            self.assertEqual(
                fleet_plans.plan_path("jakildev/irreden", 7, temp_dir),
                expected,
            )

    def test_resolution_uses_unique_issue_then_title_and_never_guesses(self):
        titles = {
            "engine": {
                2000: "Engine-only task",
                310: "World Z-yaw rotation across the trixel pipeline",
                137: "Engine issue one thirty-seven",
                42: "Engine forty-two",
            },
            "game": {
                310: "MIDI lattice visualizer",
                137: "Game-only task",
                42: "Game forty-two",
            },
        }
        with tempfile.TemporaryDirectory() as temp_dir:
            root = fleet_plans.plans_root(temp_dir)
            root.mkdir(parents=True)
            engine_only = root / "issue-2000.md"
            engine_only.write_text("## Plan\n", encoding="utf-8")
            engine_match = root / "issue-310.md"
            engine_match.write_text(
                "# Plan: #310 — World Z-yaw rotation across the trixel pipeline\n",
                encoding="utf-8",
            )
            game_only = root / "issue-137.md"
            game_only.write_text("# Plan: Game-only task\n", encoding="utf-8")
            ambiguous = root / "issue-42.md"
            ambiguous.write_text("# Plan: Something unrelated\n", encoding="utf-8")
            old_task = root / "T-054.md"
            old_task.write_text("# Plan: old task\n", encoding="utf-8")

            self.assertEqual(
                fleet_plans.resolve_legacy_file(engine_only, titles)[0],
                "engine",
            )
            self.assertEqual(
                fleet_plans.resolve_legacy_file(engine_match, titles)[0],
                "engine",
            )
            self.assertEqual(
                fleet_plans.resolve_legacy_file(game_only, titles)[0],
                "game",
            )
            self.assertIsNone(
                fleet_plans.resolve_legacy_file(ambiguous, titles)[0],
            )
            key, reason = fleet_plans.resolve_legacy_file(old_task, titles)
            self.assertIsNone(key)
            self.assertEqual(reason, "UNRESOLVED-legacy")


if __name__ == "__main__":
    unittest.main()
