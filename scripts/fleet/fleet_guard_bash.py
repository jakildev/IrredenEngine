"""Policy for the PreToolUse Bash guard; the `fleet-guard-bash` shim is its caller.

Reads the hook JSON on stdin and prints a `hookSpecificOutput` deny when a
simple command in `tool_input.command` matches a rule in `RULES`. Anything
else prints nothing.

The guard is a seatbelt against an agent's mistake, not a security boundary
(server-side branch protection is the boundary). Two invariants follow, and
every function here keeps them:

  * A deny needs a positive match. Text only the shell knows (an expansion in
    a refspec, a directory behind a variable), a branch an earlier command on
    the same line may have changed, and a command line the splitter cannot
    read are all allowed.
  * The hook never blocks a tool call by failing. Every internal failure exits
    0 with no output and appends one line to the guard log.
"""
import json
import os
import re
import sys
import threading
import time

PROTECTED_BRANCHES = frozenset({"master", "main"})

# Lowercased `owner/name`. Keep in sync with fleet_runtime.REPO_SLUGS; the
# suite compares the two.
FLEET_REPOS = frozenset({"jakildev/irredenengine", "jakildev/irreden"})

LOOKUP_TIMEOUT_S = 3.0
# For the whole process. Several times one lookup's timeout, so a single slow
# lookup ends through LOOKUP_TIMEOUT_S and still yields a verdict; past this
# the process exits 0 with no output, whatever it was doing.
DEADLINE_S = 10.0

# argv prefix of the read-only git lookups. The suite swaps in a stub.
GIT_ARGV = ["git"]

_ASK_HUMAN = (
    "if the human asked for this exact command, they can run it themselves with the ! prefix."
)
PUSH_REASON = (
    "fleet-guard-bash: this pushes to a protected branch of a fleet repository. "
    "Push a feature branch and open a PR through the commit-and-push skill instead; "
    + _ASK_HUMAN
)
MERGE_REASON = (
    "fleet-guard-bash: agents never merge here; the human merges. "
    "Leave the PR open for review; " + _ASK_HUMAN
)
API_WRITE_REASON = (
    "fleet-guard-bash: this writes to a branch or to its protection through the GitHub API, "
    "which skips review. Open a PR through the commit-and-push skill and let the human "
    "merge it; " + _ASK_HUMAN
)
SKIP_HOOKS_REASON = (
    "fleet-guard-bash: this skips the repository's git hooks. "
    "Fix what the hook reports and rerun without the bypass; " + _ASK_HUMAN
)
COMMIT_REASON = (
    "fleet-guard-bash: this commits on a protected branch of a fleet repository. "
    "Commit on a feature branch and open a PR through the commit-and-push skill instead; "
    + _ASK_HUMAN
)


# ----------------------------------------------------------------------
# Splitting a command line into simple commands
# ----------------------------------------------------------------------
class Unparseable(Exception):
    """The command line cannot be split with confidence, so the caller allows it."""


# Stands in for text only the shell knows: an expansion or a substitution. A
# word that contains it never equals a literal the rules look for.
_DYN = "\0"

_NAME_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_ASSIGNMENT_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\+?=")
_PLAIN_RE = re.compile(r"""[^ \t\r\n;|&()<>\\'"`$]+""")
_DQ_STOP_RE = re.compile(r'["\\`$]')
_ANSI_C_ESCAPES = {"n": "\n", "t": "\t", "\\": "\\", "'": "'", '"': '"'}

_MAX_NESTING = 8


