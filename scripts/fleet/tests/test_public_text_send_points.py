"""Ratchet every fleet call site that sends text to GitHub."""

import re
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

CALL_RE = re.compile(
    r"\bgh\s+(issue|pr)\s+(create|edit|comment)\b"
    r"|\bgh\s+api\s+[^\n]*(?:issues|pulls)/[^\n]*/comments\b"
    r"|fleet_github\.run\(\s*\[\s*['\"](issue|pr)['\"]\s*,\s*['\"](create|edit|comment)['\"]"
    r"|\bgh_write\(\[\s*['\"](issue|pr)['\"]\s*,\s*['\"](create|edit|comment)['\"]",
    re.MULTILINE,
)
PYTHON_FUNCTION_RE = re.compile(r"^def\s+([A-Za-z_]\w*)\s*\(", re.MULTILINE)
BASH_FUNCTION_RE = re.compile(r"^([A-Za-z_]\w*)\s*\(\)\s*\{", re.MULTILINE)

# Each exempt sender constructs text exclusively from public-repository state,
# or is an owner-only/manual lane whose input is governed at its own boundary.
EXEMPT = {
    ("fleet-claim", "gh_release_label"): (2, "label-only edits send no text"),
    ("fleet-claim", "_amend_label_remove_fenced"): (1, "label-only edit sends no text"),
    ("fleet-claim", "_mark_sweep_cooldown"): (1, "label-only edit sends no text"),
    ("fleet-claim", "_clear_sweep_cooldown"): (1, "label-only edit sends no text"),
    ("fleet-claim", "_sweep_stale_prefix_holders"): (1, "label-only edits send no text"),
    ("fleet-claim", "cmd_claim"): (1, "label-only edit sends no text"),
    ("fleet-claim", "cmd_claim_base"): (1, "help-text example is not executed"),
    ("fleet-claim", "cmd_release"): (1, "label-only edits send no text"),
    ("fleet-claim", "cmd_planning_claim"): (1, "label-only edit sends no text"),
    ("fleet-claim", "_cleanup_sweep_label"): (1, "label-only edit sends no text"),
    ("fleet-claim", "cmd_cleanup_gh"): (5, "embedded reconcile code sends labels only"),
    ("fleet-claim", "cmd_reset_sweep_host_claims"): (5, "label-only edits send no text"),
    ("fleet-claim", "replay_orphans"): (1, "label-only edit sends no text"),
    ("fleet-claim", "cmd_check_stale"): (1, "tool-generated engine-state tracker"),
    ("fleet-claim", "<module>"): (4, "library-dispatch examples and label-only calls"),
    ("fleet-claim", "reconcile_heal_design_unblock"): (2, "label-only edits send no text"),
    ("fleet-dispatcher", "assess_target_completion"): (1, "read-only comments API call"),
    ("fleet-dispatcher", "park_target"): (1, "tool-generated engine target state"),
    ("fleet-nit-close-for-pr", "<module>"): (1, "fixed tool-generated nit closure text"),
    ("fleet-pr-clear-feedback-labels", "<module>"): (2, "label-only edits send no text"),
    ("fleet-queue-backfill-model-labels", "<module>"): (1, "label-only edit sends no text"),
    ("fleet-queue-ingest", "<module>"): (11, "labels and tool-generated engine queue state"),
    ("fleet-rebase", "park_orphaned_base"): (2, "tool-generated engine branch state"),
    ("fleet-rebase", "attempt_orphaned_base"): (6, "labels and tool-generated engine branch state"),
    ("fleet-rebase", "cleanup_mergeable_label"): (1, "label-only edit sends no text"),
    ("fleet-rebase", "cleanup_stale_base_park"): (1, "label-only edit sends no text"),
    ("fleet-reconcile-amendments", "revert_orphan_label"): (2, "labels and fixed recovery text"),
    ("fleet-state-scout", "fetch_pr_inline_comments"): (1, "read-only comments API call"),
    ("fleet-transition", "<module>"): (1, "label-only edit sends no text"),
    ("fleet-triage-sweep", "<module>"): (6, "tool-generated engine triage state"),
    ("fleet-up", "file_stuck_task_issue"): (1, "tool-generated engine task state"),
    ("fleet-up", "write_worktree_settings"): (1, "shell template text is not executed here"),
}

PROTECTED = {
    ("fleet-claim", "cmd_decline"): 1,
    ("fleet-claim", "reconcile_escalate_drift"): 2,
    ("fleet-issue", "cmd_set_field"): 2,
}


def send_points(root):
    points = []
    for path in sorted(root.iterdir()):
        if not path.is_file() or path.name.startswith("test_"):
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if path.suffix == ".py" or text.startswith("#!/usr/bin/env python"):
            matches = list(PYTHON_FUNCTION_RE.finditer(text))
            functions = [
                (match.start(), matches[index + 1].start() if index + 1 < len(matches)
                 else len(text), match.group(1))
                for index, match in enumerate(matches)
            ]
        else:
            functions = []
            for function_match in BASH_FUNCTION_RE.finditer(text):
                end_match = re.search(r"^}\s*$", text[function_match.end():], re.MULTILINE)
                end = (function_match.end() + end_match.end()) if end_match else len(text)
                functions.append((function_match.start(), end, function_match.group(1)))
        counts = {}
        for match in CALL_RE.finditer(text):
            function = "<module>"
            for start, end, name in functions:
                if start <= match.start() < end:
                    function = name
            verb = next((value for value in match.groups() if value in
                         {"create", "edit", "comment"}), "api-comment")
            base = (path.name, function, verb)
            counts[base] = counts.get(base, 0) + 1
            points.append((*base, counts[base]))
    return points


class SendPointRatchet(unittest.TestCase):
    def test_every_send_point_is_checked_or_reasoned(self):
        points = send_points(ROOT)
        observed = {}
        for point in points:
            observed[point[:2]] = observed.get(point[:2], 0) + 1
        expected = {**{key: count for key, (count, _reason) in EXEMPT.items()}, **PROTECTED}
        self.assertEqual(observed, expected)

    def test_new_unlisted_send_point_turns_the_ratchet_red(self):
        with tempfile.TemporaryDirectory() as temp:
            scratch = Path(temp)
            (scratch / "fleet-new").write_text(
                "new_sender() { gh issue comment 1 --body private; }\n", encoding="utf-8")
            points = send_points(scratch)
        self.assertEqual(points, [("fleet-new", "new_sender", "comment", 1)])
        self.assertNotIn(points[0][:2], EXEMPT)


if __name__ == "__main__":
    unittest.main()
