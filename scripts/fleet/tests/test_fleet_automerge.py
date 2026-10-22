"""Hermetic tests for fleet-automerge, the tier-0 auto-merge executor.

`gh` is a fail-closed PATH stub over one JSON state file: it models exactly
the shapes the executor issues (candidate list, PR view, paginated files,
label events, comments, the expected-head squash merge, label and comment
writes, the merged-revert search) and exits 2 on anything else. The conf, the
kill switch, the ledger and the lock all live under a temp HOME.
"""

import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUBJECT = ROOT / "fleet-automerge"
if not SUBJECT.is_file():
    print("SKIP: fleet-automerge subject absent", file=sys.stderr)
    sys.exit(3)
sys.path.insert(0, str(ROOT))
import fleet_merge_class as mc  # noqa: E402

SLUG = "jakildev/IrredenEngine"
NOW = "2026-10-20T12:00:00Z"
HEAD_A = "a" * 40
HEAD_B = "b" * 40
GREEN = [{"__typename": "CheckRun", "name": "linux-build", "workflowName": "Engine Tests",
          "status": "COMPLETED", "conclusion": "SUCCESS", "detailsUrl": "https://x/1"},
         {"__typename": "CheckRun", "name": "closes-lint", "workflowName": "PR closes lint",
          "status": "COMPLETED", "conclusion": "SKIPPED", "detailsUrl": "https://x/2"}]
REREVIEW_GREEN = {"__typename": "CheckRun", "name": "swap-approval-label",
                  "workflowName": "Auto re-review on push to approved PR",
                  "status": "COMPLETED", "conclusion": "SUCCESS", "detailsUrl": "https://x/rr"}
# Synthetic paths: shaped like the real trees, naming no tracked file.
REBLESS_FILES = ["creations/demos/fixture_demo/test/references/macos-debug/zoom4_origin.png",
                 "docs/pr-screenshots/fixture/after.png"]
DOCS_FILE = "docs/design/fixture-topic.md"
OBJECTIVE_FILE = "docs/design/objectives/fixture-objective.md"

