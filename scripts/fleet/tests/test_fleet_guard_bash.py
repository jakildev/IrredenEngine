"""Hermetic tests for fleet-guard-bash, the PreToolUse Bash hook, and its policy module.

  * One deny table per rule. Every case asserts WHICH rule denied it, so a
    rule that stops firing turns its own table red even where another rule
    still denies the line.
  * One allow table.
  * `RuleMutation` drops each rule in turn: the other three tables stay
    attributed, and the dropped rule's leading case is allowed outright.
  * The repository lookups run against fixture repositories under a temp
    directory (fake remotes, no network); a failing, a hung and an absent
    `git` are driven through the module's lookup seam.
  * The shim end to end, under every bash on the host.

`@name@` in a command is replaced by that fixture repository's path.
"""
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SHIM = ROOT / "fleet-guard-bash"
POLICY = ROOT / "fleet_guard_bash.py"
if not SHIM.is_file() or not POLICY.is_file():
    print("SKIP: fleet-guard-bash subject absent", file=sys.stderr)
    sys.exit(3)
sys.path.insert(0, str(ROOT))
import fleet_guard_bash as guard  # noqa: E402
from fleet_runtime import REPO_SLUGS  # noqa: E402

SETTINGS = ROOT.parents[1] / ".claude" / "settings.json"
BASHES = sorted({path for path in (shutil.which("bash"), "/bin/bash")
                 if path and os.path.exists(path)}) or ["bash"]

ENGINE_SSH = "git@github.com:jakildev/IrredenEngine.git"
ENGINE_HTTPS = "https://github.com/jakildev/IrredenEngine.git"
GAME_HTTPS = "https://github.com/jakildev/irreden"

REPOS = {}
_STATE = {}


