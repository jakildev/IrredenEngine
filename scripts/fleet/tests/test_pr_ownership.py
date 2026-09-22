"""Persistent PR ownership gates writes without preventing independent review."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_claim_namespace_matrix import CLAIM_PATH, SCOUT_PATH, load_scout, run_claim


class PrOwnership(unittest.TestCase):
    def test_merger_preserves_ready_signal_but_cannot_rewrite_owned_pr(self):
        scout = load_scout(SCOUT_PATH)
        labels = {"fleet:approved", "fleet:claim-mac-probeA"}
        self.assertEqual(scout._merger_action_signal(labels, "MERGEABLE", "master"), "merge-ready")
        for state in ("CONFLICTING", "BEHIND", "UNSTABLE"):
            self.assertIsNone(scout._merger_action_signal(labels, state, "master"))
            self.assertIsNone(scout._merger_candidate_signal(labels, state, "master"))

    def test_failed_owner_removal_does_not_queue_a_delayed_release(self):
        for command in ("cmd_pr_claim", "cmd_pr_release"):
            with tempfile.TemporaryDirectory() as temp:
                env = dict(
                    os.environ,
                    FLEET_CLAIM_LIB="1",
                    FLEET_TEST_HOST="mac",
                    FLEET_CLAIM_NO_SLEEP="1",
                    FLEET_CLAIM_ACQUIRE_RETRIES="1",
                    HOME=temp,
                    TEST_COMMAND=command,
                )
                script = r"""
fleet-gh-token() { return 1; }
source "$1"
sleep() { :; }
gh() {
    if [[ "$1 $2" == "pr view" ]]; then
        echo OPEN
    elif [[ "$1 $2" == "issue view" ]]; then
        if [[ "${TEST_RESUME:-0}" == 1 ]]; then
            echo '{"state":"OPEN","labels":[{"name":"fleet:claim-mac-probeA"}],"body":""}'
        else
            echo '{"state":"OPEN","labels":[],"body":""}'
        fi
    elif [[ "$*" == *"--method POST"* ]]; then
        echo '[]'
    else
        return 1
    fi
}
_fetch_live_labels() {
    echo fleet:claim-mac-probeA
    [[ "${TEST_RESUME:-0}" == 1 ]] || echo fleet:claim-linux-probeB
    return 0
}
"$TEST_COMMAND" 990001 probeA && exit 99
echo removal-failed
TEST_RESUME=1
cmd_pr_claim 990001 probeA
"""
                result = subprocess.run(
                    ["bash", "-c", script, "test", str(CLAIM_PATH)],
                    env=env,
                    capture_output=True,
                    text=True,
                    timeout=15,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("removal-failed", result.stdout)
                self.assertIn("already held", result.stdout)
                self.assertEqual(list(Path(temp).rglob("*.json")), [])

    def test_owner_persists_across_reacquire_and_explicit_release(self):
        owner = "fleet:claim-mac-probeA"
        acquired, labels, posts = run_claim(CLAIM_PATH, "pr-claim", [], "probeA")
        self.assertEqual(acquired.returncode, 0, acquired.stderr)
        self.assertEqual(labels, [owner])
        self.assertEqual(posts, [owner])
        resumed, labels, posts = run_claim(CLAIM_PATH, "pr-claim", [owner], "probeA")
        self.assertEqual(resumed.returncode, 0, resumed.stderr)
        self.assertEqual(labels, [owner])
        self.assertEqual(posts, [])
        released, labels, _ = run_claim(CLAIM_PATH, "pr-release", [owner], "probeA")
        self.assertEqual(released.returncode, 0, released.stderr)
        self.assertEqual(labels, [])

    def test_foreign_release_does_not_remove_owner(self):
        owner = "fleet:claim-linux-probeB"
        _, labels, _ = run_claim(CLAIM_PATH, "pr-release", [owner], "probeA")
        self.assertEqual(labels, [owner])

    def test_competing_owner_cannot_take_over(self):
        for owner in ("fleet:claim-mac-probeB", "fleet:claim-linux-probeB"):
            result, labels, _ = run_claim(CLAIM_PATH, "pr-claim", [owner], "probeA")
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(labels, [owner])

    def test_incumbent_writer_cannot_bypass_persistent_owner(self):
        for lane in ("amending", "resolving"):
            labels = [f"fleet:{lane}-mac-probeA", "fleet:claim-linux-probeB"]
            result, after, posts = run_claim(CLAIM_PATH, f"{lane}-claim", labels, "probeA")
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(after, labels)
            self.assertEqual(posts, [])

    def test_all_feedback_waits_for_owner_including_human_feedback(self):
        scout = load_scout(SCOUT_PATH)
        for feedback in ("fleet:needs-fix", "fleet:has-nits", "human:needs-fix", "human:blocker"):
            self.assertTrue(scout.worker_feedback_labels({feedback}))
            self.assertFalse(scout.worker_feedback_labels({feedback, "fleet:claim-mac-probeA"}))

    def test_mutation_claim_release_keeps_owner(self):
        for lane in ("amending", "resolving"):
            owner = "fleet:claim-mac-probeA"
            result, labels, _ = run_claim(
                CLAIM_PATH, f"{lane}-release", [owner, f"fleet:{lane}-mac-probeA"], "probeA"
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(labels, [owner])

    def test_late_owner_is_seen_by_each_confirmation_read(self):
        for lane in ("amending", "resolving", "claim"):
            for arrival in (1, 2):
                with tempfile.TemporaryDirectory() as temp:
                    env = dict(
                        os.environ,
                        FLEET_CLAIM_LIB="1",
                        FLEET_CLAIM_NO_SLEEP="1",
                        FLEET_CLAIM_ACQUIRE_RETRIES="1",
                        FLEET_TEST_HOST="mac",
                        HOME=temp,
                        TEST_COUNTER=temp + "/reads",
                        TEST_ARRIVAL=str(arrival),
                        TEST_LANE=lane,
                    )
                    script = r"""