GH_STUB = r'''#!/usr/bin/env python3
import json, os, sys

def load():
    with open(os.environ["GH_STUB_STATE"], encoding="utf-8") as f:
        return json.load(f)

def save(state):
    with open(os.environ["GH_STUB_STATE"], "w", encoding="utf-8") as f:
        json.dump(state, f, indent=1, sort_keys=True)

def log(argv):
    with open(os.environ["GH_STUB_LOG"], "a", encoding="utf-8") as f:
        f.write(" ".join(argv) + "\n")

def die(msg, rc=2):
    sys.stderr.write("stub gh: " + msg + "\n")
    sys.exit(rc)

def opt(argv, name):
    if name in argv:
        return argv[argv.index(name) + 1]
    return None

def view_obj(rec, number):
    return {"number": number, "title": rec["title"], "body": rec.get("body", ""),
            "state": rec["state"], "isDraft": rec.get("draft", False),
            "baseRefName": rec.get("base", "master"),
            "mergeable": rec.get("mergeable", "MERGEABLE"),
            "headRefOid": rec["head"], "labels": [{"name": n} for n in rec.get("labels", [])],
            "reviews": rec.get("reviews", []), "statusCheckRollup": rec.get("rollup", []),
            "changedFiles": rec.get("changedFiles", len(rec.get("files", []))),
            "url": "https://github.com/x/pull/%d" % number,
            "mergeCommit": ({"oid": rec["merge_sha"]} if rec.get("merge_sha") else None)}

def main():
    argv = sys.argv[1:]
    log(argv)
    state = load()
    if argv[:2] == ["pr", "list"]:
        st = opt(argv, "--state"); base = opt(argv, "--base"); label = opt(argv, "--label")
        fields = (opt(argv, "--json") or "").split(",")
        if st == "merged":
            rows = [r for r in state.get("reverts", [])]
            print(json.dumps([{k: r.get(k) for k in fields} for r in rows])); return
        rows = []
        for num, rec in sorted(state["pulls"].items(), key=lambda kv: int(kv[0])):
            if rec["state"] != "OPEN" or rec.get("base", "master") != base: continue
            if label and label not in rec.get("labels", []): continue
            rows.append({k: view_obj(rec, int(num))[k] for k in fields})
        print(json.dumps(rows)); return
    if argv[:2] == ["pr", "view"]:
        num = argv[2]; rec = state["pulls"].get(num) or die("no PR " + num, 1)
        obj = view_obj(rec, int(num)); fields = (opt(argv, "--json") or "").split(",")
        jq = opt(argv, "--jq")
        if jq == ".labels[].name":
            print("\n".join(n["name"] for n in obj["labels"]))
            return
        if jq: die("unmodeled jq " + jq)
        print(json.dumps({k: obj[k] for k in fields})); return
    if argv[:1] == ["api"]:
        path = argv[1]; jq = opt(argv, "--jq")
        parts = path.split("/")
        num = parts[4]; rec = state["pulls"].get(num) or die("no PR " + num, 1)
        if parts[3] == "pulls" and parts[5] == "files":
            if jq != ".[].filename": die("unmodeled jq " + str(jq))
            print("\n".join(rec.get("files", []))); return
        if parts[3] == "issues" and parts[5] == "events":
            print(json.dumps(rec.get("events", []))); return
        if parts[3] == "issues" and parts[5] == "comments":
            # The subject passes a literal backslash-n to jq; this file is
            # written from a raw string, so the escape below survives as one.
            if jq != '.[].body | split("\\n")[0]': die("unmodeled jq " + str(jq))
            print("\n".join(c.split("\n")[0] for c in rec.get("comments", []))); return
        die("unmodeled api " + path)
    if argv[:2] == ["pr", "merge"]:
        num = argv[2]; rec = state["pulls"][num]
        for flag in ("--squash", "--delete-branch"):
            if flag not in argv: die("merge without " + flag)
        if state.get("merge_fail"): die("merge refused by fixture", 1)
        if opt(argv, "--match-head-commit") != rec["head"]: die("head mismatch", 1)
        rec["state"] = "MERGED"; rec["merge_sha"] = "m" * 39 + num[-1]
        rec["merged_subject"] = opt(argv, "--subject")
        with open(opt(argv, "--body-file"), encoding="utf-8") as f:
            rec["merged_body"] = f.read()
        state["merges"] = state.get("merges", 0) + 1
        save(state); return
    if argv[:2] == ["pr", "edit"]:
        num = argv[2]; rec = state["pulls"][num]
        if state.get("label_fail"): die("label write refused by fixture", 1)
        lab = opt(argv, "--add-label")
        if lab not in rec["labels"]: rec["labels"].append(lab)
        save(state); return
    if argv[:2] == ["pr", "comment"]:
        num = argv[2]; rec = state["pulls"][num]
        if state.get("comment_fail"): die("comment refused by fixture", 1)
        with open(opt(argv, "--body-file"), encoding="utf-8") as f:
            rec.setdefault("comments", []).append(f.read())
        save(state); return
    die("unmodeled " + " ".join(argv))

main()
'''

NIT_STUB = '''#!/usr/bin/env bash
printf 'fleet-nit-close-for-pr %s\\n' "$*" >> "$GH_STUB_LOG"
[[ -f "$NIT_FAIL_FILE" ]] && exit 1
echo "closed #900 (nit of PR)"
exit 0
'''


def eligible_pr(number, head, labels=None, **over):
    rec = {"title": f"test(macos): re-bless {number}", "body": f"## Summary\n- bless {number}\n",
           "state": "OPEN", "draft": False, "base": "master", "mergeable": "MERGEABLE",
           "head": head, "labels": labels or ["fleet:approved", "fleet:author-claude",
                                              "fleet:needs-macos-smoke"],
           "reviews": [{"author": {"login": "jakildev"}, "body": "## Review\n**Verdict:** approve",
                        "commit": {"oid": head}, "state": "COMMENTED",
                        "submittedAt": "2026-10-20T11:00:00Z"}],
           "rollup": [dict(x) for x in GREEN], "files": list(REBLESS_FILES),
           "events": [{"event": "labeled", "label": {"name": "fleet:approved"},
                       "created_at": "2026-10-20T11:05:00Z"}],
           "comments": []}
    rec.update(over)
    return rec


class AutomergeCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)
        self.home = self.dir / "home"
        (self.home / ".fleet" / "logs").mkdir(parents=True)
        self.bin = self.dir / "bin"
        self.bin.mkdir()
        for name, text in (("gh", GH_STUB), ("fleet-nit-close-for-pr", NIT_STUB)):
            p = self.bin / name
            p.write_text(text, encoding="utf-8")
            p.chmod(p.stat().st_mode | stat.S_IXUSR)
        (self.bin / "gh.bat").write_text('@echo off\npython3 "%~dp0gh" %*\n', encoding="utf-8")
        self.state_file = self.dir / "gh-state.json"
        self.gh_log = self.dir / "gh.log"
        self.gh_log.write_text("")
        self.conf = self.home / ".fleet" / "fleet-up.conf"
        self.ledger = self.home / ".fleet" / "logs" / "automerge-audit.log"
        self.state_dir = self.dir / "state"
        self.state_dir.mkdir()
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith("FLEET_AUTOMERGE") and k not in ("GH_TOKEN",
                                                                         "GITHUB_TOKEN")}
        self.env.update({"HOME": str(self.home), "FLEET_STATE_DIR": str(self.state_dir),
                         "PATH": f"{self.bin}{os.pathsep}{os.environ.get('PATH', '')}",
                         "GH_STUB_STATE": str(self.state_file), "GH_STUB_LOG": str(self.gh_log),
                         "NIT_FAIL_FILE": str(self.dir / "nit-fail"),
                         "FLEET_AUTOMERGE_NOW": NOW, "GH_CONFIG_DIR": str(self.dir / "ghcfg"),
                         "GH_HOST": "github.invalid"})
        self.write_conf("FLEET_AUTOMERGE_CLASSES=rebless\n")
        self.set_state({"10": eligible_pr(10, HEAD_A)})

    def tearDown(self):
        self.temp.cleanup()

    def write_conf(self, text):
        self.conf.write_text(text, encoding="utf-8")

    def set_state(self, pulls, **extra):
        state = {"repo": SLUG, "pulls": pulls, **extra}
        self.state_file.write_text(json.dumps(state), encoding="utf-8")

    def state(self):
        return json.loads(self.state_file.read_text(encoding="utf-8"))

    def run_tool(self, *args, env_extra=None):
        env = dict(self.env)
        if env_extra:
            env.update(env_extra)
        return subprocess.run([sys.executable, str(SUBJECT), *args], capture_output=True,
                              text=True, timeout=120, env=env, check=False)

    def gh_calls(self, prefix):
        return [ln for ln in self.gh_log.read_text(encoding="utf-8").splitlines()
                if ln.startswith(prefix)]

    def events(self, kind=None):
        rows = []
        if self.ledger.exists():
            for line in self.ledger.read_text(encoding="utf-8").splitlines():
                if line.strip():
                    rows.append(json.loads(line))
        return [r for r in rows if kind is None or r["event"] == kind]

    def assert_no_writes(self):
        for prefix in ("pr merge", "pr edit", "pr comment", "fleet-nit-close-for-pr"):
            self.assertEqual(self.gh_calls(prefix), [], f"unexpected write: {prefix}")

    def assert_refused(self, reason_fragment):
        result = self.run_tool("--auto", "--repo", SLUG)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(reason_fragment, result.stderr)
        self.assert_no_writes()
        self.assertEqual(self.events("merge"), [])
        dry = self.run_tool("--dry-run", "--repo", SLUG)
        self.assertIn("human", dry.stdout)
        self.assertIn(reason_fragment, dry.stdout)
        return result