class _Splitter:
    """Splits one shell command line into simple commands.

    Models what decides where a command starts and what its words are:
    quoting, separators, redirections, here-documents, comments, and the
    constructs that nest a command list (`( )`, `$( )`, backticks, `<( )`).
    A here-document body and an array literal are data. Anything left
    unbalanced raises Unparseable instead of guessing.
    """

    def __init__(self, text):
        self.text = text
        self.end = len(text)
        self.pos = 0
        self.commands = []  # (words, directory), substitutions before their command
        self.heredocs = []  # (delimiter, strip_tabs) still waiting for a body

    def split(self, cwd):
        self._list(cwd, None)
        return self.commands

    def _peek(self, offset=0):
        index = self.pos + offset
        return self.text[index] if index < self.end else ""

    def _blanks(self):
        while self.pos < self.end:
            ch = self.text[self.pos]
            if ch in " \t\r":
                self.pos += 1
            elif ch == "\\" and self.text.startswith("\n", self.pos + 1):
                self.pos += 2
            else:
                return

    def _list(self, cwd, closer):
        """Read commands up to `closer` (`)`, a backtick, or None for the end of the text)."""
        words, before = [], ""
        while True:
            self._blanks()
            ch = self._peek()
            if not ch:
                if closer:
                    raise Unparseable("unterminated " + closer)
                self._finish(words, before, "", cwd)
                return
            if ch == closer:
                self.pos += 1
                self._finish(words, before, "", cwd)
                return
            if ch == "#":
                newline = self.text.find("\n", self.pos)
                self.pos = self.end if newline < 0 else newline
                continue
            after = self._separator()
            if after:
                cwd = self._finish(words, before, after, cwd)
                words, before = [], after
                if after == "\n":
                    self._heredoc_bodies()
                continue
            if ch == "(":
                self.pos += 1
                self._list(cwd, ")")
                continue
            if self._redirection(cwd, closer):
                continue
            word = self._word(cwd, closer)
            if word is None:
                raise Unparseable("unexpected " + repr(ch))
            if word.isdigit() and self._peek() in ("<", ">"):
                continue  # the descriptor number of a redirection
            words.append(word)

    def _separator(self):
        """Consume the command separator at the cursor and return it, or "" for none."""
        ch, following = self._peek(), self._peek(1)
        if ch in ("\n", ";", ")"):
            self.pos += 1
            return ch
        if ch == "&":
            if following == ">":
                return ""
            self.pos += 2 if following == "&" else 1
            return "&&" if following == "&" else "&"
        if ch == "|":
            self.pos += 2 if following in ("|", "&") else 1
            return "||" if following == "|" else "|"
        return ""

    def _finish(self, words, before, after, cwd):
        """Record one simple command; return the directory the commands after it run in."""
        if not words:
            return cwd
        self.commands.append((words, cwd))
        if "|" in (before, after) or after == "&":
            return cwd  # runs in a subshell, so its `cd` does not carry over
        return _directory_after(words, cwd)

    def _heredoc_bodies(self):
        """Skip the bodies of the here-documents opened on the line just ended.

        A body with no closing delimiter runs to the end of the text, as the
        shell reads it.
        """
        for delimiter, strip_tabs in self.heredocs:
            while self.pos < self.end:
                newline = self.text.find("\n", self.pos)
                stop = self.end if newline < 0 else newline
                line = self.text[self.pos:stop]
                self.pos = min(stop + 1, self.end)
                if (line.lstrip("\t") if strip_tabs else line) == delimiter:
                    break
        self.heredocs = []

    def _redirection(self, cwd, closer):
        """Consume a redirection and its target at the cursor; False when there is none."""
        text, pos = self.text, self.pos
        if text.startswith(("<(", ">("), pos):
            return False
        if text.startswith("&>", pos):
            self.pos += 3 if text.startswith("&>>", pos) else 2
        elif text.startswith("<<<", pos):
            self.pos += 3
        elif text.startswith("<<", pos):
            self.pos += 2
            strip_tabs = self._peek() == "-"
            if strip_tabs:
                self.pos += 1
            self._blanks()
            delimiter = self._word(cwd, closer)
            if not delimiter or _DYN in delimiter:
                raise Unparseable("here-document delimiter")
            self.heredocs.append((delimiter, strip_tabs))
            return True
        elif text.startswith(("<", ">"), pos):
            self.pos += 1
            if self._peek() in (">", "|", "&"):
                self.pos += 1
        else:
            return False
        self._blanks()
        self._word(cwd, closer)
        return True

    def _word(self, cwd, closer):
        """Read one word with its quoting removed; None when the cursor is not on one."""
        text, start = self.text, self.pos
        parts = []
        while self.pos < self.end:
            ch = text[self.pos]
            if ch in " \t\r\n;|&)" or ch == closer:
                break
            if ch in "<>":
                if not text.startswith("(", self.pos + 1):
                    break
                self.pos += 2
                self._list(cwd, ")")
                parts.append(_DYN)
            elif ch == "(":
                if not (parts and parts[-1].endswith("=")):
                    break
                self.pos += 1
                kept, self.commands = self.commands, []
                self._list(cwd, ")")
                self.commands = kept
                parts.append(_DYN)
            elif ch == "\\":
                following = self._peek(1)
                if following != "\n":
                    parts.append(following or ch)
                self.pos += 2 if following else 1
            elif ch == "'":
                close = text.find("'", self.pos + 1)
                if close < 0:
                    raise Unparseable("unterminated single quote")
                parts.append(text[self.pos + 1:close])
                self.pos = close + 1
            elif ch == '"':
                self.pos += 1
                parts.append(self._double_quoted(cwd))
            elif ch == "`":
                self.pos += 1
                self._list(cwd, "`")
                parts.append(_DYN)
            elif ch == "$":
                parts.append(self._dollar(cwd, False))
            else:
                plain = _PLAIN_RE.match(text, self.pos)
                parts.append(plain.group())
                self.pos = plain.end()
        if self.pos == start:
            return None
        word = "".join(parts)
        if text[start] == "~" and (word == "~" or word.startswith("~/")):
            word = (os.environ.get("HOME") or _DYN) + word[1:]
        return word

    def _double_quoted(self, cwd):
        """Read to the closing double quote; the cursor starts just past the opening one."""
        parts = []
        while True:
            stop = _DQ_STOP_RE.search(self.text, self.pos)
            if not stop:
                raise Unparseable("unterminated double quote")
            parts.append(self.text[self.pos:stop.start()])
            self.pos = stop.start()
            ch = stop.group()
            if ch == '"':
                self.pos += 1
                return "".join(parts)
            if ch == "\\":
                following = self._peek(1)
                if not following:
                    raise Unparseable("unterminated double quote")
                if following in '$`"\\':
                    parts.append(following)
                elif following != "\n":
                    parts.append("\\" + following)
                self.pos += 2
            elif ch == "`":
                self.pos += 1
                self._list(cwd, "`")
                parts.append(_DYN)
            else:
                parts.append(self._dollar(cwd, True))

    def _dollar(self, cwd, quoted):
        """Consume the expansion at a `$` and return what it adds to the word."""
        text, following = self.text, self._peek(1)
        if following == "(":
            if text.startswith("((", self.pos + 1):
                self._arithmetic()
            else:
                self.pos += 2
                self._list(cwd, ")")
            return _DYN
        if following == "{":
            self.pos += 2
            self._braces(cwd, quoted)
            return _DYN
        if following == "'" and not quoted:
            self.pos += 2
            return self._ansi_c()
        if following == '"' and not quoted:
            self.pos += 2
            return self._double_quoted(cwd)
        name = _NAME_RE.match(text, self.pos + 1)
        if name:
            self.pos = name.end()
            return _DYN
        if following and following in "0123456789@*#?$!-":
            self.pos += 2
            return _DYN
        self.pos += 1
        return "$"

    def _arithmetic(self):
        depth = 0
        for index in range(self.pos + 1, self.end):
            ch = self.text[index]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    self.pos = index + 1
                    return
        raise Unparseable("unterminated $((")

    def _braces(self, cwd, quoted):
        """Skip a `${...}` expansion; the cursor starts just past the `${`."""
        text = self.text
        while self.pos < self.end:
            ch = text[self.pos]
            if ch == "}":
                self.pos += 1
                return
            if ch == "\\":
                self.pos += 2
            elif ch == "$":
                self._dollar(cwd, True)
            elif ch == "`":
                self.pos += 1
                self._list(cwd, "`")
            elif ch == '"':
                self.pos += 1
                self._double_quoted(cwd)
            elif ch == "'" and not quoted:
                close = text.find("'", self.pos + 1)
                if close < 0:
                    raise Unparseable("unterminated single quote")
                self.pos = close + 1
            else:
                self.pos += 1
        raise Unparseable("unterminated ${")

    def _ansi_c(self):
        """Read a `$'...'` string; the cursor starts just past the `$'`."""
        parts = []
        while self.pos < self.end:
            ch = self.text[self.pos]
            if ch == "'":
                self.pos += 1
                return "".join(parts)
            if ch == "\\":
                parts.append(_ANSI_C_ESCAPES.get(self._peek(1), _DYN))
                self.pos += 2
            else:
                parts.append(ch)
                self.pos += 1
        raise Unparseable("unterminated $' quote")