fleet-gh-token() { return 1; }; source "$1"
gh() { [[ "$*" == *"--method POST"* ]]; }
gh_release_label() { echo "released:$3"; }
_fetch_live_labels() {
    local n=0
    [[ ! -f "$TEST_COUNTER" ]] || read -r n < "$TEST_COUNTER"
    n=$((n + 1))
    printf '%s\n' "$n" > "$TEST_COUNTER"
    printf '%s\n' "$3"
    if (( n >= TEST_ARRIVAL )); then
        if [[ "$TEST_LANE" == claim ]]; then
            echo fleet:amending-linux-probeB
        else
            echo fleet:claim-linux-probeB
        fi
    fi
}
_acquire_label_on example/repo 990001 "fleet:${TEST_LANE}-mac-probeA" "fleet:${TEST_LANE}-"
"""
                    result = subprocess.run(
                        ["bash", "-c", script, "test", str(CLAIM_PATH)],
                        env=env,
                        capture_output=True,
                        text=True,
                        timeout=15,
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(f"released:fleet:{lane}-mac-probeA", result.stdout, result.stderr)

    def test_persistent_owner_is_not_ttl_swept(self):
        with tempfile.TemporaryDirectory() as temp:
            env = dict(os.environ, FLEET_CLAIM_LIB="1", HOME=temp)
            result = subprocess.run(
                [
                    "bash",
                    "-c",
                    'fleet-gh-token() { return 1; }; source "$1"; '
                    "gh() { echo unexpected-network; return 99; }; "
                    "_sweep_stale_prefix_holders example/repo 990001 "
                    "fleet:claim- fleet:claim-mac-probeA",
                    "test",
                    str(CLAIM_PATH),
                ],
                env=env,
                capture_output=True,
                text=True,
                timeout=15,
            )
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertNotIn("unexpected-network", result.stdout)


if __name__ == "__main__":
    unittest.main()