class Enablement(AutomergeCase):
    def test_disabled_lane_is_a_noop_logged_once(self):
        self.write_conf("FLEET_RUNTIMES=claude\n")
        first = self.run_tool("--auto", "--repo", SLUG)
        second = self.run_tool("--auto", "--repo", SLUG)
        self.assertEqual((first.returncode, second.returncode), (0, 0))
        self.assertIn("lane disabled", first.stderr)
        self.assertNotIn("lane disabled", second.stderr)
        self.assert_no_writes()
        self.assertEqual(self.gh_calls("pr list"), [])
        self.assertFalse(self.ledger.exists())

    def test_disabled_log_rearms_after_enablement(self):
        self.write_conf("FLEET_RUNTIMES=claude\n")
        self.run_tool("--auto", "--repo", SLUG)
        self.write_conf("FLEET_AUTOMERGE_CLASSES=rebless\n")
        self.run_tool("--auto", "--repo", SLUG)
        self.assertEqual(len(self.events("merge")), 1)
        self.write_conf("FLEET_RUNTIMES=claude\n")
        again = self.run_tool("--auto", "--repo", SLUG)
        self.assertIn("lane disabled", again.stderr)

    def test_environment_cannot_enable(self):
        self.write_conf("FLEET_RUNTIMES=claude\n")
        result = self.run_tool("--auto", "--repo", SLUG,
                               env_extra={"FLEET_AUTOMERGE_CLASSES": "rebless",
                                          "FLEET_AUTOMERGE_HOLD_SECS": "0"})
        self.assertIn("lane disabled", result.stderr)
        self.assert_no_writes()

    def test_cli_cannot_enable(self):
        self.write_conf("FLEET_RUNTIMES=claude\n")
        for argv in (["--auto", "--classes", "rebless"], ["--auto", "--enable", "rebless"],
                     ["--auto", "--dry-run"], ["--auto", "rebless"]):
            with self.subTest(argv=argv):
                result = self.run_tool(*argv, "--repo", SLUG)
                self.assertEqual(result.returncode, 2, result.stderr)
        self.assert_no_writes()

    def test_malformed_conf_enables_nothing(self):
        self.write_conf("FLEET_AUTOMERGE_CLASSES=rebless,code\n")
        result = self.run_tool("--auto", "--repo", SLUG)
        self.assertIn("lane disabled", result.stderr)
        self.assertIn("unknown class", result.stderr)
        self.assert_no_writes()

    def test_kill_switch_logged_once_and_rearmed(self):
        (self.home / ".fleet" / "automerge.off").write_text("")
        result = self.run_tool("--auto", "--repo", SLUG)
        self.assertIn("kill switch", result.stderr)
        self.assertNotIn("kill switch", self.run_tool("--auto", "--repo", SLUG).stderr)
        self.assert_no_writes()
        (self.home / ".fleet" / "automerge.off").unlink()
        self.run_tool("--auto", "--repo", SLUG)
        self.assertEqual(len(self.events("merge")), 1)
        (self.home / ".fleet" / "automerge.off").write_text("")
        self.assertIn("kill switch", self.run_tool("--auto", "--repo", SLUG).stderr)
        dry = self.run_tool("--dry-run", "--repo", SLUG)
        self.assertIn("kill-switch=present", dry.stdout)
        self.assertIn("0 would merge", dry.stdout)

    def test_lock_held_elsewhere_stands_down(self):
        lock = self.state_dir / "automerge.lock.d"
        lock.mkdir()
        (lock / "pid").write_text(f"{os.getpid()}\n")
        result = self.run_tool("--auto", "--repo", SLUG)
        self.assertIn("holds the lock", result.stderr)
        self.assert_no_writes()

    def test_stale_lock_is_broken(self):
        lock = self.state_dir / "automerge.lock.d"
        lock.mkdir()
        (lock / "pid").write_text("999999\n")
        self.run_tool("--auto", "--repo", SLUG)
        self.assertEqual(len(self.events("merge")), 1)


