"""Corpus tests for the shared fleet ``Model:`` field reader (#2833)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))
import fleet_model_field as fmf

CORPUS = (
    ("plain", "**Model:** sonnet", "sonnet"),
    ("backticked", "**Model:** `sonnet`", "sonnet"),
    ("bracketed", "**Model:** [sonnet]", "sonnet"),
    ("list item", "- **Model:** sonnet", "sonnet"),
    ("suggested alias", "**Suggested Model:** sonnet", "sonnet"),
    (
        "qualified",
        "**Model:** sonnet (escalate to opus if the shader path is involved)",
        "sonnet",
    ),
    ("plain fable", "**Model:** fable", "fable"),
    ("absent", "No model field.", None),
    ("prose two", "**Model:** either opus or sonnet", "opus"),
    ("no token", "**Model:** TBD", None),
    (
        "inline field",
        "**Area:** tooling · **Owner:** fleet · **Model:** sonnet",
        "sonnet",
    ),
)


class ModelFieldCorpus(unittest.TestCase):
    def test_declared_class(self):
        for name, body, expected in CORPUS:
            with self.subTest(name=name):
                self.assertEqual(fmf.declared_class(body), expected)

    def test_first_field_wins(self):
        body = "**Model:** sonnet\n**Model:** fable\n"
        self.assertEqual(fmf.declared_class(body), "sonnet")

    def test_tokens_preserve_position(self):
        body = "**Model:** sonnet (escalate to opus)"
        self.assertEqual(fmf.class_tokens(body), ["sonnet", "opus"])

    def test_exported_classes_match_routing_vocabulary(self):
        self.assertEqual(fmf.MODEL_CLASSES, ("fable", "opus", "sonnet"))


if __name__ == "__main__":
    unittest.main()
