"""Tests for fleet-tasks-render.

The renderer takes TASKS.md + GitHub state and rewrites the derived
fields (status marker, Owner, Blocked-by resolution, Done-section
ordering) while preserving human-authored fields (Notes, Acceptance,
Issue, Stack, etc.). This harness covers:

  - Idempotence: render twice -> same output.
  - Status derivation: [ ] vs [~] vs [x] vs stranded-merge.
  - Owner derivation from headRefName for open PRs.
  - Done-section ordering by mergedAt desc.
  - Existing-title preservation (so hand-tuned Done entries
    don't get gratuitously rewritten on every render).
  - Done-section cap at --done-limit.
  - Blocked-by resolution drops [x] blockers, preserves URL/text
    blockers, preserves remaining [~] / [ ] blockers.
  - Stranded-merge (mergedAt set but base != master) keeps the
    task in Open as [~] — covers the PR #543 regression pattern.
"""
import importlib.machinery
import importlib.util
import json
import os
import pathlib
import tempfile
import unittest

_SCRIPT = pathlib.Path(__file__).parent.parent / "fleet-tasks-render"
_loader = importlib.machinery.SourceFileLoader("fleet_tasks_render", str(_SCRIPT))
_spec = importlib.util.spec_from_loader("fleet_tasks_render", _loader)
_mod = importlib.util.module_from_spec(_spec)
_loader.exec_module(_mod)
render = _mod.render


def _state_file(tmpdir, *, prs_open=None, prs_merged=None,
                closed_fleet_queued=None):
    """Write a synthetic scout-cache state.json into tmpdir."""
    payload = {
        "repos": {
            "engine": {
                "prs": prs_open or [],
                "recent_merged_prs": prs_merged or [],
                "closed_fleet_queued": closed_fleet_queued or [],
            },
        },
    }
    p = pathlib.Path(tmpdir) / "state.json"
    p.write_text(json.dumps(payload))
    return p


def _pr(number, title, *, base="master", merged_at=None,
        head=None, state="OPEN", labels=None):
    return {
        "number": number,
        "title": title,
        "headRefName": head or f"claude/T-{number}-something",
        "baseRefName": base,
        "mergedAt": merged_at,
        "state": state if not merged_at else "MERGED",
        "labels": labels or [],
        "updatedAt": merged_at or "2026-05-08T00:00:00Z",
    }


def _basic_tasks_md(open_status=" ", owner="free", blocked_by="(none)"):
    return f"""# TASKS

intro text passes through.

## Open

<!-- Add tasks below this line. -->

- [{open_status}] **Some Engine Feature** — short summary
  - **ID:** T-200
  - **Area:** engine/feature
  - **Model:** opus
  - **Owner:** {owner}
  - **Blocked by:** {blocked_by}
  - **Acceptance:** (1) builds pass
  - **Issue:** #999
  - **Notes:** Hand-written notes that should pass through verbatim.
  - **Links:**


## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-100** — Pre-existing done entry · Owner: claude/T-100-foo · PR: https://github.com/jakildev/IrredenEngine/pull/100
"""


class Idempotence(unittest.TestCase):
    def test_render_twice_is_stable(self):
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td)
            text = _basic_tasks_md()
            once = render(text, state_file=state, repo_key="engine")
            twice = render(once, state_file=state, repo_key="engine")
            self.assertEqual(once, twice)

    def test_render_with_no_state_file_passes_through(self):
        text = _basic_tasks_md()
        out = render(text, state_file=pathlib.Path("/nonexistent.json"),
                     repo_key="engine", repo_slug="jakildev/IrredenEngine")
        # Without any cache, gh-fallback runs but may pull live PRs;
        # either way the open task metadata should pass through.
        self.assertIn("**ID:** T-200", out)
        self.assertIn("**Some Engine Feature**", out)