class Gates(AutomergeCase):
    def mutate(self, **over):
        self.set_state({"10": eligible_pr(10, HEAD_A, **over)})

    def test_draft(self):
        self.mutate(draft=True)
        self.assert_refused("draft")

    def test_closed(self):
        self.mutate(state="CLOSED")
        dry = self.run_tool("--dry-run", "--repo", SLUG)
        self.assertIn("0 candidate(s)", dry.stdout)

    def test_stacked_base(self):
        self.mutate(base="claude/4563-voxel-text")
        dry = self.run_tool("--dry-run", "--repo", SLUG)
        self.assertIn("0 candidate(s)", dry.stdout)
        self.run_tool("--auto", "--repo", SLUG)
        self.assert_no_writes()

    def test_mergeable_states(self):
        for state in ("CONFLICTING", "UNKNOWN", ""):
            with self.subTest(mergeable=state):
                self.gh_log.write_text("")
                self.mutate(mergeable=state)
                if state:
                    self.assert_refused(f"mergeable={state}")
                else:
                    self.assert_refused("unreadable field")

    def test_every_blocking_label_refuses(self):
        for label in sorted(mc.BLOCKING_EXACT) + [p + "mac-pool-2" for p in mc.BLOCKING_PREFIXES]:
            with self.subTest(label=label):
                self.gh_log.write_text("")
                self.mutate(labels=["fleet:approved", label])
                self.assert_refused(f"blocked by label {label}")

    def test_human_hold_refuses(self):
        self.mutate(labels=["fleet:approved", "human:hold"])
        self.assert_refused("blocked by label human:hold")

    def test_missing_approval_never_lists(self):
        self.mutate(labels=["fleet:needs-macos-smoke"])
        dry = self.run_tool("--dry-run", "--repo", SLUG)
        self.assertIn("0 candidate(s)", dry.stdout)

    def test_check_rollup_arms(self):
        failed = dict(GREEN[0], conclusion="FAILURE")
        neutral = dict(GREEN[0], conclusion="NEUTRAL")
        pending = dict(GREEN[0], status="IN_PROGRESS", conclusion=None)
        status_ctx = {"__typename": "StatusContext", "context": "perf-gate", "state": "PENDING"}
        for rollup, fragment in (([], "no checks on the head"),
                                 ([failed, GREEN[1]], "linux-build is COMPLETED/FAILURE"),
                                 ([GREEN[1], neutral], "linux-build is COMPLETED/NEUTRAL"),
                                 ([pending], "linux-build is IN_PROGRESS"),
                                 (GREEN + [status_ctx], "status perf-gate is PENDING")):
            with self.subTest(fragment=fragment):
                self.gh_log.write_text("")
                self.mutate(rollup=rollup)
                self.assert_refused(fragment)

    def test_one_code_file_drops_to_human(self):
        self.mutate(files=REBLESS_FILES + ["engine/render/src/trixel.cpp"])
        self.assert_refused("engine/render/src/trixel.cpp is outside every auto class")

    def test_file_count_mismatch_refuses(self):
        self.mutate(changedFiles=5)
        self.assert_refused("changed-file count 5 != listed 2")

    def test_docs_class_waits_for_its_wave(self):
        self.mutate(files=[DOCS_FILE])
        result = self.assert_refused("class docs not enabled")
        self.assertIn("class=auto:docs", result.stderr)

    def test_docs_merge_when_enabled(self):
        self.write_conf('FLEET_AUTOMERGE_CLASSES="rebless, docs"\n')
        self.mutate(files=[DOCS_FILE, "README.md"])
        self.run_tool("--auto", "--repo", SLUG)
        self.assertEqual(self.events("merge")[0]["class"], "docs")

    def test_instruction_markdown_never_merges(self):
        self.write_conf('FLEET_AUTOMERGE_CLASSES="rebless, docs"\n')
        for path in ("docs/agents/fixture-protocol.md", ".claude/commands/role-fixture.md",
                     OBJECTIVE_FILE, "engine/fixture/CLAUDE.md"):
            with self.subTest(path=path):
                self.gh_log.write_text("")
                self.mutate(files=[DOCS_FILE, path])
                self.assert_refused(f"{path} is outside every auto class")

    def test_review_on_old_head_without_rereview_refuses(self):
        self.mutate(reviews=[{"author": {"login": "jakildev"}, "body": "approve",
                              "commit": {"oid": HEAD_B}, "state": "COMMENTED",
                              "submittedAt": "2026-10-20T11:00:00Z"}])
        self.assert_refused("no review on the current head")

    def test_empty_review_body_does_not_count(self):
        self.mutate(reviews=[{"author": {"login": "jakildev"}, "body": "",
                              "commit": {"oid": HEAD_A}, "state": "COMMENTED",
                              "submittedAt": "2026-10-20T11:00:00Z"}])
        self.assert_refused("no review on the current head")

    def test_rereview_run_restores_provenance(self):
        self.mutate(reviews=[{"author": {"login": "jakildev"}, "body": "approve",
                              "commit": {"oid": HEAD_B}, "state": "COMMENTED",
                              "submittedAt": "2026-10-20T11:00:00Z"}],
                    rollup=GREEN + [REREVIEW_GREEN])
        dry = self.run_tool("--dry-run", "--repo", SLUG)
        self.assertIn("auto-rereview kept fleet:approved", dry.stdout)
        result = self.run_tool("--auto", "--repo", SLUG)
        self.assertIn("merged=1", result.stderr)
        merge = self.events("merge")[0]
        self.assertEqual(merge["provenance"]["review_sha"], HEAD_B)
        self.assertEqual(merge["provenance"]["rereview_run"], "https://x/rr")
        comment = self.state()["pulls"]["10"]["comments"][0]
        self.assertIn(f"auto-rereview kept fleet:approved on {HEAD_A[:9]}", comment)

    def test_hold_window(self):
        self.write_conf("FLEET_AUTOMERGE_CLASSES=rebless\nFLEET_AUTOMERGE_HOLD_SECS=1800\n")
        self.mutate(events=[{"event": "labeled", "label": {"name": "fleet:approved"},
                             "created_at": "2026-10-20T11:58:00Z"}])
        self.assert_refused("hold window: 1680s left")
        self.gh_log.write_text("")
        self.mutate(events=[{"event": "labeled", "label": {"name": "fleet:approved"},
                             "created_at": "2026-10-20T11:00:00Z"}])
        self.run_tool("--auto", "--repo", SLUG)
        self.assertEqual(len(self.events("merge")), 1)

    def test_hold_without_label_history_refuses(self):
        self.write_conf("FLEET_AUTOMERGE_CLASSES=rebless\nFLEET_AUTOMERGE_HOLD_SECS=60\n")
        self.mutate(events=[])
        self.assert_refused("no fleet:approved label event")

    def test_github_refusing_the_merge_is_recorded_not_retried(self):
        self.set_state({"10": eligible_pr(10, HEAD_A)}, merge_fail=True)
        result = self.run_tool("--auto", "--repo", SLUG)
        self.assertIn("merge refused by GitHub", result.stderr)
        self.assertEqual(len(self.gh_calls("pr merge")), 1)
        self.assertEqual(len(self.events("merge-failed")), 1)
        self.assertEqual(self.events("merge"), [])
        self.assertEqual(self.gh_calls("pr edit"), [])