def _resolve_dir(base, path):
    """`path` as a directory relative to `base`; None when either is unknown."""
    if not path or _DYN in path:
        return None
    if os.path.isabs(path):
        return os.path.normpath(path)
    if base is None:
        return None
    return os.path.normpath(os.path.join(base, path))


def _options(args, short_valued="", long_valued=(), short_optional="", leading_only=False):
    """Separate option tokens from positionals.

    Returns (options, positionals). An option is (name, value): the name is
    spelled `-x` or `--long`, the value is None for a flag. `short_valued` and
    `long_valued` name the options that consume a value, `short_optional` the
    short ones whose value is optional and must be attached; a value never
    surfaces as an option or a positional. `--` ends option parsing. With
    `leading_only`, parsing stops at the first positional and everything from
    there on is returned as positionals.
    """
    options, positionals = [], []
    index = 0
    while index < len(args):
        arg = args[index]
        index += 1
        if arg == "--":
            positionals.extend(args[index:])
            break
        if arg.startswith("--"):
            name, attached, value = arg.partition("=")
            if not attached and name in long_valued:
                value = args[index] if index < len(args) else ""
                index += 1
            elif not attached:
                value = None
            options.append((name, value))
        elif arg.startswith("-") and len(arg) > 1:
            at = 1
            while at < len(arg):
                letter = arg[at]
                at += 1
                if letter in short_valued:
                    if at < len(arg):
                        value = arg[at:]
                    else:
                        value = args[index] if index < len(args) else ""
                        index += 1
                    options.append(("-" + letter, value))
                    break
                if letter in short_optional:
                    options.append(("-" + letter, arg[at:] or None))
                    break
                options.append(("-" + letter, None))
        elif leading_only:
            positionals.extend(args[index - 1:])
            break
        else:
            positionals.append(arg)
    return options, positionals