class StatusDerivation(unittest.TestCase):
    def test_open_pr_marks_task_in_progress(self):
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td, prs_open=[
                _pr(200, "T-200: Some Engine Feature", head="claude/T-200-feature"),
            ])
            text = _basic_tasks_md()
            out = render(text, state_file=state, repo_key="engine")
            self.assertIn("- [~] **Some Engine Feature**", out)
            self.assertIn("**Owner:** claude/T-200-feature", out)

    def test_no_pr_keeps_task_open(self):
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td)
            text = _basic_tasks_md(open_status="~", owner="claude/stale")
            out = render(text, state_file=state, repo_key="engine")
            self.assertIn("- [ ] **Some Engine Feature**", out)
            self.assertIn("**Owner:** free", out)

    def test_merged_to_master_moves_to_done(self):
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td, prs_merged=[
                _pr(200, "T-200: Some Engine Feature",
                    head="claude/T-200-feature",
                    merged_at="2026-05-08T20:00:00Z"),
            ])
            text = _basic_tasks_md(open_status="~", owner="claude/T-200-feature")
            out = render(text, state_file=state, repo_key="engine")
            # Task removed from Open
            self.assertNotIn("- [~] **Some Engine Feature**", out)
            self.assertNotIn("- [ ] **Some Engine Feature**", out)
            # Task added to Done
            self.assertIn("- [x] **T-200**", out)
            self.assertIn("PR: https://github.com/jakildev/IrredenEngine/pull/200", out)

    def test_fs_claim_preserves_in_progress_marker_with_no_pr(self):
        # T-138: fleet-claim flips master/TASKS.md [ ] → [~] before the
        # worker opens its PR. derive_status with no PR would normally
        # revert to [ ], clobbering the master push. The FS claim is the
        # signal to preserve [~] until the PR appears (or check-stale
        # prunes the abandoned claim).
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td)
            claims_dir = pathlib.Path(td) / "claims"
            (claims_dir / "t-200").mkdir(parents=True)
            (claims_dir / "t-200" / "title").write_text("T-200")
            (claims_dir / "t-200" / "owner").write_text("opus-worker-1")
            old = os.environ.pop("FLEET_CLAIMS_DIR", None)
            os.environ["FLEET_CLAIMS_DIR"] = str(claims_dir)
            try:
                text = _basic_tasks_md(open_status="~", owner="opus-worker-1")
                out = render(text, state_file=state, repo_key="engine")
            finally:
                if old is None:
                    os.environ.pop("FLEET_CLAIMS_DIR", None)
                else:
                    os.environ["FLEET_CLAIMS_DIR"] = old
            self.assertIn("- [~] **Some Engine Feature**", out)
            self.assertIn("**Owner:** opus-worker-1", out)

    def test_no_fs_claim_reverts_in_progress_marker_with_no_pr(self):
        # Mirror of the test above without the FS claim — confirms the
        # FS-claim signal is what preserves [~]. With no PR and no FS
        # claim, the row reverts to [ ] (the historical behavior).
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td)
            claims_dir = pathlib.Path(td) / "empty-claims"
            claims_dir.mkdir(parents=True)
            old = os.environ.pop("FLEET_CLAIMS_DIR", None)
            os.environ["FLEET_CLAIMS_DIR"] = str(claims_dir)
            try:
                text = _basic_tasks_md(open_status="~", owner="opus-worker-1")
                out = render(text, state_file=state, repo_key="engine")
            finally:
                if old is None:
                    os.environ.pop("FLEET_CLAIMS_DIR", None)
                else:
                    os.environ["FLEET_CLAIMS_DIR"] = old
            self.assertIn("- [ ] **Some Engine Feature**", out)
            self.assertIn("**Owner:** free", out)

    def test_game_slug_namespace_isolation(self):
        # An engine render must ignore game-namespaced FS claims (slug
        # `game-...`) so cross-repo claims don't bleed across renders.
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td)
            claims_dir = pathlib.Path(td) / "claims"
            (claims_dir / "game-t-200").mkdir(parents=True)
            (claims_dir / "game-t-200" / "title").write_text("T-200")
            (claims_dir / "game-t-200" / "owner").write_text("opus-worker-1")
            old = os.environ.pop("FLEET_CLAIMS_DIR", None)
            os.environ["FLEET_CLAIMS_DIR"] = str(claims_dir)
            try:
                text = _basic_tasks_md(open_status="~", owner="opus-worker-1")
                # Engine render: game-T-200 claim must not preserve [~]
                # for this engine task with the same task ID.
                out = render(text, state_file=state, repo_key="engine")
            finally:
                if old is None:
                    os.environ.pop("FLEET_CLAIMS_DIR", None)
                else:
                    os.environ["FLEET_CLAIMS_DIR"] = old
            self.assertIn("- [ ] **Some Engine Feature**", out)

    def test_closed_issue_with_no_pr_match_reaps_to_done(self):
        # The strand pattern from 2026-05-19: linked Issue is closed
        # (work landed) but no PR title carries `T-NNN:` so the
        # renderer's PR-keyed path leaves the row in Open forever.
        # The closed-issue reaper should detect this via the scout's
        # closed_fleet_queued slice and move the row to Done with a
        # synthetic placeholder pointing at the issue URL.
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td, closed_fleet_queued=[
                {"number": 999, "title": "Some Engine Feature",
                 "labels": ["fleet:queued", "human:approved"]},
            ])
            text = _basic_tasks_md(open_status=" ")
            out = render(text, state_file=state, repo_key="engine")
            self.assertNotIn("- [ ] **Some Engine Feature**", out)
            self.assertNotIn("- [~] **Some Engine Feature**", out)
            self.assertIn("- [x] **T-200**", out)
            self.assertIn("(auto-reaped)", out)
            self.assertIn(
                "PR: https://github.com/jakildev/IrredenEngine/issues/999", out)

    def test_closed_issue_does_not_override_real_pr(self):
        # When both signals are present (linked Issue closed AND a real
        # merged PR matches via `T-NNN:` prefix), the real PR wins —
        # the Done entry carries the proper PR URL, not the synthetic
        # placeholder.
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td,
                prs_merged=[
                    _pr(200, "T-200: Some Engine Feature",
                        head="claude/T-200-feature",
                        merged_at="2026-05-08T20:00:00Z"),
                ],
                closed_fleet_queued=[
                    {"number": 999, "title": "Some Engine Feature",
                     "labels": ["fleet:queued", "human:approved"]},
                ])
            text = _basic_tasks_md(open_status="~",
                                   owner="claude/T-200-feature")
            out = render(text, state_file=state, repo_key="engine")
            self.assertIn("- [x] **T-200**", out)
            self.assertIn("PR: https://github.com/jakildev/IrredenEngine/pull/200",
                          out)
            self.assertNotIn("(auto-reaped)", out)

    def test_open_issue_does_not_reap(self):
        # Sanity check: a row whose linked Issue is still OPEN must
        # stay in Open. The reaper only fires on CLOSED issues.
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td)  # empty closed_fleet_queued
            text = _basic_tasks_md(open_status=" ")
            out = render(text, state_file=state, repo_key="engine")
            self.assertIn("- [ ] **Some Engine Feature**", out)
            self.assertNotIn("- [x] **T-200**", out)

    def test_stranded_merge_keeps_task_in_progress(self):
        # PR #543 regression: mergedAt is set but base != master, so
        # the commit isn't reachable from origin/master. Don't move
        # the task to Done.
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td, prs_merged=[
                _pr(200, "T-200: Some Engine Feature",
                    head="claude/T-200-feature",
                    merged_at="2026-05-08T20:00:00Z",
                    base="claude/some-feature-branch"),
            ])
            text = _basic_tasks_md(open_status="~",
                                   owner="claude/T-200-feature")
            out = render(text, state_file=state, repo_key="engine")
            # Still in Open as [~]
            self.assertIn("- [~] **Some Engine Feature**", out)
            # Not in Done
            self.assertNotIn("- [x] **T-200**", out)