class PositiveFire(AutomergeCase):
    def test_two_eligible_merge_exactly_one_per_run(self):
        self.set_state({"10": eligible_pr(10, HEAD_A), "11": eligible_pr(11, HEAD_B)})
        result = self.run_tool("--auto", "--repo", SLUG)
        self.assertEqual(result.returncode, 0, result.stderr)
        merges = self.gh_calls("pr merge")
        self.assertEqual(len(merges), 1)
        self.assertIn("pr merge 10 --repo", merges[0])
        self.assertIn(f"--match-head-commit {HEAD_A}", merges[0])
        self.assertIn("merged=1", result.stderr)
        rec = self.state()["pulls"]["10"]
        self.assertEqual(rec["state"], "MERGED")
        self.assertEqual(rec["merged_subject"], "test(macos): re-bless 10")
        self.assertEqual(rec["merged_body"], "## Summary\n- bless 10\n")
        self.assertIn("fleet:auto-merged", rec["labels"])
        self.assertIn("fleet:needs-macos-smoke", rec["labels"])
        self.assertEqual(len(rec["comments"]), 1)
        self.assertTrue(rec["comments"][0].startswith("Auto-merged (class=rebless): approved by "
                                                      "jakildev on aaaaaaaaa"))
        self.assertIn("hold 0s elapsed", rec["comments"][0])
        self.assertEqual(self.gh_calls("fleet-nit-close-for-pr"),
                         [f"fleet-nit-close-for-pr --repo {SLUG}"])
        kinds = [e["event"] for e in self.events()]
        self.assertEqual(kinds, ["merge", "audit-complete"])
        merge = self.events("merge")[0]
        self.assertEqual((merge["pr"], merge["class"], merge["head"]), (10, "rebless", HEAD_A))
        self.assertEqual(merge["merge_sha"], "m" * 39 + "0")
        self.assertEqual(merge["provenance"]["review_author"], "jakildev")
        self.assertEqual(self.events("audit-complete")[0]["nit_closed"],
                         ["closed #900 (nit of PR)"])
        self.assertEqual(self.state()["pulls"]["11"]["state"], "OPEN")

        self.gh_log.write_text("")
        again = self.run_tool("--auto", "--repo", SLUG)
        self.assertEqual(again.returncode, 0, again.stderr)
        merges = self.gh_calls("pr merge")
        self.assertEqual(len(merges), 1)
        self.assertIn("pr merge 11 --repo", merges[0])
        self.assertEqual([e["pr"] for e in self.events("merge")], [10, 11])

        self.gh_log.write_text("")
        third = self.run_tool("--auto", "--repo", SLUG)
        self.assertIn("candidates=0 merged=0", third.stderr)
        self.assert_no_writes()

    def test_dry_run_partitions_and_writes_nothing(self):
        self.set_state({"10": eligible_pr(10, HEAD_A),
                        "12": eligible_pr(12, HEAD_B, files=["engine/a.cpp"]),
                        "13": eligible_pr(13, HEAD_B, labels=["fleet:approved", "fleet:has-nits"])})
        result = self.run_tool("--dry-run", "--repo", SLUG)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("enabled=rebless", result.stdout)
        self.assertIn("PR #10  auto:rebless", result.stdout)
        self.assertIn("would merge: approved by jakildev", result.stdout)
        self.assertIn("PR #12  human", result.stdout)
        self.assertIn("engine/a.cpp is outside every auto class", result.stdout)
        self.assertIn("PR #13  human", result.stdout)
        self.assertIn("blocked by label fleet:has-nits", result.stdout)
        self.assertIn("class=auto:rebless", result.stdout)
        self.assertIn("3 candidate(s), 1 would merge, 0 writes", result.stdout)
        self.assert_no_writes()
        self.assertFalse(self.ledger.exists())

    def test_engine_namespace_resolves(self):
        result = self.run_tool("--dry-run", "--repo", "engine")
        self.assertIn(f"repo={SLUG}", result.stdout)
        bad = self.run_tool("--dry-run", "--repo", "nope")
        self.assertEqual(bad.returncode, 2)