def _git(*args):
    subprocess.run(["git", *args], check=True, stdin=subprocess.DEVNULL,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def _make_repo(name, branch, remotes):
    path = _STATE["root"] / name
    _git("init", "-q", str(path))
    _git("-C", str(path), "symbolic-ref", "HEAD", "refs/heads/" + branch)
    for remote, url in remotes.items():
        _git("-C", str(path), "remote", "add", remote, url)
    REPOS[name] = str(path)
    return path


def setUpModule():
    tmp = tempfile.TemporaryDirectory(prefix="guard-bash.")
    root = Path(tmp.name).resolve()
    home = root / "home"
    (home / ".fleet" / "logs").mkdir(parents=True)
    (root / "gitconfig").write_text("", encoding="utf-8")
    env = mock.patch.dict(os.environ, {
        "HOME": str(home),
        "FLEET_HOME": str(home / ".fleet"),
        "GIT_CONFIG_GLOBAL": str(root / "gitconfig"),
        "GIT_CONFIG_NOSYSTEM": "1",
    })
    env.start()
    for name in ("GIT_DIR", "GIT_WORK_TREE"):
        os.environ.pop(name, None)
    _STATE.update(tmp=tmp, root=root, env=env, log=home / ".fleet" / "logs" / "guard-bash.log")

    _make_repo("fleet_feature", "claude/feature", {
        "origin": ENGINE_SSH,
        "fork": "https://github.com/someone-else/IrredenEngine.git",
    })
    _make_repo("fleet_master", "master", {"origin": ENGINE_HTTPS})
    _make_repo("game_main", "main", {"origin": GAME_HTTPS})
    _make_repo("scratch_master", "master", {
        "origin": str(root / "bare.git"),
        "upstream": "ssh://git@github.com/JakilDev/IrredenEngine/",
    })
    _make_repo("no_remote_master", "master", {})
    (root / "not_a_repo").mkdir()
    REPOS["not_a_repo"] = str(root / "not_a_repo")

    base = _make_repo("fleet_worktree_base", "claude/base", {"origin": ENGINE_HTTPS})
    _git("-C", str(base), "-c", "user.name=fixture", "-c", "user.email=fixture@example.invalid",
         "commit", "-q", "--allow-empty", "-m", "fixture")
    worktree = root / "fleet_worktree_main"
    _git("-C", str(base), "worktree", "add", "-q", "-b", "main", str(worktree))
    REPOS["fleet_worktree_main"] = str(worktree)


def tearDownModule():
    _STATE["env"].stop()
    _STATE["tmp"].cleanup()


def expand(command):
    for name, path in REPOS.items():
        command = command.replace("@%s@" % name, path)
    return command


def rule_for(command, where="fleet_feature"):
    """The name of the rule that denies `command` run from fixture `where`, or None."""
    verdict = guard.evaluate(expand(command), REPOS[where])
    return verdict[0] if verdict else None


def log_text():
    log = _STATE["log"]
    return log.read_text(encoding="utf-8") if log.exists() else ""


def cases(default, *rows):
    """Normalize rows to (command, fixture): a bare string runs from `default`."""
    return [(row, default) if isinstance(row, str) else row for row in rows]


HEREDOC_MESSAGE = (
    "\"$(cat <<'EOF'\nsubject line\n\nbody: git push origin master, gh pr merge\nEOF\n)\"")

DENY = {}

DENY["push-protected"] = cases(
    "fleet_feature",
    # Every refspec spelling whose destination is a protected branch.
    "git push origin master",
    "git push origin main",
    "git push origin HEAD:master",
    "git push origin HEAD:main",
    "git push origin claude/feature:master",
    "git push origin +claude/feature:master",
    "git push origin +HEAD:master",
    "git push origin +master",
    "git push origin refs/heads/master",
    "git push origin HEAD:refs/heads/master",
    "git push origin +refs/heads/claude/feature:refs/heads/main",
    "git push origin :master",
    "git push origin --delete master",
    "git push --delete origin main",
    "git push -d origin master",
    "git push origin claude/feature master",
    "git push origin master claude/feature",
    "git push origin tag v1 master",
    # Options around the refspec.
    "git push -u origin master",
    "git push --set-upstream origin HEAD:master",
    "git push -f origin master",
    "git push --force origin master",
    "git push --force-with-lease origin HEAD:master",
    "git push --force-with-lease=master:abc123 origin master",
    "git push origin HEAD:master --dry-run",
    "git push --dry-run origin master",
    "git push -n origin master",
    "git push -o ci.skip origin master",
    "git push --push-option=ci.skip origin HEAD:master",
    "git push --push-option ci.skip origin HEAD:master",
    "git push --no-verify origin master",
    "git push -q --atomic origin master",
    # Every branch at once.
    "git push --mirror",
    "git push --mirror origin",
    "git push origin --mirror",
    "git push --all",
    "git push --all origin",
    "git push origin --all",
    "git push --branches origin",
    # git's global options.
    ("git -C @fleet_feature@ push origin HEAD:master", "scratch_master"),
    ("git -C @fleet_feature@ push --all", "scratch_master"),
    ("git -C @scratch_master@ -C ../fleet_feature push origin master", "scratch_master"),
    "git -c user.name=x push origin master",
    "git -c push.default=current -c user.email=x@example.invalid push origin HEAD:main",
    "git --no-pager push origin master",
    "git -P push origin master",
    "git --namespace fixture push origin master",
    # Environment and wrapper prefixes.
    "GH_TOKEN=abc git push origin master",
    'GIT_SSH_COMMAND="ssh -i key" git push origin HEAD:main',
    "A=1 B=2 git push origin master",
    "env git push origin master",
    "env GIT_TRACE=1 git push origin master",
    "env -i PATH=/usr/bin git push origin master",
    "env -u GIT_TRACE git push origin master",
    "command git push origin master",
    "exec git push origin master",
    "nohup git push origin master",
    "time git push origin master",
    "time -p git push origin master",
    "timeout 30 git push origin master",
    "timeout -k 5 30 git push origin master",
    "timeout --signal=KILL 30s git push origin master",
    "echo x | xargs git push origin master",
    "xargs -n 1 git push origin master",
    "xargs -I {} git push origin {}:master",
    "/usr/bin/git push origin master",
    "/opt/homebrew/bin/git push origin HEAD:master",
    "git.exe push origin master",
    # Compound lines.
    "cd x && git push origin HEAD:master",
    ("cd @fleet_feature@ && git push origin HEAD:master", "scratch_master"),
    ("cd @fleet_feature@; git push origin master", "scratch_master"),
    "git status; git push origin master",
    "git fetch origin && git push origin HEAD:master",
    "git fetch origin || git push origin master",
    "git status & git push origin master",
    "git status | git push origin master",
    "git status\ngit push origin master",
    "git add -A && git commit -m wip && git push origin master",
    "if git push origin master; then echo ok; fi",
    "while ! git push origin master; do sleep 1; done",
    "for r in a b; do git push origin master; done",
    "{ git push origin master; }",
    "! git push origin master",
    "true && { git fetch; git push origin HEAD:master; }",
    "case x in x) git push origin master ;; esac",
    # Subshells and substitutions.
    "(git push origin master)",
    ("( cd @fleet_feature@ && git push origin master )", "scratch_master"),
    "echo $(git push origin master)",
    'echo "$(git push origin HEAD:master)"',
    "echo `git push origin master`",
    "result=$(git push origin master 2>&1)",
    "diff <(git push origin master) /dev/null",
    "echo ${X:-$(git push origin master)}",
    # A nested shell.
    'bash -c "git push origin master"',
    "bash -c 'git push origin HEAD:master'",
    "bash -lc 'git push origin master'",
    "bash -c 'cd . && git push origin master'",
    "sh -c 'git push origin master'",
    "zsh -c 'git push origin master'",
    "/bin/bash -c 'git push origin master'",
    "bash -o pipefail -c 'git fetch | cat; git push origin master'",
    "bash -c \"bash -c 'git push origin master'\"",
    "env bash -c 'git push origin master'",
    'eval "git push origin master"',
    "eval git push origin master",
    # Quoting.
    'git push "origin" "master"',
    "git push origin 'HEAD:master'",
    'git push origin "+HEAD:master"',
    "git 'push' origin master",
    '"git" push origin master',
    "git push origin mas\\ter",
    "git push origin $'master'",
    'git push origin HEAD:"master"',
    "git push \\\n  origin master",
    "git push origin master \\",
    # Redirections and comments.
    "git push origin master > /dev/null 2>&1",
    "git push origin master 2>&1 | tail -5",
    "git push origin master &> push.log",
    "git push origin master </dev/null",
    "git push origin master # ship it",
    "git push origin master <<EOF\nbody\nEOF",
    "cat <<EOF && git push origin master",
    # A URL in place of a remote name.
    ("git push https://github.com/jakildev/IrredenEngine.git HEAD:master", "scratch_master"),
    ("git push https://github.com/jakildev/IrredenEngine HEAD:master", "scratch_master"),
    ("git push git@github.com:jakildev/IrredenEngine.git master", "scratch_master"),
    ("git push ssh://git@github.com/jakildev/irreden.git HEAD:main", "scratch_master"),
    ("git push https://x-access-token:abc@github.com/JAKILDEV/irredenengine.git master",
     "scratch_master"),
    # A fleet remote under another name.
    ("git push upstream master", "scratch_master"),
    ("git push upstream HEAD:main", "scratch_master"),
    ("git push --all upstream", "scratch_master"),
    # The repository cannot be resolved and the refspec names the branch.
    'git push "$REMOTE" master',
    'git -C "$DIR" push origin master',
    'cd "$DIR" && git push origin HEAD:master',
    "cd - && git push origin master",
    "git push nosuchremote master",
    ("git push origin master", "not_a_repo"),
    ("git push origin HEAD:main", "no_remote_master"),
    "GIT_DIR=/nonexistent/.git git push origin master",
    "git --git-dir=@fleet_feature@/.git push origin master",
    "git --work-tree=. push origin master",
    # No refspec, or a bare HEAD, while the current branch is protected.
    ("git push", "fleet_master"),
    ("git push origin", "fleet_master"),
    ("git push -u origin HEAD", "fleet_master"),
    ("git push origin +HEAD", "fleet_master"),
    ("git push origin @", "fleet_master"),
    ("git push --force-with-lease", "fleet_master"),
    ("git push --follow-tags", "fleet_master"),
    ("git push origin HEAD", "game_main"),
    ("git push origin main", "game_main"),
    ("git push", "fleet_worktree_main"),
    ("git push upstream", "scratch_master"),
    ("git -C @fleet_master@ push", "scratch_master"),
    ("cd @fleet_master@ && git push", "scratch_master"),
    ("git add -A && git push", "fleet_master"),
    ("git status && git push origin HEAD", "game_main"),
)

DENY["merge"] = cases(
    "fleet_feature",
    # gh pr merge.
    "gh pr merge",
    "gh pr merge 123",
    "gh pr merge 123 --squash",
    "gh pr merge 123 --squash --delete-branch",
    "gh pr merge --merge 123",
    "gh pr merge --rebase 123",
    "gh pr merge --auto --squash 123",
    "gh pr merge --admin 123",
    "gh pr merge https://github.com/jakildev/IrredenEngine/pull/123",
    "gh pr merge 123 -R jakildev/IrredenEngine",
    "gh pr merge -R jakildev/IrredenEngine 123",
    "gh pr merge --repo jakildev/IrredenEngine 123",
    "gh -R jakildev/IrredenEngine pr merge 123",
    "gh --repo jakildev/IrredenEngine pr merge 123",
    "gh --repo=jakildev/IrredenEngine pr merge 123",
    "gh pr -R jakildev/IrredenEngine merge 123",
    "gh pr --repo jakildev/IrredenEngine merge 123",
    "GH_TOKEN=abc gh pr merge 123",
    'GH_TOKEN="$(cat token)" gh pr merge 123',
    "GH_REPO=jakildev/IrredenEngine gh pr merge 123",
    "env GH_TOKEN=abc gh pr merge 123",
    "/opt/homebrew/bin/gh pr merge 123",
    "scripts/fleet/gh pr merge 123",
    "command gh pr merge 123",
    "timeout 60 gh pr merge 123",
    "gh pr view 123 && gh pr merge 123",
    "gh pr checks 123 --watch; gh pr merge 123 --squash",
    "gh pr merge 123 | cat",
    "gh pr merge 123 > /dev/null",
    'bash -c "gh pr merge 123"',
    "sh -c 'gh pr merge 123 --squash'",
    "eval gh pr merge 123",
    "(gh pr merge 123)",
    "echo $(gh pr merge 123)",
    # Backticks inside double quotes are a substitution: the shell runs this.
    'git commit -m "note: `gh pr merge 123` is denied"',
    'gh pr merge "$PR"',
    "gh 'pr' \"merge\" 123",
    # The REST merge endpoints, with any method that is not a read.
    "gh api -X PUT repos/jakildev/IrredenEngine/pulls/123/merge",
    "gh api -XPUT repos/jakildev/IrredenEngine/pulls/123/merge",
    "gh api -X put repos/o/r/pulls/123/merge",
    "gh api --method PUT repos/o/r/pulls/123/merge",
    "gh api --method=PUT /repos/o/r/pulls/123/merge",
    "gh api repos/o/r/pulls/123/merge -X PUT",
    "gh api repos/o/r/pulls/123/merge -X PUT -f merge_method=squash",
    "gh api repos/o/r/pulls/123/merge --method PUT --field merge_method=squash",
    "gh api -X POST repos/o/r/pulls/123/merge",
    "gh api -X DELETE repos/o/r/pulls/123/merge",
    "gh api -iX PUT repos/o/r/pulls/123/merge",
    "gh api -X PUT repos/o/r/pulls/123/merge/",
    "gh api -X PUT 'repos/o/r/pulls/123/merge?ref=x'",
    'gh api -X PUT "repos/$REPO/pulls/$PR/merge"',
    "gh api -X PUT repos/{owner}/{repo}/pulls/123/merge",
    "gh api -X PUT https://api.github.com/repos/o/r/pulls/123/merge",
    'gh api -H "Accept: application/vnd.github+json" -X PUT repos/o/r/pulls/123/merge',
    "gh api --hostname github.com -X PUT repos/o/r/pulls/123/merge",
    "gh -R o/r api -X PUT repos/o/r/pulls/123/merge",
    "GH_TOKEN=abc gh api -X PUT repos/o/r/pulls/123/merge",
    # A field or an input file makes the default method POST.
    "gh api repos/o/r/pulls/123/merge -f merge_method=squash",
    "gh api repos/o/r/pulls/123/merge -F sha=abc",
    "gh api repos/o/r/pulls/123/merge -fmerge_method=squash",
    "gh api repos/o/r/pulls/123/merge --field merge_method=squash",
    "gh api repos/o/r/pulls/123/merge --field=merge_method=squash",
    "gh api repos/o/r/pulls/123/merge --raw-field commit_title=x",
    "gh api repos/o/r/pulls/123/merge --input body.json",
    "gh api --input - repos/o/r/pulls/123/merge",
    "gh api -X POST repos/o/r/merges -f base=master -f head=claude/feature",
    "gh api repos/o/r/merges -f base=release -f head=claude/feature",
    "gh api --method POST /repos/o/r/merges --input merge.json",
    # GraphQL mutations.
    "gh api graphql -f query='mutation { mergePullRequest(input: {pullRequestId: \"x\"})"
    " { clientMutationId } }'",
    "gh api graphql -f query='mutation($id: ID!) { enablePullRequestAutoMerge(input:"
    " {pullRequestId: $id}) { clientMutationId } }' -f id=x",
    "gh api graphql -F query='mutation { mergeBranch(input: {base: \"master\", head: \"f\"})"
    " { clientMutationId } }'",
    "gh api graphql -f query='mutation { updateRef(input: {refId: \"x\", oid: \"abc\"})"
    " { clientMutationId } }'",
    "gh api graphql -f query='mutation { createCommitOnBranch(input: {branch:"
    " {branchName: \"master\"}}) { clientMutationId } }'",
    "gh api graphql -f query='mutation { updateBranchProtectionRule(input:"
    " {branchProtectionRuleId: \"x\"}) { clientMutationId } }'",
    "gh api graphql -f query='mutation { deleteBranchProtectionRule(input:"
    " {branchProtectionRuleId: \"x\"}) { clientMutationId } }'",
    'gh api graphql --raw-field query="mutation { mergePullRequest(input:'
    ' {pullRequestId: \\"$ID\\"}) { clientMutationId } }"',
    # Writes to a protected branch's ref or to its protection.
    "gh api -X PATCH repos/o/r/git/refs/heads/master -f sha=abc -F force=true",
    "gh api -X PATCH repos/o/r/git/refs/heads/main -f sha=abc",
    "gh api -X DELETE repos/o/r/git/refs/heads/master",
    "gh api repos/o/r/git/refs/heads/master -f sha=abc",
    "gh api -X PUT repos/o/r/branches/master/protection --input protection.json",
    "gh api -X DELETE repos/o/r/branches/main/protection",
    "gh api -X DELETE repos/o/r/branches/master/protection/enforce_admins",
    "gh api -X PATCH repos/o/r/branches/master/protection/required_pull_request_reviews"
    " -F required_approving_review_count=0",
    # The tier-0 executor in command position.
    "fleet-automerge",
    "fleet-automerge --dry-run",
    "fleet-automerge --once --repo engine",
    "scripts/fleet/fleet-automerge",
    "./scripts/fleet/fleet-automerge",
    "./fleet-automerge",
    "~/bin/fleet-automerge",
    "/Users/someone/bin/fleet-automerge --once",
    '"$HOME"/bin/fleet-automerge',
    "python3 scripts/fleet/fleet-automerge",
    "python3 ./scripts/fleet/fleet-automerge --once",
    "python scripts/fleet/fleet-automerge",
    "python3 -u scripts/fleet/fleet-automerge",
    "python3.12 -X dev scripts/fleet/fleet-automerge",
    "/usr/bin/env python3 scripts/fleet/fleet-automerge",
    "bash scripts/fleet/fleet-automerge",
    "sh ./scripts/fleet/fleet-automerge",
    "bash -x scripts/fleet/fleet-automerge",
    "env FLEET_HOME=/tmp/fixture fleet-automerge",
    "FLEET_AUTOMERGE_NOW=1 fleet-automerge",
    "nohup fleet-automerge &",
    "cd scripts/fleet && ./fleet-automerge",
    "timeout 300 fleet-automerge --once",
    "fleet-automerge 2>&1 | tee merge.log",
    "bash -c 'fleet-automerge --once'",
    "exec fleet-automerge",
)

DENY["skip-hooks"] = cases(
    "fleet_feature",
    # --no-verify and commit's -n, alone and inside a short-flag cluster.
    "git commit --no-verify",
    'git commit --no-verify -m "wip"',
    'git commit -m "wip" --no-verify',
    "git commit -n",
    'git commit -n -m "wip"',
    'git commit -m "wip" -n',
    'git commit -nm "wip"',
    'git commit -anm "wip"',
    "git commit -anmwip",
    "git commit -an -m wip",
    "git commit -qn -m wip",
    "git commit -nq -m wip",
    "git commit -sn -m wip",
    "git commit -nF message.txt",
    "git commit -F message.txt -n",
    "git commit -n -C HEAD",
    "git commit -c ORIG_HEAD -n",
    "git commit -S -n -m wip",
    "git commit -uno -n -m wip",
    "git commit --amend --no-edit --no-verify",
    "git commit --amend -n --no-edit",
    "git commit -a --no-verify --message=wip",
    "git commit --message wip --no-verify",
    "git commit --fixup HEAD~1 --no-verify",
    "git commit --no-verify -- src/file.cpp",
    "git commit --no-verify -m " + HEREDOC_MESSAGE,
    'git commit -n -m "$(cat <<EOF\nsubject\nEOF\n)"',
    # Prefixes and compound lines.
    ("git -C @fleet_feature@ commit --no-verify -m wip", "scratch_master"),
    "git --no-pager commit -n -m wip",
    "HUSKY=0 git commit --no-verify -m wip",
    "GIT_AUTHOR_NAME=x git commit -n -m wip",
    "env GIT_COMMITTER_DATE=now git commit --no-verify -m wip",
    'git add -A && git commit --no-verify -m "wip"',
    "git add -A; git commit -n -m wip; git push -u origin HEAD",
    ("cd @fleet_feature@ && git commit -nm wip", "scratch_master"),
    "bash -c 'git commit -n -m wip'",
    "bash -c \"git commit --no-verify -m 'wip'\"",
    "(git commit --no-verify -m wip)",
    # Not scoped to the fleet's repositories.
    ("git commit --no-verify -m wip", "scratch_master"),
    ("git commit -n -m wip", "not_a_repo"),
    # push and merge.
    "git push --no-verify",
    "git push --no-verify origin claude/feature",
    "git push origin claude/feature --no-verify",
    "git push -u origin HEAD --no-verify",
    "git push --force-with-lease --no-verify origin claude/feature",
    "git merge --no-verify origin/master",
    "git merge origin/master --no-verify --no-edit",
    'git merge --no-ff --no-verify claude/other -m "merge other"',
    'git merge -m "sync" --no-verify origin/master',
    # A hooks path override on the same three subcommands.
    "git -c core.hooksPath=/dev/null commit -m wip",
    "git -c core.hookspath=/dev/null commit -m wip",
    "git -c CORE.HOOKSPATH= commit -m wip",
    "git -c core.hooksPath= commit -m wip",
    'git -c "core.hooksPath=/dev/null" commit -m wip',
    "git -c user.name=x -c core.hooksPath=/dev/null commit -m wip",
    "git -c core.hooksPath=/dev/null -C @fleet_feature@ commit -m wip",
    "git -c core.hooksPath=/dev/null push origin claude/feature",
    "git -c core.hooksPath=/dev/null push",
    "git -c core.hooksPath=/dev/null merge origin/master",
)

DENY["commit-protected"] = cases(
    "fleet_master",
    "git commit",
    'git commit -m "wip"',
    'git commit -am "wip"',
    "git commit -a -m wip",
    "git commit --amend --no-edit",
    "git commit --allow-empty -m wip",
    "git commit -F message.txt",
    "git commit -m wip -- src/file.cpp",
    "git commit -m " + HEREDOC_MESSAGE,
    "git commit -m \"$(printf 'subject\\n\\nbody')\"",
    ("git commit -m wip", "game_main"),
    ("git commit -m wip", "fleet_worktree_main"),
    'git add -A && git commit -m "wip"',
    "git add src && git status && git commit -m wip",
    "git add -A; git commit -m wip; git push",
    "echo done && git commit -m wip",
    ("git -C @fleet_master@ commit -m wip", "scratch_master"),
    ("git -C @fleet_master@ add -A && git -C @fleet_master@ commit -m wip", "scratch_master"),
    ("cd @fleet_master@ && git commit -m wip", "scratch_master"),
    ("cd @fleet_master@ && git add -A && git commit -m wip", "fleet_feature"),
    "GIT_AUTHOR_NAME=x GIT_AUTHOR_EMAIL=x@example.invalid git commit -m wip",
    "env GIT_COMMITTER_DATE=now git commit -m wip",
    "git -c user.name=x commit -m wip",
    "git --no-pager commit -m wip",
    "command git commit -m wip",
    "bash -c 'git commit -m wip'",
    "bash -c \"git add -A && git commit -m 'wip'\"",
    "(git commit -m wip)",
)

ALLOW = cases(
    "fleet_feature",
    # Pushing a feature branch.
    "git push",
    "git push origin",
    "git push origin HEAD",
    "git push -u origin HEAD",
    "git push --set-upstream origin HEAD",
    "git push origin claude/feature",
    "git push -u origin claude/feature",
    "git push origin HEAD:claude/feature",
    "git push origin HEAD:refs/heads/claude/feature",
    "git push origin claude/feature:claude/feature",
    "git push --force-with-lease origin claude/feature",
    "git push --force-with-lease",
    "git push --force-with-lease=claude/feature origin claude/feature",
    "git push -f origin HEAD:claude/feature",
    "git push origin +claude/feature",
    "git push origin master:claude/feature",
    "git push origin main:refs/heads/claude/feature",
    "git push origin claude/master-fix",
    "git push origin HEAD:mastery",
    "git push origin feature/main-menu",
    "git push origin HEAD:refs/heads/claude/master",
    "git push origin refs/tags/master",
    "git push origin tag master",
    "git push --tags",
    "git push origin --tags",
    "git push --follow-tags origin claude/feature",
    "git push origin :claude/old-branch",
    "git push origin --delete claude/old-branch",
    "git push -d origin claude/old-branch",
    "git push --dry-run",
    "git push -n origin claude/feature",
    'git push origin "$BRANCH"',
    'git push origin HEAD:"$TARGET"',
    'git push origin "HEAD:$TARGET"',
    "git push origin $(git branch --show-current)",
    "git push origin `git rev-parse --abbrev-ref HEAD`",
    'git push "$REMOTE" claude/feature',
    "git push -u origin HEAD 2>&1 | tail -3",
    "git push origin claude/feature 2>&1 | grep -v master",
    "git push origin claude/feature > master",
    "git push origin claude/feature # not master",
    "time git push origin claude/feature",
    "timeout 60 git push -u origin HEAD",
    "env GIT_TRACE=1 git push -u origin HEAD",
    "xargs git push origin < branches.txt",
    # A fleet checkout's other remotes.
    "git push fork HEAD:master",
    "git push fork master",
    "git push --all fork",
    # A scratch repository's own protected branch.
    ("git push origin master", "scratch_master"),
    ("git push origin HEAD:master", "scratch_master"),
    ("git push origin main", "scratch_master"),
    ("git push -f origin master", "scratch_master"),
    ("git push", "scratch_master"),
    ("git push -u origin HEAD", "scratch_master"),
    ("git push --all", "scratch_master"),
    ("git push --mirror origin", "scratch_master"),
    ("git push origin --delete master", "scratch_master"),
    ("git -C @scratch_master@ push origin master", "fleet_master"),
    ("cd @scratch_master@ && git push origin master", "fleet_master"),
    ("(cd @scratch_master@ && git push origin HEAD:master)", "fleet_master"),
    ("bash -c 'cd @scratch_master@ && git push origin master'", "fleet_master"),
    ("git push /tmp/guard-fixture/bare.git master", "fleet_master"),
    ("git push ../bare.git HEAD:master", "fleet_master"),
    ("git push https://example.invalid/someone/project.git master", "fleet_master"),
    ("git push git@example.invalid:someone/IrredenEngine.git master", "fleet_master"),
    ("git push https://github.com/jakildev/IrredenEngine-mirror.git master", "fleet_master"),
    # The repository cannot be resolved and no refspec names a protected branch.
    ("git push", "not_a_repo"),
    ("git push --all", "not_a_repo"),
    ("git push", "no_remote_master"),
    ("git push -u origin HEAD", "no_remote_master"),
    ("git push --mirror", "no_remote_master"),
    ('git -C "$DIR" push', "fleet_master"),
    ('cd "$DIR" && git push', "fleet_master"),
    ("GIT_DIR=/nonexistent/.git git push", "fleet_master"),
    ("git --git-dir=/nonexistent/.git push", "fleet_master"),
    # On a protected branch, pushing something other than the current branch.
    ("git push origin claude/feature", "fleet_master"),
    ("git push origin HEAD:claude/feature", "fleet_master"),
    ("git push origin tag v1", "fleet_master"),
    ("git push origin refs/tags/v1", "fleet_master"),
    ("git push --tags", "fleet_master"),
    ("git push origin --delete claude/old-branch", "fleet_master"),
    # An earlier command on the line may have left the protected branch.
    ("git checkout -b claude/x && git push -u origin HEAD", "fleet_master"),
    ("git checkout -b claude/x && git commit -m wip", "fleet_master"),
    ("git switch -c claude/x && git add -A && git commit -m wip", "fleet_master"),
    ("git checkout claude/x; git commit -am wip", "fleet_master"),
    ("git stash && git checkout -b claude/x && git stash pop && git commit -m wip",
     "fleet_master"),
    ("gh pr checkout 12 && git commit -m wip", "fleet_master"),
    ("fleet-start-next-task && git commit -m wip", "fleet_master"),
    ("./tools/new-branch.sh x && git commit -m wip && git push", "fleet_master"),
    ("git remote set-url origin /tmp/guard-fixture/bare.git && git commit -m wip",
     "fleet_master"),
    # Committing on a feature branch, with trigger words inside values.
    'git commit -m "wip"',
    "git commit -am wip",
    "git commit --amend --no-edit",
    'git commit -m "fix merge of no-verify docs"',
    'git commit -m "never push to master; the human merges"',
    'git commit -m "git push origin master is denied"',
    "git commit -m '--no-verify'",
    "git commit -m --no-verify",
    "git commit -m -n",
    'git commit -m "-n"',
    "git commit -mn",
    'git commit -am "n"',
    "git commit -am n",
    "git commit --message --no-verify",
    "git commit --message=-n",
    'git commit --message "-n is no-verify"',
    "git commit -C HEAD~1",
    "git commit -c ORIG_HEAD",
    "git commit --fixup=abc1234",
    "git commit -S -m wip",
    "git commit -Skeyid -m wip",
    "git commit -uno -m wip",
    "git commit -unormal -m wip",
    "git commit -v -m wip",
    "git commit -m wip -- -n",
    'git commit --author "-n <n@example.invalid>" -m wip',
    "git commit -t template-n.txt",
    "git commit --no-status -m wip",
    "git commit --no-gpg-sign -m wip",
    "git commit --verify -m wip",
    "git commit -m " + HEREDOC_MESSAGE,
    "git commit -F - <<'EOF'\nsubject\n\ngit push origin master\nEOF",
    # Committing on a protected branch outside the fleet's repositories.
    ("git commit -m wip", "scratch_master"),
    ("git commit -am wip", "scratch_master"),
    ("git add -A && git commit -m wip", "scratch_master"),
    ("git commit -m wip", "no_remote_master"),
    ("git commit -m wip", "not_a_repo"),
    ("git -C @scratch_master@ commit -m wip", "fleet_master"),
    ("cd @fleet_feature@ && git commit -m wip", "fleet_master"),
    ("git -C /nonexistent/guard-fixture commit -m wip", "fleet_master"),
    ('git -C "$DIR" commit -m wip', "fleet_master"),
    ("git --git-dir=/nonexistent/.git commit -m wip", "fleet_master"),
    ("GIT_DIR=/nonexistent/.git git commit -m wip", "fleet_master"),
    ("GIT_WORK_TREE=/nonexistent git commit -m wip", "fleet_master"),
    ("git commit --dry-run", "fleet_master"),
    ("git commit --dry-run -a", "fleet_master"),
    ("git commit-tree HEAD^{tree} -m wip", "fleet_master"),
    ("git commit-graph write", "fleet_master"),
    # Other git commands that name a protected branch.
    "git status",
    "git log --grep master",
    "git log master..HEAD",
    "git log --oneline origin/master -5",
    "git diff master",
    "git diff origin/master...HEAD --stat",
    "git fetch origin master",
    "git fetch origin master:refs/remotes/origin/master",
    "git pull origin master",
    "git pull --rebase origin master",
    "git merge origin/master",
    "git merge master",
    "git merge --no-edit origin/master",
    'git merge --no-ff -m "merge master" origin/master',
    "git merge -n origin/master",
    "git merge --no-verify-signatures origin/master",
    "git merge --abort",
    "git rebase origin/master",
    "git rebase --onto master a b",
    "git checkout master",
    "git switch main",
    "git branch --merged master",
    "git show master:notes.txt",
    "git rev-parse origin/master",
    "git merge-base HEAD origin/master",
    "git cherry-pick -n abc1234",
    "git revert -n abc1234",
    'git stash push -m "no-verify"',
    "git config core.hooksPath hooks",
    "git config --get core.hooksPath",
    "git -c core.hooksPath=/dev/null status",
    "git -c core.hooksPath=/dev/null rebase --continue",
    "git -c color.ui=always commit -m wip",
    "git -c user.name=x push origin claude/feature",
    "git remote -v",
    "git remote get-url --push origin",
    "git ls-remote origin master",
    "git worktree add ../wt master",
    'git tag -a v1 -m "merge window" master',
    # gh commands that read, create or comment.
    'gh pr create --title "x" --body-file body.md',
    "gh pr create --base master --head claude/feature --title x --body y",
    "gh pr view 123",
    "gh pr view 123 --json mergeable,mergeStateStatus,mergeCommit",
    "gh pr view 123 --json mergeable -q .mergeable",
    "gh pr view merge",
    'gh pr list --search "merge"',
    "gh pr list --search merge",
    "gh pr list -S merge",
    "gh pr list --state merged --limit 5",
    "gh pr list --label merge",
    "gh pr checks 123",
    'gh pr comment 123 --body "ready to merge"',
    "gh pr comment 123 --body merge",
    "gh pr edit 123 --add-label fleet:approved",
    "gh pr edit 123 --title merge",
    "gh pr status",
    "gh pr diff 123",
    "gh pr ready 123",
    "gh pr close 123",
    "gh pr checkout 123",
    'gh pr review 123 --comment --body "do not merge yet"',
    "gh -R jakildev/IrredenEngine pr view 123",
    'gh issue create --title "merge queue" --body x',
    'gh issue list --search "pr merge"',
    "gh issue view 123 --comments",
    "gh search prs merge --repo jakildev/IrredenEngine",
    "gh run list --workflow merge.yml",
    "gh repo view --json mergeCommitAllowed",
    "gh label create merge",
    # gh api reads, and writes to other endpoints.
    "gh api repos/o/r/pulls/123/merge",
    "gh api -X GET repos/o/r/pulls/123/merge",
    "gh api --method GET repos/o/r/pulls/123/merge",
    "gh api --method=get repos/o/r/pulls/123/merge",
    "gh api -X GET repos/o/r/pulls/123/merge -f per_page=1",
    "gh api -X HEAD repos/o/r/pulls/123/merge",
    "gh api -i repos/o/r/pulls/123/merge",
    "gh api repos/o/r/pulls/123/merge --silent",
    'gh api repos/o/r/pulls/123/merge -H "Accept: application/vnd.github+json"',
    "gh api repos/o/r/pulls/123/merge --jq .message",
    "gh api repos/o/r/pulls/123/merge -q .merged -t '{{.}}'",
    'gh api -X "$METHOD" repos/o/r/pulls/123/merge',
    "gh api repos/o/r/pulls/123",
    "gh api repos/o/r/pulls/123 --jq .merged",
    "gh api repos/o/r/pulls/123/commits",
    "gh api repos/o/r/merges",
    "gh api -X PUT repos/o/r/pulls/123/update-branch",
    "gh api repos/o/r/pulls -f title=x -f head=claude/feature -f base=master",
    "gh api -X PATCH repos/o/r/pulls/123 -f title=merge",
    'gh api -X POST repos/o/r/issues/123/comments -f body="please merge"',
    "gh api -X POST repos/o/r/issues/123/labels -f 'labels[]=merge'",
    "gh api -X POST repos/o/r/merge-upstream -f branch=claude/feature",
    "gh api repos/o/r/git/refs/heads/master",
    "gh api repos/o/r/git/refs/heads/master --jq .object.sha",
    "gh api repos/o/r/branches/master/protection",
    "gh api repos/o/r/branches/master",
    "gh api repos/o/r/commits/master/status",
    "gh api repos/o/r/compare/master...claude/feature",
    "gh api -X PATCH repos/o/r/git/refs/heads/claude/feature -f sha=abc -F force=true",
    "gh api -X DELETE repos/o/r/git/refs/heads/claude/old",
    "gh api -X DELETE repos/o/r/git/refs/heads/master-backup",
    "gh api -X POST repos/o/r/git/refs -f ref=refs/heads/claude/new -f sha=abc",
    "gh api -X PUT repos/o/r/branches/claude/feature/protection --input p.json",
    "gh api graphql -f query='query { repository(owner: \"o\", name: \"r\") { pullRequest("
    "number: 1) { mergeable merged mergeCommit { oid } } } }'",
    "gh api graphql -f query='mutation { addComment(input: {subjectId: \"x\", body:"
    " \"merge?\"}) { clientMutationId } }'",
    "gh api graphql -f query='mutation { disablePullRequestAutoMerge(input:"
    " {pullRequestId: \"x\"}) { clientMutationId } }'",
    "gh api graphql -f query='query { repository(owner: \"o\", name: \"r\") {"
    " branchProtectionRules(first: 5) { nodes { pattern } } } }'",
    "gh api graphql -F query=@merge.graphql",
    # Text that mentions a denied command without running it.
    'echo "git push origin master"',
    "echo 'gh pr merge 123'",
    "echo git push origin master",
    "printf '%s\\n' \"git commit --no-verify\"",
    'grep -rn "git push origin master" docs',
    'rg "gh pr merge" docs scripts',
    'git grep -n "push origin master"',
    'git log --grep "gh pr merge"',
    'git log -S "--no-verify"',
    "python3 -c \"print('git push origin master')\"",
    "python3 -c 'import sys; print(\"gh pr merge\")'",
    "python3 -m pytest -k merge",
    'echo "$(echo git push origin master)"',
    "git commit -m \"$(echo 'gh pr merge 123')\"",
    'git commit -m "note: \\`gh pr merge 123\\` is denied"',
    "git commit -m 'note: `gh pr merge 123` is denied'",
    'x="git push origin master"',
    "CMD='gh pr merge 123'; echo \"$CMD\"",
    "alias gpm='git push origin master'",
    "args=(git push origin master)",
    'args=(git push origin master); echo "${args[@]}"',
    "export GIT_PUSH_DEFAULT=master",
    "npm run commit-lint",
    "make push-docs",
    "man git-push",
    # The executor's file as an argument, not as the command.
    "cat scripts/fleet/fleet-automerge",
    "grep -n merge scripts/fleet/fleet-automerge",
    "ls -l scripts/fleet/fleet-automerge",
    "wc -l scripts/fleet/fleet-automerge",
    "git log --oneline -- scripts/fleet/fleet-automerge",
    "ruff check scripts/fleet/fleet-automerge",
    "python3 scripts/fleet/tests/test_fleet_automerge.py",
    "python3 -m unittest scripts/fleet/tests/test_fleet_automerge.py",
    "python3 -c 'print(1)' scripts/fleet/fleet-automerge",
    "bash scripts/fleet/tests/run_all.sh --only fleet_automerge",
    "fleet-positive-control scripts/fleet/tests/test_fleet_automerge.py origin/master",
    "which fleet-automerge",
    "command -v fleet-automerge",
    "command -V gh",
    "type fleet-automerge",
    "fleet-help fleet-automerge",
    # Comments.
    "git status # then git push origin master",
    "# git push origin master",
    "git status # gh pr merge 123\ngit diff",
    # Here-document bodies and here-strings are data.
    "cat <<'EOF' > notes.txt\ngit push origin master\ngh pr merge 123\nEOF",
    "cat <<EOF\ngit commit --no-verify -m wip\nEOF",
    "cat <<-EOF\n\tgit push origin master\n\tEOF",
    "cat > plan.md <<'PLAN'\n- gh pr merge 123\n- fleet-automerge\nPLAN\ngit status",
    "python3 - <<'PY'\nprint(\"git push origin master\")\nPY",
    "cat <<EOF\ngit push origin master",
    "cat <<< 'git push origin master'",
    # A line the splitter cannot read is allowed whole.
    'git push origin master "unbalanced',
    "git push origin 'master",
    'echo "x && git push origin master',
    "git push origin master && echo 'done",
    "echo $(git push origin master",
    "echo `git push origin master",
    "echo ${X:-git push origin master",
    "echo $((1 + && git push origin master",
    "git push origin $'master",
    # Shell syntax around protected branch names.
    "echo $((1 << 2)); git push origin claude/feature",
    'case "$1" in master) echo protected ;; esac',
    'for b in master main; do echo "$b"; done',
    "git branch --show-current | grep -qx master && echo on-master",
    'test "$(git symbolic-ref --short HEAD)" = master && echo yes',
    '[ "$branch" = master ] && git status',
)

REASONS = {
    "push-protected": {guard.PUSH_REASON},
    "merge": {guard.MERGE_REASON, guard.API_WRITE_REASON},
    "skip-hooks": {guard.SKIP_HOOKS_REASON},
    "commit-protected": {guard.COMMIT_REASON},
}


class DenyTables(unittest.TestCase):
    def check(self, rule):
        self.assertGreater(len(DENY[rule]), 20)
        for command, where in DENY[rule]:
            with self.subTest(command=command, where=where):
                self.assertEqual(rule_for(command, where), rule)

    def test_push_to_a_protected_branch(self):
        self.check("push-protected")

    def test_merging(self):
        self.check("merge")

    def test_skipping_hooks(self):
        self.check("skip-hooks")

    def test_committing_on_a_protected_branch(self):
        self.check("commit-protected")

    def test_every_rule_has_a_table(self):
        self.assertEqual(set(DENY), {name for name, _ in guard.RULES})


class AllowTable(unittest.TestCase):
    def test_allowed(self):
        self.assertGreater(len(ALLOW), 200)
        for command, where in ALLOW:
            with self.subTest(command=command, where=where):
                self.assertIsNone(rule_for(command, where))


class RuleMutation(unittest.TestCase):
    """Each rule alone carries its table.

    With one rule dropped, the other three tables stay attributed case for
    case, and the dropped rule's leading case is allowed outright: no other
    rule was shadowing it. That its whole table then fails `DenyTables`
    follows from the attribution those tests assert.
    """

    def test_each_rule_carries_its_own_table(self):
        for dropped in DENY:
            kept = tuple(rule for rule in guard.RULES if rule[0] != dropped)
            self.assertEqual(len(kept), len(guard.RULES) - 1)
            with mock.patch.object(guard, "RULES", kept):
                for rule, table in DENY.items():
                    verdicts = [rule_for(command, where) for command, where in table]
                    with self.subTest(dropped=dropped, rule=rule):
                        if rule == dropped:
                            self.assertNotIn(rule, verdicts)
                            self.assertIsNone(verdicts[0])
                        else:
                            self.assertEqual(verdicts, [rule] * len(table))


class LookupSeam(unittest.TestCase):
    def setUp(self):
        guard._lookups.clear()
        self.addCleanup(guard._lookups.clear)

    def test_fixtures_answer_the_lookups(self):
        branch = ("symbolic-ref", "--quiet", "--short", "HEAD")
        self.assertEqual(guard._lookup(REPOS["fleet_feature"], *branch), "claude/feature")
        self.assertEqual(guard._lookup(REPOS["fleet_worktree_main"], *branch), "main")
        self.assertEqual(
            guard._lookup(REPOS["fleet_master"], "remote", "get-url", "origin"), ENGINE_HTTPS)
        self.assertIsNone(guard._lookup(REPOS["no_remote_master"], "remote", "get-url", "origin"))
        self.assertIsNone(guard._lookup(REPOS["not_a_repo"], *branch))
        self.assertIsNone(guard._lookup(None, *branch))

    def test_a_failed_lookup_denies_only_an_explicit_protected_refspec(self):
        with mock.patch.object(guard, "_run_git", return_value=None) as run:
            self.assertEqual(rule_for("git push origin master", "fleet_master"), "push-protected")
            self.assertEqual(rule_for("git push origin HEAD:main", "fleet_master"),
                             "push-protected")
            for command in ("git push", "git push -u origin HEAD", "git push --all",
                            "git push --mirror", "git commit -m wip", "git commit -am wip"):
                with self.subTest(command=command):
                    self.assertIsNone(rule_for(command, "fleet_master"))
            self.assertGreater(run.call_count, 0)

    def test_a_hung_lookup_times_out(self):
        marker = _STATE["root"] / "hung-git-started"
        stub = _STATE["root"] / "hung_git.py"
        stub.write_text(
            "import sys, time\n"
            "open(%r, 'a').write('x')\n"
            "time.sleep(120)\n" % str(marker), encoding="utf-8")
        with mock.patch.object(guard, "GIT_ARGV", [sys.executable, str(stub)]), \
                mock.patch.object(guard, "LOOKUP_TIMEOUT_S", 1.0):
            started = time.monotonic()
            self.assertIsNone(rule_for("git commit -m wip", "fleet_master"))
            self.assertIsNone(rule_for("git push", "fleet_master"))
            self.assertEqual(rule_for("git push origin HEAD:master", "fleet_master"),
                             "push-protected")
            elapsed = time.monotonic() - started
        self.assertTrue(marker.exists(), "the hung stub never started: the seam was bypassed")
        self.assertLess(elapsed, 60)

    def test_an_absent_git_is_a_failed_lookup(self):
        with mock.patch.object(guard, "GIT_ARGV", [str(_STATE["root"] / "no-such-git")]):
            self.assertIsNone(rule_for("git commit -m wip", "fleet_master"))
            self.assertEqual(rule_for("git push origin master", "fleet_master"), "push-protected")

    def test_commands_that_need_no_lookup_make_none(self):
        with mock.patch.object(guard, "_run_git", side_effect=AssertionError("lookup made")):
            for command, expected in (
                    ("git status", None),
                    ("git push origin claude/feature", None),
                    ("git push --tags", None),
                    ("gh pr view 123", None),
                    ("gh pr merge 123", "merge"),
                    ("git commit --no-verify -m wip", "skip-hooks"),
                    ("git checkout -b claude/x && git commit -m wip", None),
            ):
                with self.subTest(command=command):
                    self.assertEqual(rule_for(command, "fleet_master"), expected)

    def test_a_feature_branch_push_costs_one_lookup(self):
        line = "git add -A && git commit -m wip && git push -u origin HEAD"
        with mock.patch.object(guard, "_run_git", side_effect=guard._run_git) as run:
            self.assertIsNone(rule_for(line))
        self.assertEqual(run.call_count, 1)


class Splitting(unittest.TestCase):
    def split(self, line):
        return [(command.name, command.argv[1:], command.cwd)
                for command in guard.simple_commands(line, "/base")]

    def test_words_lose_their_quoting(self):
        self.assertEqual(
            self.split("git commit -m \"a b\" 'c d' e\\ f \"g\\\"h\" $'i\\tj'"),
            [("git", ["commit", "-m", "a b", "c d", "e f", 'g"h', "i\tj"], "/base")])

    def test_text_only_the_shell_knows_is_marked(self):
        for line in ('git push origin "$BRANCH"', "git push origin ${BRANCH}",
                     "git push origin $(cat branch)", "git push origin `cat branch`",
                     "git push origin <(cat branch)", "git push origin $((1 + 1))"):
            with self.subTest(line=line):
                self.assertEqual(self.split(line)[-1][1][2], "\0")

    def test_redirections_comments_and_heredoc_bodies_are_not_words(self):
        self.assertEqual(self.split("git status >out 2>&1 </dev/null &>>log # trailing"),
                         [("git", ["status"], "/base")])
        self.assertEqual(
            self.split("cat <<EOF | tee out\ngit push origin master\nEOF\ngit status"),
            [("cat", [], "/base"), ("tee", ["out"], "/base"), ("git", ["status"], "/base")])

    def test_wrappers_and_assignments_are_stripped(self):
        for line in ("A=1 B=2 git status", "env -i A=1 git status", "command git status",
                     "nohup git status", "time -p git status", "exec git status",
                     "timeout -k 5 30 git status", "xargs -n 1 -I {} git status",
                     "if git status", "then git status", "! git status", "{ git status"):
            with self.subTest(line=line):
                self.assertEqual(self.split(line), [("git", ["status"], "/base")])
        self.assertEqual(self.split("command -v git"), [])

    def test_nested_shells_are_split_in_place(self):
        self.assertEqual(
            self.split("true; bash -lc 'cd a && git status' ; eval git diff"),
            [("true", [], "/base"), ("cd", ["a"], "/base"), ("git", ["status"], "/base/a"),
             ("git", ["diff"], "/base")])

    def test_directory_tracking(self):
        home = os.environ["HOME"]
        for line, expected in (
                ("git status", "/base"),
                ("cd a && git status", "/base/a"),
                ("cd a; cd ../b && git status", "/base/b"),
                ("cd /abs/x/.. && git status", "/abs"),
                ("cd ~/x && git status", home + "/x"),
                ("cd && git status", home),
                ("pushd a && git status", "/base/a"),
                ("(cd a && true); git status", "/base"),
                ("(cd a && git status)", "/base/a"),
                ("echo $(cd a && true) && git status", "/base"),
                ("cd a | git status", "/base"),
                ("cd a & git status", "/base"),
                ("env -C a git status", "/base/a"),
                ("git -C a status", "/base/a"),
                ("git -C a -C b status", "/base/a/b"),
                ("git -C /abs status", "/abs"),
                ("cd a && git -C ../b status", "/base/b"),
                ('cd "$X" && git status', None),
                ('cd "$X" && git -C /abs status', "/abs"),
                ("cd - && git status", None),
                ("popd && git status", None),
                ('git -C "$X" status', None),
                ("git --git-dir=/x/.git status", None),
                ("git --work-tree /x status", None),
                ("GIT_DIR=/x/.git git status", None),
                ("env GIT_WORK_TREE=/x git status", None),
        ):
            with self.subTest(line=line):
                last = list(guard.simple_commands(line, "/base"))[-1]
                self.assertEqual(last.git.sub, "status")
                self.assertEqual(last.git.cwd, expected)

    def test_unbalanced_lines_raise(self):
        for line in ('echo "open', "echo 'open", "echo $(open", "echo `open", "echo ${open",
                     "echo $((open", "echo $'open", "(echo open", "cat << $X"):
            with self.subTest(line=line):
                with self.assertRaises(guard.Unparseable):
                    list(guard.simple_commands(line, "/base"))


class FleetRepoUrls(unittest.TestCase):
    def test_fleet_urls(self):
        for url in (ENGINE_SSH, ENGINE_HTTPS, GAME_HTTPS,
                    "https://github.com/jakildev/IrredenEngine",
                    "https://github.com/jakildev/IrredenEngine/",
                    "https://github.com/JakilDev/irredenengine.GIT",
                    "git@github.com:jakildev/irreden.git",
                    "git@github.com:jakildev/irreden",
                    "ssh://git@github.com/jakildev/irreden.git",
                    "ssh://git@github.com:22/jakildev/IrredenEngine.git",
                    "git@github-fleet:jakildev/IrredenEngine.git",
                    "https://x-access-token:abc@github.com/jakildev/irreden.git"):
            with self.subTest(url=url):
                self.assertTrue(guard._names_fleet_repo(url))

    def test_other_urls(self):
        for url in ("https://github.com/someone-else/IrredenEngine.git",
                    "https://github.com/jakildev/IrredenEngine-mirror.git",
                    "https://github.com/jakildev/irreden-tools",
                    "https://github.com/jakildev/other.git",
                    "https://example.invalid/jakildev/IrredenEngine/extra.git",
                    "/tmp/guard-fixture/bare.git", "../IrredenEngine", "IrredenEngine", ""):
            with self.subTest(url=url):
                self.assertFalse(guard._names_fleet_repo(url))

    def test_repo_set_matches_fleet_runtime(self):
        self.assertEqual(guard.FLEET_REPOS, {slug.lower() for slug in REPO_SLUGS.values()})


class ReasonText(unittest.TestCase):
    def test_reasons_say_what_to_do_instead(self):
        for reason in sorted(set().union(*REASONS.values())):
            with self.subTest(reason=reason):
                self.assertTrue(reason.startswith("fleet-guard-bash: "))
                self.assertIn("they can run it themselves with the ! prefix", reason)
                self.assertLessEqual(len(re.findall(r"\.(?:\s|$)", reason)), 2)
                self.assertLessEqual(set(re.findall(r"\b[A-Z]{2,}\b", reason)), {"PR", "API"})
        for reason in (guard.PUSH_REASON, guard.COMMIT_REASON, guard.API_WRITE_REASON):
            self.assertIn("commit-and-push skill", reason)
        self.assertIn("the human merges", guard.MERGE_REASON)


class HookDocument(unittest.TestCase):
    def payload(self, command, where="fleet_feature", **extra):
        return json.dumps(dict({"tool_name": "Bash", "cwd": REPOS[where],
                                "tool_input": {"command": command}}, **extra))

    def test_deny_document_shape(self):
        document = json.loads(guard.run(self.payload("git push origin HEAD:master")))
        self.assertEqual(document, {"hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": guard.PUSH_REASON,
        }})

    def test_allow_is_empty(self):
        self.assertEqual(guard.run(self.payload("git push origin claude/feature")), "")

    def test_input_without_a_usable_command_is_allowed(self):
        for raw in ("", "   ", "{not json push", "[]", '"git push origin master"', "null", "7",
                    "{}", '{"tool_input": null}', '{"tool_input": "git push origin master"}',
                    '{"tool_input": {}}', '{"tool_input": {"command": 7}}',
                    '{"tool_input": {"command": ["git", "push", "origin", "master"]}}',
                    '{"tool_input": {"command": ""}}',
                    '{"tool_input": {"command": "git push origin master\\u0000"}}'):
            with self.subTest(raw=raw):
                self.assertEqual(guard.run(raw), "")

    def test_unparsable_input_is_logged(self):
        before = log_text()
        guard.run("{not json push")
        self.assertIn("hook input is not JSON", log_text()[len(before):])

    def test_a_line_the_splitter_cannot_read_is_logged_without_its_text(self):
        before = log_text()
        self.assertEqual(guard.run(self.payload("git push origin master 'SECRET-TOKEN")), "")
        added = log_text()[len(before):]
        self.assertIn("command not split", added)
        self.assertNotIn("SECRET-TOKEN", added)
        self.assertEqual(added.count("\n"), 1)

    def test_a_missing_cwd_leaves_the_repository_unresolved(self):
        for cwd in ({}, {"cwd": ""}, {"cwd": None}, {"cwd": 7}):
            with self.subTest(cwd=cwd):
                raw = json.dumps(dict({"tool_input": {"command": "git push origin master"}}, **cwd))
                self.assertIn('"deny"', guard.run(raw))
                raw = json.dumps(dict({"tool_input": {"command": "git commit -m wip"}}, **cwd))
                self.assertEqual(guard.run(raw), "")