def _leading(args, short_valued="", long_valued=()):
    """Split `args` at the first positional: (that word or None, the words after it)."""
    _, rest = _options(args, short_valued, long_valued, leading_only=True)
    return (rest[0], rest[1:]) if rest else (None, [])


def _program(word):
    """The program a command word runs: its basename, lowercased, without `.exe`."""
    name = word.replace("\\", "/").rsplit("/", 1)[-1].lower()
    return name[:-4] if name.endswith(".exe") else name


_KEYWORDS = frozenset({"if", "then", "elif", "else", "while", "until", "do", "!", "{"})

# program -> (short options that take a value, long options that take one,
# short options whose value is optional and attached). What follows the
# options is the command the wrapper runs.
_WRAPPERS = {
    "command": ("", (), ""),
    "exec": ("a", (), ""),
    "nohup": ("", (), ""),
    "time": ("fo", ("--format", "--output"), ""),
    "timeout": ("ks", ("--kill-after", "--signal"), ""),
    "env": ("CPSu", ("--chdir", "--split-string", "--unset"), ""),
    "xargs": (
        "adEIJLnPRSs",
        ("--arg-file", "--delimiter", "--max-args", "--max-chars", "--max-lines", "--max-procs"),
        "eil",
    ),
}


def _strip_prefix(words):
    """Drop leading shell keywords and assignments; return (rest, assignments)."""
    assignments = {}
    index = 0
    while index < len(words):
        assignment = _ASSIGNMENT_RE.match(words[index])
        if assignment:
            assignments[assignment.group(1)] = words[index][assignment.end():]
        elif words[index] not in _KEYWORDS:
            break
        index += 1
    return words[index:], assignments