class AuditRecovery(AutomergeCase):
    def test_partial_audit_completes_on_the_next_run_without_a_second_merge(self):
        self.set_state({"10": eligible_pr(10, HEAD_A)}, comment_fail=True)
        first = self.run_tool("--auto", "--repo", SLUG)
        self.assertIn("audit step comment failed", first.stderr)
        self.assertEqual([e["event"] for e in self.events()], ["merge", "audit-error"])
        self.assertEqual(self.events("audit-error")[0]["step"], "comment")
        self.assertIn("fleet:auto-merged", self.state()["pulls"]["10"]["labels"])

        state = self.state()
        state["comment_fail"] = False
        self.state_file.write_text(json.dumps(state), encoding="utf-8")
        self.gh_log.write_text("")
        second = self.run_tool("--auto", "--repo", SLUG)
        self.assertIn("completing the audit left behind", second.stderr)
        self.assertEqual(self.gh_calls("pr merge"), [])
        self.assertEqual(len(self.state()["pulls"]["10"]["comments"]), 1)
        self.assertEqual([e["event"] for e in self.events()],
                         ["merge", "audit-error", "audit-complete"])

        self.gh_log.write_text("")
        third = self.run_tool("--auto", "--repo", SLUG)
        self.assertNotIn("completing the audit", third.stderr)
        self.assertEqual(self.gh_calls("pr comment"), [])
        self.assertEqual(len(self.events()), 3)

    def test_audit_retries_are_bounded(self):
        self.set_state({"10": eligible_pr(10, HEAD_A)}, comment_fail=True)
        for _ in range(4):
            self.run_tool("--auto", "--repo", SLUG)
        self.assertEqual(len(self.events("audit-error")), 3)
        self.gh_log.write_text("")
        result = self.run_tool("--auto", "--repo", SLUG)
        self.assertIn("a human finishes it", result.stderr)
        self.assertEqual(self.gh_calls("pr comment"), [])
        self.assertEqual(len(self.gh_calls("pr merge")), 0)