class PolicyProcess(unittest.TestCase):
    """fleet_guard_bash.py as the shim runs it."""

    def policy(self, raw):
        return subprocess.run([sys.executable, "-I", str(POLICY)], input=raw.encode("utf-8"),
                              capture_output=True, timeout=60)

    def injected(self, patch):
        """Run main() with `patch` applied to the module first."""
        code = ("import sys, time\nsys.path.insert(0, %r)\nimport fleet_guard_bash as g\n%s\n"
                "sys.exit(g.main())\n" % (str(ROOT), patch))
        raw = json.dumps({"cwd": REPOS["fleet_feature"],
                          "tool_input": {"command": "git push origin HEAD:master"}})
        return subprocess.run([sys.executable, "-c", code], input=raw.encode("utf-8"),
                              capture_output=True, timeout=60)

    def test_deny_and_allow(self):
        raw = json.dumps({"cwd": REPOS["fleet_feature"],
                          "tool_input": {"command": "git push origin HEAD:master"}})
        done = self.policy(raw)
        self.assertEqual(done.returncode, 0)
        self.assertEqual(done.stdout.count(b"\n"), 1)
        self.assertNotIn(b"\r", done.stdout)
        reason = json.loads(done.stdout)["hookSpecificOutput"]["permissionDecisionReason"]
        self.assertEqual(reason, guard.PUSH_REASON)
        done = self.policy(raw.replace("HEAD:master", "HEAD:claude/feature"))
        self.assertEqual((done.returncode, done.stdout), (0, b""))

    def test_the_injection_harness_reaches_the_policy(self):
        done = self.injected("pass")
        self.assertEqual(done.returncode, 0)
        self.assertIn(b'"deny"', done.stdout)

    def test_an_exception_fails_open_and_is_logged(self):
        before = log_text()
        done = self.injected("def boom(*a):\n    raise RuntimeError('injected')\ng.evaluate = boom")
        self.assertEqual((done.returncode, done.stdout), (0, b""))
        self.assertIn("internal failure, allowed: RuntimeError('injected')",
                      log_text()[len(before):])

    def test_the_deadline_fails_open_and_is_logged(self):
        before = log_text()
        started = time.monotonic()
        done = self.injected("g.DEADLINE_S = 0.5\ng.evaluate = lambda *a: time.sleep(120)")
        self.assertEqual((done.returncode, done.stdout), (0, b""))
        self.assertLess(time.monotonic() - started, 60)
        self.assertIn("deadline exceeded, allowed", log_text()[len(before):])