def _unwrap(words, cwd):
    """Strip keywords, assignments and wrappers down to the program actually run.

    Returns (argv, assignments, directory); argv is empty when nothing runs.
    """
    assignments = {}
    rest = words
    while True:
        rest, more = _strip_prefix(rest)
        assignments.update(more)
        name = _program(rest[0]) if rest else ""
        if name not in _WRAPPERS:
            return rest, assignments, cwd
        options, rest = _options(rest[1:], *_WRAPPERS[name], leading_only=True)
        given = dict(options)
        if name == "command" and ("-v" in given or "-V" in given):
            return [], assignments, cwd
        if name == "timeout":
            rest = rest[1:]
        if name == "env":
            for flag in ("-C", "--chdir"):
                if flag in given:
                    cwd = _resolve_dir(cwd, given[flag])


def _directory_after(words, cwd):
    """The shell's working directory once the simple command `words` has run."""
    argv, _ = _strip_prefix(words)
    if not argv or argv[0] not in ("cd", "pushd", "popd"):
        return cwd
    targets = [arg for arg in argv[1:] if arg == "-" or not arg.startswith("-")]
    if argv[0] == "popd" or (targets and targets[0] == "-"):
        return None
    if not targets:
        return os.environ.get("HOME") if argv[0] == "cd" else None
    return _resolve_dir(cwd, targets[0])


_SHELLS = frozenset({"bash", "sh", "zsh"})


def _dash_c_script(args):
    """The command string of `sh -c <string>`; None when the shell runs a file or stdin."""
    options, positionals = _options(args, "oO", ("--init-file", "--rcfile"), leading_only=True)
    if "-c" not in dict(options) or not positionals:
        return None
    return positionals[0]


class _Git:
    """One git invocation with its global options read."""

    __slots__ = ("sub", "args", "cwd", "config")

    def __init__(self, sub, args, cwd, config):
        self.sub = sub
        self.args = args
        self.cwd = cwd  # None when the repository cannot be told from the command
        self.config = config  # lowercased keys of the `-c key=value` overrides

    def options(self):
        return _options(self.args, *_GIT_OPTIONS.get(self.sub, ("", (), "")))


_GIT_GLOBAL_VALUED = frozenset({
    "--attr-source", "--config-env", "--git-dir", "--namespace", "--super-prefix", "--work-tree",
})

# subcommand -> (short options that take a value, long options that take one,
# short options whose value is optional and attached). Without these a commit
# message or a merge strategy would be read as flags.
_GIT_OPTIONS = {
    "commit": (
        "CcFmt",
        ("--author", "--cleanup", "--date", "--file", "--fixup", "--message",
         "--pathspec-from-file", "--reedit-message", "--reuse-message", "--squash",
         "--template", "--trailer"),
        "Su",
    ),
    "push": ("o", ("--exec", "--push-option", "--receive-pack", "--repo"), ""),
    "merge": (
        "FmsX",
        ("--cleanup", "--file", "--into-name", "--message", "--strategy", "--strategy-option"),
        "S",
    ),
}


def _git_call(argv, assignments, cwd):
    """Read git's global options off `argv`; None when no subcommand follows them."""
    relocated = "GIT_DIR" in assignments or "GIT_WORK_TREE" in assignments
    config = []
    index = 1
    while index < len(argv):
        arg = argv[index]
        value = argv[index + 1] if index + 1 < len(argv) else ""
        if arg == "-C":
            cwd = _resolve_dir(cwd, value)
            index += 2
        elif arg == "-c":
            config.append(value.partition("=")[0].lower())
            index += 2
        elif arg in _GIT_GLOBAL_VALUED:
            relocated = relocated or arg in ("--git-dir", "--work-tree")
            index += 2
        elif arg.startswith("-"):
            relocated = relocated or arg.startswith(("--git-dir=", "--work-tree="))
            index += 1
        else:
            return _Git(arg, argv[index + 1:], None if relocated else cwd, config)
    return None