class Rollbacks(AutomergeCase):
    def merged_ledger(self):
        self.run_tool("--auto", "--repo", SLUG)
        self.assertEqual(len(self.events("merge")), 1)

    def test_no_revert_records_nothing(self):
        self.merged_ledger()
        result = self.run_tool("--reconcile", "--repo", SLUG)
        self.assertIn("rollbacks recorded=0", result.stderr)
        self.assertEqual(self.events("rollback"), [])

    def test_exact_revert_records_one_rollback_once(self):
        self.merged_ledger()
        state = self.state()
        state["reverts"] = [{"number": 20, "title": 'Revert "test(macos): re-bless 10"',
                             "body": f"Reverts {SLUG}#10\n", "mergeCommit": {"oid": "r" * 40}}]
        self.state_file.write_text(json.dumps(state), encoding="utf-8")
        self.run_tool("--reconcile", "--repo", SLUG)
        self.run_tool("--reconcile", "--repo", SLUG)
        rollbacks = self.events("rollback")
        self.assertEqual(len(rollbacks), 1)
        self.assertEqual((rollbacks[0]["pr"], rollbacks[0]["revert_pr"]), (10, 20))
        self.assertEqual(rollbacks[0]["merge_sha"], "m" * 39 + "0")
        self.assertEqual(rollbacks[0]["revert_merge_sha"], "r" * 40)

    def test_two_reverts_of_one_pr_record_one_rollback(self):
        self.merged_ledger()
        state = self.state()
        state["reverts"] = [
            {"number": 20, "title": 'Revert "test(macos): re-bless 10"',
             "body": f"Reverts {SLUG}#10\n", "mergeCommit": {"oid": "r" * 40}},
            {"number": 21, "title": 'Revert "test(macos): re-bless 10"',
             "body": f"Reverts {SLUG}#10\n", "mergeCommit": {"oid": "s" * 40}},
        ]
        self.state_file.write_text(json.dumps(state), encoding="utf-8")
        result = self.run_tool("--reconcile", "--repo", SLUG)
        self.assertIn("rollbacks recorded=1", result.stderr)
        self.run_tool("--reconcile", "--repo", SLUG)
        rollbacks = self.events("rollback")
        self.assertEqual(len(rollbacks), 1)
        self.assertEqual((rollbacks[0]["pr"], rollbacks[0]["revert_pr"]), (10, 20))

    def test_lookalikes_do_not_count(self):
        self.merged_ledger()
        state = self.state()
        state["reverts"] = [
            {"number": 21, "title": 'Revert "something else"', "body": "no backlink here",
             "mergeCommit": {"oid": "1" * 40}},
            {"number": 22, "title": "fix: undo the re-bless", "body": f"Reverts {SLUG}#10",
             "mergeCommit": {"oid": "2" * 40}},
            {"number": 23, "title": 'Revert "x"', "body": f"Reverts {SLUG}#100",
             "mergeCommit": {"oid": "3" * 40}},
        ]
        self.state_file.write_text(json.dumps(state), encoding="utf-8")
        self.run_tool("--reconcile", "--repo", SLUG)
        self.assertEqual(self.events("rollback"), [])

    def test_reconcile_without_merges_queries_nothing(self):
        result = self.run_tool("--reconcile", "--repo", SLUG)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(self.gh_calls("pr list"), [])


if __name__ == "__main__":
    unittest.main()