class Shim(unittest.TestCase):
    def shim(self, bash, raw, env=None):
        return subprocess.run([bash, str(SHIM)], input=raw.encode("utf-8"),
                              capture_output=True, env=env, timeout=60)

    def payload(self, command, where="fleet_feature"):
        # `where` may also be a literal directory: a temp path can spell a
        # trigger word, which the trigger-free cases cannot afford.
        return json.dumps({"session_id": "fixture", "hook_event_name": "PreToolUse",
                           "tool_name": "Bash", "cwd": REPOS.get(where, where),
                           "tool_input": {"command": command, "description": "fixture"}})

    def python_stub(self):
        """A PATH whose `python3` records that it ran and exits 97."""
        bin_dir = Path(tempfile.mkdtemp(prefix="stub-bin.", dir=str(_STATE["root"])))
        marker = bin_dir / "python3-ran"
        stub = bin_dir / "python3"
        stub.write_text("#!/bin/sh\necho ran >> '%s'\nexit 97\n" % marker, encoding="utf-8")
        stub.chmod(stub.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        return dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ["PATH"]), marker

    def test_one_case_from_each_rule_is_denied_with_its_reason(self):
        for bash in BASHES:
            for rule, table in DENY.items():
                command, where = table[0]
                with self.subTest(bash=bash, rule=rule):
                    done = self.shim(bash, self.payload(expand(command), where))
                    self.assertEqual((done.returncode, done.stderr), (0, b""))
                    hook = json.loads(done.stdout)["hookSpecificOutput"]
                    self.assertEqual(hook["hookEventName"], "PreToolUse")
                    self.assertEqual(hook["permissionDecision"], "deny")
                    self.assertIn(hook["permissionDecisionReason"], REASONS[rule])

    def test_an_allowed_command_prints_nothing(self):
        for bash in BASHES:
            for command in ("git push -u origin HEAD", "gh pr view 123", "ls -la"):
                with self.subTest(bash=bash, command=command):
                    done = self.shim(bash, self.payload(command))
                    self.assertEqual((done.returncode, done.stdout, done.stderr), (0, b"", b""))

    def test_empty_and_unparsable_input_print_nothing(self):
        for bash in BASHES:
            for raw in ("", "\n", "{not json push", '{"tool_input": {"command": 7}} merge'):
                with self.subTest(bash=bash, raw=raw):
                    done = self.shim(bash, raw)
                    self.assertEqual((done.returncode, done.stdout, done.stderr), (0, b"", b""))

    def test_a_trigger_free_command_never_starts_python(self):
        for bash in BASHES:
            env, marker = self.python_stub()
            for command in ("ls -la", "fleet-build --target IRShapeDebug", "grep -rn foo docs",
                            "git status", "gh pr view 123", "git log --oneline -5"):
                with self.subTest(bash=bash, command=command):
                    done = self.shim(bash, self.payload(command, "/fixture/cwd"), env)
                    self.assertEqual((done.returncode, done.stdout, done.stderr), (0, b"", b""))
                    self.assertFalse(marker.exists(), "python3 was started")
            # The control: the same stub does run once a trigger word is present.
            with self.subTest(bash=bash, command="control"):
                self.shim(bash, self.payload("git push origin HEAD:master"), env)
                self.assertTrue(marker.exists(), "the python3 stub is not on the shim's PATH")

    def test_a_python_failure_fails_open_and_is_logged(self):
        for bash in BASHES:
            with self.subTest(bash=bash):
                env, marker = self.python_stub()
                before = log_text()
                done = self.shim(bash, self.payload("git push origin HEAD:master"), env)
                self.assertTrue(marker.exists())
                self.assertEqual((done.returncode, done.stdout, done.stderr), (0, b"", b""))
                self.assertIn("fleet_guard_bash.py exited 97, allowed", log_text()[len(before):])

    def test_a_missing_python_fails_open_and_is_logged(self):
        empty = tempfile.mkdtemp(prefix="empty-bin.", dir=str(_STATE["root"]))
        for bash in BASHES:
            with self.subTest(bash=bash):
                before = log_text()
                done = self.shim(bash, self.payload("git push origin HEAD:master"),
                                 dict(os.environ, PATH=empty))
                self.assertEqual((done.returncode, done.stdout, done.stderr), (0, b"", b""))
                self.assertIn("python3 not found, allowed", log_text()[len(before):])

    def test_help_prints_the_header_without_reading_stdin(self):
        for bash in BASHES:
            with self.subTest(bash=bash):
                proc = subprocess.Popen([bash, str(SHIM), "--help"], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.addCleanup(proc.kill)
                self.addCleanup(proc.stdin.close)
                self.assertEqual(proc.wait(timeout=30), 0)
                text = proc.stdout.read().decode("utf-8")
                proc.stdout.close()
                proc.stderr.close()
                self.assertTrue(text.startswith("fleet-guard-bash — PreToolUse hook for Bash."))
                self.assertIn("Usage: fleet-guard-bash < hook-input.json", text)
                self.assertNotIn("set -uo pipefail", text)

    def test_every_denied_command_carries_a_trigger_word(self):
        found = re.search(r"^TRIGGER_RE='([^']+)'$", SHIM.read_text(encoding="utf-8"), re.M)
        self.assertIsNotNone(found, "TRIGGER_RE not found in the shim")
        self.assertGreaterEqual(len(found.group(1).split("|")), 3)
        trigger = re.compile(found.group(1), re.I)
        for rule, table in DENY.items():
            for command, _ in table:
                with self.subTest(rule=rule, command=command):
                    self.assertRegex(json.dumps(expand(command)), trigger)


class Registration(unittest.TestCase):
    def test_settings_register_the_hook_for_bash(self):
        if not SETTINGS.is_file():
            self.skipTest("no .claude/settings.json beside this tree")
        groups = json.loads(SETTINGS.read_text(encoding="utf-8"))["hooks"]["PreToolUse"]
        commands = {group["matcher"]: [hook["command"] for hook in group["hooks"]]
                    for group in groups}
        self.assertEqual(commands.get("Bash"),
                         ['"$CLAUDE_PROJECT_DIR"/scripts/fleet/fleet-guard-bash'])
        self.assertEqual(commands.get("Edit|Write|MultiEdit"),
                         ['"$CLAUDE_PROJECT_DIR"/scripts/fleet/fleet-guard-worktree-edit'])
        for group in groups:
            for hook in group["hooks"]:
                self.assertEqual(hook["type"], "command")

    def test_the_registered_command_runs_the_hook(self):
        if not SETTINGS.is_file():
            self.skipTest("no .claude/settings.json beside this tree")
        groups = json.loads(SETTINGS.read_text(encoding="utf-8"))["hooks"]["PreToolUse"]
        bash_group = [group for group in groups if group["matcher"] == "Bash"][0]
        command = bash_group["hooks"][0]["command"]
        raw = json.dumps({"cwd": REPOS["fleet_feature"],
                          "tool_input": {"command": "git push origin HEAD:master"}})
        for bash in BASHES:
            with self.subTest(bash=bash):
                done = subprocess.run(
                    [bash, "-c", command], input=raw.encode("utf-8"), capture_output=True,
                    env=dict(os.environ, CLAUDE_PROJECT_DIR=str(ROOT.parents[1])), timeout=60)
                self.assertEqual((done.returncode, done.stderr), (0, b""))
                reason = json.loads(done.stdout)["hookSpecificOutput"]["permissionDecisionReason"]
                self.assertEqual(reason, guard.PUSH_REASON)

    def test_the_shim_is_executable(self):
        self.assertTrue(os.access(str(SHIM), os.X_OK))


if __name__ == "__main__":
    unittest.main()