class _Command:
    """One simple command, reduced to the program it runs."""

    __slots__ = ("name", "argv", "cwd", "git")

    def __init__(self, argv, assignments, cwd):
        self.name = _program(argv[0])
        self.argv = argv
        self.cwd = cwd
        self.git = _git_call(argv, assignments, cwd) if self.name == "git" else None


def simple_commands(line, cwd, depth=0):
    """Yield a _Command for each simple command `line` runs, in execution order.

    The string handed to `sh -c` or `eval` is split in turn, so its commands
    are yielded in place of the shell that runs them.
    """
    if depth > _MAX_NESTING:
        raise Unparseable("shells nested too deep")
    for words, directory in _Splitter(line).split(cwd):
        argv, assignments, directory = _unwrap(words, directory)
        if not argv:
            continue
        command = _Command(argv, assignments, directory)
        script = None
        if command.name in _SHELLS:
            script = _dash_c_script(argv[1:])
        elif command.name == "eval":
            script = " ".join(argv[1:])
        if script is None:
            yield command
        else:
            yield from simple_commands(script, directory, depth + 1)


# ----------------------------------------------------------------------
# Repository lookups
# ----------------------------------------------------------------------
_lookups = {}

_REPO_TAIL_RE = re.compile(r"(?:^|[/:])([^/:]+)/([^/:]+?)(?:\.git)?/*$")


def _run_git(directory, args):
    """stdout of `git -C <directory> <args>`; None when git fails, hangs or is absent."""
    import subprocess

    try:
        done = subprocess.run(
            [*GIT_ARGV, "-C", directory, *args],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            timeout=LOOKUP_TIMEOUT_S,
        )
    except (OSError, ValueError, subprocess.SubprocessError):
        return None
    if done.returncode != 0:
        return None
    return done.stdout.decode("utf-8", "replace").strip() or None


def _lookup(directory, *args):
    """A read-only git query in `directory`, asked once; None when it has no answer."""
    if directory is None:
        return None
    key = (directory, args)
    if key not in _lookups:
        _lookups[key] = _run_git(directory, args)
    return _lookups[key]


def _on_protected_branch(git):
    return _lookup(git.cwd, "symbolic-ref", "--quiet", "--short", "HEAD") in PROTECTED_BRANCHES


def _names_fleet_repo(url):
    tail = _REPO_TAIL_RE.search(url.strip().lower())
    return bool(tail) and tail.group(1) + "/" + tail.group(2) in FLEET_REPOS


def _push_url(git, remote):
    """The URL a push to `remote` reaches; None when it cannot be resolved."""
    if _DYN in remote:
        return None
    if "/" in remote or ":" in remote or "\\" in remote or remote.startswith("."):
        return remote  # a URL or a path, not a configured remote's name
    return _lookup(git.cwd, "remote", "get-url", "--push", remote)


# ----------------------------------------------------------------------
# Rules. Each takes a _Command and `settled`, and returns a reason to deny.
# `settled` is False once an earlier command on the line may have switched
# the branch or rewritten the remotes: a lookup made before the line runs no
# longer describes the repository this command will see.
# ----------------------------------------------------------------------
_STATE_KEEPING_GIT = frozenset({
    "add", "commit", "diff", "fetch", "log", "merge", "mv", "pull", "push", "reset",
    "restore", "rev-parse", "rm", "show", "status", "tag",
})
_STATE_KEEPING_PROGRAMS = frozenset({
    "cd", "pushd", "popd", "cat", "echo", "printf", "true", ":", "test", "[",
})


def _keeps_repo_state(command):
    if command.name == "git":
        return command.git is not None and command.git.sub in _STATE_KEEPING_GIT
    return command.name in _STATE_KEEPING_PROGRAMS