class DoneSectionOrdering(unittest.TestCase):
    def test_done_sorted_by_merged_at_desc(self):
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td, prs_merged=[
                _pr(150, "T-150: Older",
                    merged_at="2026-05-01T10:00:00Z"),
                _pr(160, "T-160: Newer",
                    merged_at="2026-05-08T20:00:00Z"),
            ])
            text = """# TASKS

## Open

<!-- empty -->

## Done — last 20

"""
            out = render(text, state_file=state, repo_key="engine")
            t160_idx = out.find("**T-160**")
            t150_idx = out.find("**T-150**")
            self.assertGreater(t160_idx, 0)
            self.assertGreater(t150_idx, 0)
            self.assertLess(t160_idx, t150_idx,
                            "newer merge should appear above older")

    def test_done_capped_at_limit(self):
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td, prs_merged=[
                _pr(100 + i, f"T-{100+i}: Task {i}",
                    merged_at=f"2026-05-{i+1:02d}T00:00:00Z")
                for i in range(25)
            ])
            text = "# TASKS\n\n## Open\n\n## Done — last 20\n"
            out = render(text, state_file=state, repo_key="engine", done_limit=20)
            count = out.count("- [x] **T-")
            self.assertEqual(count, 20)

    def test_existing_title_is_preserved(self):
        # A hand-tuned Done entry's title should pass through even when
        # the merged-PR cache has a slightly different title.
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td, prs_merged=[
                _pr(100, "T-100: Pre-existing done entry but slightly different",
                    head="claude/T-100-foo",
                    merged_at="2026-05-08T20:00:00Z"),
            ])
            text = _basic_tasks_md()
            out = render(text, state_file=state, repo_key="engine")
            # Original hand-tuned title preserved.
            self.assertIn("**T-100** — Pre-existing done entry ·", out)


