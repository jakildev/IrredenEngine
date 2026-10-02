"""Reviewer pickup order and transient-failure decline expiry in fleet_task_class.

`pick_role` hands the dispatcher one `review:<repo>:<N>` per candidate. Two
rules pinned here:

- Order: a PR carrying a priority label (`human:blocker`, `fleet:blocker`,
  `human:re-review`) comes first, then engine before game, then oldest
  first by number, whatever order the slice arrived in.
- Decline memory: a decline whose reason was the host's own failure (HTTP
  401 after a token expired, a 5xx, a rate-limit refusal) expires after
  DECLINE_TRANSIENT_TTL_SECONDS; a judgement decline keeps its full TTL.
  Two opus rechecks once sat 19 hours behind a "HTTP 401 Bad credentials"
  decline, offered to nobody until something else touched the PR.
"""
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fleet_task_class as ftc  # noqa: E402


def _pr(number, labels=(), repo="engine", updated="2026-10-01T12:00:00Z"):
    return {"number": number, "labels": list(labels), "repo": repo, "updatedAt": updated}


class ReviewPickOrder(unittest.TestCase):
    def test_priority_then_engine_then_oldest(self):
        prs = [_pr(4049), _pr(4007), _pr(301, repo="game"), _pr(4028, ["human:re-review"]),
               _pr(3998), _pr(299, repo="game", labels=["human:blocker"])]
        self.assertEqual(ftc.pick_role({"flagged_prs": prs}, "opus-reviewer"),
                         ["review:engine:4028", "review:game:299", "review:engine:3998",
                          "review:engine:4007", "review:engine:4049", "review:game:301"])
        self.assertEqual(ftc.pick_role({"candidate_prs": prs}, "sonnet-reviewer")[:2],
                         ["review:engine:4028", "review:game:299"])

    def test_held_and_declined_are_still_dropped_after_ordering(self):
        prs = [_pr(4007, ["fleet:reviewing-mac-pool-1"]), _pr(4028)]
        self.assertEqual(ftc.pick_role({"flagged_prs": prs}, "opus-reviewer"),
                         ["review:engine:4028"])


class TransientDeclineExpiry(unittest.TestCase):
    def _with_decline(self, detail, age_seconds):
        state_dir = tempfile.mkdtemp()
        os.environ["FLEET_STATE_DIR"] = state_dir
        os.makedirs(os.path.join(state_dir, "declined"))
        path = os.path.join(state_dir, "declined", "review-engine-4007")
        with open(path, "w") as handle:
            handle.write(f"2026-10-01T12:00:00Z\n{detail}\nopus-reviewer\n")
        stamp = time.time() - age_seconds
        os.utime(path, (stamp, stamp))
        return state_dir

    def tearDown(self):
        os.environ.pop("FLEET_STATE_DIR", None)

    def test_transient_decline_expires_after_its_ttl(self):
        pr = _pr(4007)
        self._with_decline("Blocked: GitHub authentication returned HTTP 401 Bad credentials",
                           ftc.DECLINE_TRANSIENT_TTL_SECONDS + 60)
        self.assertEqual(ftc.pick_role({"flagged_prs": [pr]}, "opus-reviewer"),
                         ["review:engine:4007"])

    def test_fresh_transient_decline_still_holds(self):
        pr = _pr(4007)
        self._with_decline("gh pr review failed with HTTP 401 Bad credentials", 60)
        self.assertEqual(ftc.pick_role({"flagged_prs": [pr]}, "opus-reviewer"), [])

    def test_judgement_decline_keeps_the_full_ttl(self):
        pr = _pr(4007)
        self._with_decline("fresh sonnet approval already posted; escalation standing",
                           ftc.DECLINE_TRANSIENT_TTL_SECONDS + 60)
        self.assertEqual(ftc.pick_role({"flagged_prs": [pr]}, "opus-reviewer"), [])
        self._with_decline("fresh sonnet approval already posted; escalation standing",
                           ftc.DECLINE_TTL_SECONDS + 60)
        self.assertEqual(ftc.pick_role({"flagged_prs": [pr]}, "opus-reviewer"),
                         ["review:engine:4007"])


if __name__ == "__main__":
    unittest.main()