def _refspec_target(refspec):
    """The remote branch a refspec writes, or `HEAD` for the current branch."""
    source, colon, target = refspec.lstrip("+").partition(":")
    name = target if colon else source
    if name.startswith("refs/heads/"):
        name = name[len("refs/heads/"):]
    return "HEAD" if name == "@" else name


def _rule_push_protected(command, settled):
    git = command.git
    if git is None or git.sub != "push":
        return None
    options, positionals = git.options()
    flags = {name for name, _ in options}
    remote = positionals[0] if positionals else "origin"
    targets = []
    words = iter(positionals[1:])
    for word in words:
        if word == "tag":
            next(words, None)
        else:
            targets.append(_refspec_target(word))

    if any(target in PROTECTED_BRANCHES for target in targets):
        url = _push_url(git, remote)
        return PUSH_REASON if url is None or _names_fleet_repo(url) else None

    sweeping = bool(flags & {"--all", "--branches", "--mirror"})
    current = "HEAD" in targets or (len(positionals) < 2 and "--tags" not in flags)
    if not sweeping and not (current and settled and _on_protected_branch(git)):
        return None
    url = _push_url(git, remote)
    return PUSH_REASON if url is not None and _names_fleet_repo(url) else None


_PYTHON_RE = re.compile(r"python[0-9.]*")
_MERGE_MUTATIONS = ("mergePullRequest", "enablePullRequestAutoMerge", "mergeBranch")
_BRANCH_MUTATIONS = (
    "updateRef", "createCommitOnBranch", "updateBranchProtectionRule",
    "deleteBranchProtectionRule",
)
_GH_API_VALUED = (
    "--cache", "--field", "--header", "--hostname", "--input", "--jq", "--method",
    "--preview", "--raw-field", "--template",
)
_BODY_OPTIONS = frozenset({"-f", "-F", "--field", "--raw-field", "--input"})
_READ_METHODS = frozenset({"GET", "HEAD"})
_PROTECTED_ALTERNATION = "|".join(sorted(PROTECTED_BRANCHES))
_MERGE_ENDPOINT_RE = re.compile(r"(?:^|/)(?:pulls/[^/]+/merge|merges)$")
_BRANCH_ENDPOINT_RE = re.compile(
    r"(?:^|/)(?:git/refs/heads/(?:%s)$|branches/(?:%s)/protection(?:/|$))"
    % (_PROTECTED_ALTERNATION, _PROTECTED_ALTERNATION)
)


def _api_path(endpoint):
    path = endpoint.split("?", 1)[0]
    if "://" in path:
        path = path.split("://", 1)[1].partition("/")[2]
    return path.strip("/")


def _gh_api_reason(args):
    options, positionals = _options(args, "FfHpqtX", _GH_API_VALUED)
    endpoint = positionals[0] if positionals else ""
    if endpoint == "graphql":
        text = " ".join(args)
        if any(name in text for name in _MERGE_MUTATIONS):
            return MERGE_REASON
        if any(name in text for name in _BRANCH_MUTATIONS):
            return API_WRITE_REASON
        return None
    method = None
    sends_body = False
    for name, value in options:
        if name in ("-X", "--method") and value:
            method = value.upper()
        elif name in _BODY_OPTIONS:
            sends_body = True
    if method is None:
        method = "POST" if sends_body else "GET"
    if method in _READ_METHODS or _DYN in method:
        return None
    path = _api_path(endpoint)
    if _MERGE_ENDPOINT_RE.search(path):
        return MERGE_REASON
    if _BRANCH_ENDPOINT_RE.search(path):
        return API_WRITE_REASON
    return None


def _script_operand(command):
    """The file an interpreter command runs; None when it runs none."""
    args = command.argv[1:]
    if command.name in _SHELLS:
        script, _ = _leading(args, "oO", ("--init-file", "--rcfile"))
        return script
    if _PYTHON_RE.fullmatch(command.name):
        options, operands = _options(args, "cmWX", leading_only=True)
        given = dict(options)
        if operands and "-c" not in given and "-m" not in given:
            return operands[0]
    return None