class BlockedByResolution(unittest.TestCase):
    def test_dropped_blocker_when_done(self):
        # T-200 is blocked by T-180; T-180 is done (merged-to-master).
        # The renderer should drop T-180 from T-200's Blocked-by line.
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td, prs_merged=[
                _pr(180, "T-180: Some Done Task",
                    merged_at="2026-05-08T19:00:00Z"),
            ])
            text = """# TASKS

## Open

<!-- -->

- [ ] **Blocked Task** — summary
  - **ID:** T-200
  - **Area:** engine
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-180
  - **Acceptance:** (1) ok
  - **Issue:** (none)
  - **Notes:** notes
  - **Links:**


## Done — last 20
"""
            out = render(text, state_file=state, repo_key="engine")
            self.assertIn("**Blocked Task**", out)
            t200_start = out.find("**Blocked Task**")
            done_start = out.find("## Done", t200_start)
            self.assertGreater(t200_start, 0, "T-200 task block missing")
            self.assertGreater(done_start, t200_start)
            blocked_section = out[t200_start:done_start]
            blocked_by_line = [l for l in blocked_section.split("\n")
                               if "Blocked by" in l]
            self.assertTrue(blocked_by_line, "blocked-by line missing")
            self.assertIn("(none)", blocked_by_line[0],
                          "blocker T-180 should be dropped (now done)")
            # T-180 should now appear in Done.
            self.assertIn("**T-180**", out[done_start:])

    def test_url_blocker_preserved(self):
        # Cross-repo blockers are PR URLs in plain text — preserve verbatim.
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td)
            text = _basic_tasks_md(
                blocked_by="https://github.com/jakildev/irreden/pull/100",
            )
            out = render(text, state_file=state, repo_key="engine")
            self.assertIn(
                "**Blocked by:** https://github.com/jakildev/irreden/pull/100",
                out,
            )


class FrontMatterAndFooter(unittest.TestCase):
    def test_passthrough_unchanged(self):
        # Front matter (everything before "## Open") should not change.
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td)
            text = _basic_tasks_md()
            out = render(text, state_file=state, repo_key="engine")
            front = text[:text.find("## Open")]
            self.assertTrue(out.startswith(front),
                            "front matter must pass through verbatim")

    def test_passthrough_human_authored_fields(self):
        # Notes and Acceptance must pass through verbatim.
        with tempfile.TemporaryDirectory() as td:
            state = _state_file(td)
            text = _basic_tasks_md()
            out = render(text, state_file=state, repo_key="engine")
            self.assertIn(
                "**Notes:** Hand-written notes that should pass through verbatim.",
                out,
            )
            self.assertIn(
                "**Acceptance:** (1) builds pass",
                out,
            )


if __name__ == "__main__":
    unittest.main()