def _rule_merge(command, settled):
    args = command.argv[1:]
    script = _script_operand(command)
    if "fleet-automerge" in (command.name, _program(script or "")):
        return MERGE_REASON
    if command.name != "gh":
        return None
    group, rest = _leading(args, "R", ("--repo",))
    if group == "pr":
        verb, _ = _leading(rest, "R", ("--repo",))
        return MERGE_REASON if verb == "merge" else None
    if group == "api":
        return _gh_api_reason(rest)
    return None


def _rule_skip_hooks(command, settled):
    git = command.git
    if git is None or git.sub not in ("commit", "push", "merge"):
        return None
    if "core.hookspath" in git.config:
        return SKIP_HOOKS_REASON
    flags = {name for name, _ in git.options()[0]}
    if "--no-verify" in flags or (git.sub == "commit" and "-n" in flags):
        return SKIP_HOOKS_REASON
    return None


def _rule_commit_protected(command, settled):
    git = command.git
    if git is None or git.sub != "commit" or not settled:
        return None
    if "--dry-run" in {name for name, _ in git.options()[0]}:
        return None
    if not _on_protected_branch(git):
        return None
    url = _lookup(git.cwd, "remote", "get-url", "origin")
    return COMMIT_REASON if url is not None and _names_fleet_repo(url) else None


RULES = (
    ("push-protected", _rule_push_protected),
    ("merge", _rule_merge),
    ("skip-hooks", _rule_skip_hooks),
    ("commit-protected", _rule_commit_protected),
)


def evaluate(command, cwd):
    """(rule name, reason) for the first rule `command` matches; None to allow it."""
    if _DYN in command:
        return None
    try:
        commands = list(simple_commands(command, cwd))
    except (Unparseable, RecursionError) as error:
        _log("command not split, allowed: %s" % error)
        return None
    settled = True
    for simple in commands:
        for name, rule in RULES:
            reason = rule(simple, settled)
            if reason:
                return name, reason
        settled = settled and _keeps_repo_state(simple)
    return None


# ----------------------------------------------------------------------
# Hook entry point
# ----------------------------------------------------------------------
def _log(message):
    """Append one line to the guard log. Best effort: never raises."""
    try:
        home = os.environ.get("FLEET_HOME") or os.path.join(os.path.expanduser("~"), ".fleet")
        log_dir = os.path.join(home, "logs")
        os.makedirs(log_dir, exist_ok=True)
        stamp = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        with open(os.path.join(log_dir, "guard-bash.log"), "a", encoding="utf-8") as log:
            log.write("%s fleet-guard-bash[%d]: %s\n" % (stamp, os.getpid(), message))
    except Exception:
        pass


def run(raw):
    """The hook's stdout for the PreToolUse JSON `raw`: a deny document, or ""."""
    try:
        payload = json.loads(raw)
    except ValueError:
        _log("hook input is not JSON, allowed")
        return ""
    tool_input = payload.get("tool_input") if isinstance(payload, dict) else None
    command = tool_input.get("command") if isinstance(tool_input, dict) else None
    if not isinstance(command, str):
        return ""
    cwd = payload.get("cwd")
    verdict = evaluate(command, cwd if isinstance(cwd, str) and cwd else None)
    if verdict is None:
        return ""
    return json.dumps({"hookSpecificOutput": {
        "hookEventName": "PreToolUse",
        "permissionDecision": "deny",
        "permissionDecisionReason": verdict[1],
    }})


def _expire():
    _log("deadline exceeded, allowed")
    os._exit(0)


def main():
    watchdog = threading.Timer(DEADLINE_S, _expire)
    watchdog.daemon = True
    watchdog.start()
    try:
        output = run(sys.stdin.buffer.read().decode("utf-8", "replace"))
    except BaseException as error:
        _log("internal failure, allowed: %r" % (error,))
        return 0
    if output:
        # Bytes, so a native-Windows text stream cannot turn the newline into CRLF.
        sys.stdout.buffer.write((output + "\n").encode("utf-8"))
        sys.stdout.buffer.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
