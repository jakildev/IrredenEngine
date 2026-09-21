#!/usr/bin/env python3
"""Whole-tree ratchet: native-tool stdout is CR-stripped before Bash reads it.

Native-Windows `python3 print()` and `jq -r` CRLF-terminate every line.
`read -r` and `mapfile -t` strip only the `\\n`, so the CR rides the last field
into every later string comparison, `case` match and `gh … --remove-label`
call (scripts/fleet/CLAUDE.md §"Bash pitfalls"). This is that rule's executed
form: a whole-tree ratchet rather than a diff-time check, because the sites it
guards against predate any reviewer who would look for them.

What is flagged
---------------
A Bash pipeline whose *producer* is `python3 -c` (`python -c`) or a raw-output
`jq` — or a same-file function that forwards such a producer's stdout — and
whose output reaches a line-oriented *consumer* without a CR strip in between:

  producer | while … read            direct pipe into a reader
  … < <(producer)                    process substitution into a reader
  v=$(producer); … <<< "$v"          capture, then a reader on the variable
  v=$(producer); [[ "$v" == x ]]     capture, then a string test / `case`
  v=$(producer); gh … --add-label "$v"   capture, then a label argument
  w=$(echo "$v" | sed -n 1p)         a re-capture carries the taint one hop on

The exclusion is drawn on the *consumer*, not the payload: a numeric capture
that only ever reaches `(( ))`, `-eq`/`-gt` and friends is out of contract
(bash arithmetic fails loudly on a stray CR — a different defect); the same
capture reaching `==`, `case` or a label position is in.

The guard must sit in the producer's own stdout pipeline, after the native
tool and before the capture or read: `… | tr -d '\\r' …` (any `tr -d` whose
set names CR, or `dos2unix`). A guard on an unrelated pipeline, a strip
upstream of the producer, or a post-capture cleanup (`${v//$'\\r'/}`) does not
count — the CR is still in the variable everything else reads.

Out of contract for this detector: `eval`/`bash -c` strings, cross-file
call-graph inference, `python3 - <<'PY'` heredoc and `python3 module.py`
producers, and `gh --jq` (a Go binary; not observed to CRLF).

Scope
-----
Bash under `scripts/fleet/` by default (`.sh`, `.bash`, and extension-less
files whose first line is a sh/bash shebang), tests included: a test's
executable statements are real Bash. Comments, quoted strings and heredoc
bodies are data — a fixture that *writes* an offending script never fires.

There is no inline suppression and no growable baseline: `EXEMPT_FILES` is
frozen empty and pinned by `tests/test_lint_crlf_producers.py` (a file may
leave it, never join it). Every finding is fixed at its producer.

Exit status: 0 clean, 1 findings, 2 usage error or a scope that resolved to
zero Bash files (a zero-file scope can never read as a clean pass). Coverage
is printed to stderr on every run. stdlib-only, `pathlib` walk, POSIX
`file:line` refs — it runs on the Windows CI lint runner too.
"""
import re
import sys
from pathlib import Path

# Shrink-only. Pinned empty by tests/test_lint_crlf_producers.py; a file may
# leave this set, never join it. No inline opt-out marker exists by design.
EXEMPT_FILES = frozenset()

_SKIP_DIRS = {"__pycache__"}
_SHEBANG_RE = re.compile(r"^#!.*\b(ba)?sh\b")

_PRODUCER_PYTHON = "python3 -c"
_PRODUCER_JQ = "jq -r"

_READERS = {"read", "mapfile", "readarray"}
_KEYWORDS = {"if", "then", "else", "elif", "fi", "while", "until", "do", "done",
             "for", "in", "case", "esac", "!", "{", "}", "time", "function",
             "select", "coproc"}
_ASSIGN_KEYWORDS = {"local", "export", "declare", "readonly", "typeset"}
_STRING_OPS = {"==", "=", "!=", "=~", "<", ">", "-n", "-z"}
_LABEL_FLAGS = {"--add-label", "--remove-label", "--label", "--labels", "-l"}
# A native producer whose stdout is only ever narrowed by one of these still
# forwards CR-terminated lines; anything else (grep -q, wc, jq, a consumer) ends
# the forwarding chain.
_PASS_THROUGH = {"sed", "head", "tail", "sort", "uniq", "cut", "awk", "grep",
                 "cat", "tee", "tac", "tr"}
_ECHOERS = {"echo", "printf"}
_CR_LITERAL_RE = re.compile(r"\\r|\\015|\\x0[dD]")
_IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_ASSIGN_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)(\[[^\]]*\])?\+?=")


# --- Lexer -----------------------------------------------------------------
#
# A deliberately small Bash reader: enough structure to tell a producer from
# the data it prints. It tracks quotes, `$'…'`, comments, backslash-newline,
# heredoc bodies (skipped), `$(…)` / `` `…` `` / `<(…)` / `>(…)` substitutions
# (lexed recursively), `$((…))` and `((…))` arithmetic (opaque — an arithmetic
# read is the excluded consumer), and `case … esac` so a pattern's `)` does not
# close an enclosing substitution. It never executes anything.

class Word:
    __slots__ = ("text", "pos", "refs", "subs", "funcdef", "arith")

    def __init__(self, pos):
        self.text = ""
        self.pos = pos
        self.refs = set()     # variables expanded in this word's own text
        self.subs = []        # nested Sub objects
        self.funcdef = False  # `name()` — a function definition head
        self.arith = False    # `((…))` command — never a string consumer

    def __repr__(self):
        return f"Word({self.text!r})"


class Sub:
    __slots__ = ("kind", "pos", "statements")

    def __init__(self, kind, pos, statements):
        self.kind = kind            # "$(", "`", "<(", ">("
        self.pos = pos
        self.statements = statements


class Op:
    __slots__ = ("text", "pos")

    def __init__(self, text, pos):
        self.text = text
        self.pos = pos

    def __repr__(self):
        return f"Op({self.text!r})"


_PIPES = {"|", "|&"}   # every other Op ends the pipeline


class _Lexer:
    def __init__(self, text):
        self.text = text
        self.n = len(text)

    def lex(self, i, terminator):
        """Lex from `i` until `terminator` (")", "`" or None for EOF).

        Returns (items, index just past the terminator)."""
        text, n = self.text, self.n
        items = []
        word = None
        paren_depth = 0
        case_depth = 0
        pending_heredocs = []
        at_stmt_start = True

        in_test = False   # inside `[[ … ]]`, where && || < > are operators

        def flush():
            nonlocal word, in_test
            if word is not None:
                if word.text == "[[":
                    in_test = True
                elif word.text == "]]":
                    in_test = False
                items.append(word)
                word = None

        def start_word():
            nonlocal word
            if word is None:
                word = Word(i)

        while i < n:
            c = text[i]
            if c == "\\" and i + 1 < n:
                if text[i + 1] == "\n":
                    i += 2
                    continue
                start_word()
                word.text += text[i:i + 2]
                i += 2
                continue
            if c == "'":
                start_word()
                j = text.find("'", i + 1)
                j = n if j < 0 else j
                word.text += text[i:j + 1]
                i = j + 1
                continue
            if c == '"':
                start_word()
                i = self._double_quoted(i, word)
                continue
            if c == "$" and text.startswith("$'", i):
                start_word()
                j = i + 2
                while j < n and text[j] != "'":
                    j += 2 if text[j] == "\\" else 1
                word.text += text[i:j + 1]
                i = j + 1
                continue
            if c == "$" and text.startswith("$((", i):
                start_word()
                i = self._arith(i, word)
                continue
            if c == "$" and text.startswith("$(", i):
                start_word()
                inner, end = self.lex(i + 2, ")")
                word.subs.append(Sub("$(", i, split_statements(inner)))
                word.text += "$(…)"
                i = end
                continue
            if c == "`":
                if terminator == "`":
                    flush()
                    return items, i + 1
                start_word()
                inner, end = self.lex(i + 1, "`")
                word.subs.append(Sub("`", i, split_statements(inner)))
                word.text += "`…`"
                i = end
                continue
            if c == "$":
                start_word()
                i = self._dollar(i, word)
                continue
            if c in "<>" and text.startswith(c + "(", i):
                start_word()
                inner, end = self.lex(i + 2, ")")
                word.subs.append(Sub(c + "(", i, split_statements(inner)))
                word.text += c + "(…)"
                i = end
                continue
            if c == "<" and text.startswith("<<<", i):
                start_word()
                word.text += "<<<"
                i += 3
                continue
            if c == "<" and text.startswith("<<", i):
                start_word()
                j = i + 2
                if j < n and text[j] == "-":
                    j += 1
                while j < n and text[j] in " \t":
                    j += 1
                k = j
                while k < n and text[k] not in " \t\n;|&)":
                    k += 1
                tag = text[j:k].strip("'\"")
                if tag:
                    pending_heredocs.append(tag)
                word.text += text[i:k]
                i = k
                continue
            if c == "#" and word is None:
                j = text.find("\n", i)
                i = n if j < 0 else j
                continue
            if c == "\n":
                flush()
                items.append(Op("\n", i))
                i += 1
                if pending_heredocs:
                    i = self._skip_heredocs(i, pending_heredocs)
                    pending_heredocs = []
                at_stmt_start = True
                continue
            if c in " \t":
                flush()
                i += 1
                continue
            if in_test and c in "&|()" and word is None:
                # `[[ a && b ]]` — conjunction inside a test, not a separator.
                for op in ("&&", "||", "(", ")"):
                    if text.startswith(op, i):
                        items.append(Word(i))
                        items[-1].text = op
                        i += len(op)
                        break
                else:
                    start_word()
                    word.text += c
                    i += 1
                continue
            if c == "(" and word is not None and _ASSIGN_RE.fullmatch(word.text):
                # `arr=( … )` — the list's expansions belong to the assignment.
                inner, end = self.lex(i + 1, ")")
                for it in inner:
                    if isinstance(it, Word):
                        word.refs |= it.refs
                        word.subs.extend(it.subs)
                word.text += "(…)"
                i = end
                continue
            if c == "(" and word is None and text.startswith("((", i):
                # Arithmetic command `(( … ))` (also after `if`/`while`/`!`):
                # opaque, never a string consumer.
                start_word()
                word.arith = True
                i = self._arith(i, word)
                continue
            if c == "(":
                # `name()` right after a word is a function-definition head.
                if word is not None and text.startswith("()", i) \
                        and _IDENT_RE.fullmatch(word.text):
                    word.funcdef = True
                    word.text += "()"
                    i += 2
                    continue
                flush()
                paren_depth += 1
                items.append(Op("(", i))
                i += 1
                at_stmt_start = True
                continue
            if c == ")":
                flush()
                if paren_depth > 0:
                    paren_depth -= 1
                    items.append(Op(")", i))
                elif case_depth > 0:
                    items.append(Op(")", i))   # end of a `case` pattern
                elif terminator == ")":
                    return items, i + 1
                else:
                    items.append(Op(")", i))   # stray — tolerate
                i += 1
                at_stmt_start = True
                continue
            if c == "&" and word is not None and word.text[-1:] in "<>":
                word.text += c   # `>&2`, `2>&1` — a redirection, not a separator
                i += 1
                continue
            if c == "&" and text.startswith("&>", i):
                start_word()
                word.text += "&>"
                i += 2
                continue
            if c in ";&|":
                flush()
                for op in (";;&", ";;", ";&", "&&", "||", "|&", ";", "&", "|"):
                    if text.startswith(op, i):
                        items.append(Op(op, i))
                        i += len(op)
                        break
                at_stmt_start = True
                continue
            start_word()
            if at_stmt_start and word.text == "":
                # Keyword bookkeeping happens once the word is complete; peek.
                m = _IDENT_RE.match(text, i)
                if m and m.group(0) == "case" and not text[m.end():m.end() + 1].strip("\t "):
                    case_depth += 1
                elif m and m.group(0) == "esac":
                    case_depth = max(0, case_depth - 1)
            word.text += c
            at_stmt_start = False
            i += 1
        flush()
        return items, i

    def _double_quoted(self, i, word):
        text, n = self.text, self.n
        j = i + 1
        buf = '"'
        while j < n:
            c = text[j]
            if c == "\\" and j + 1 < n:
                buf += text[j:j + 2]
                j += 2
                continue
            if c == '"':
                buf += c
                j += 1
                break
            if c == "$" and text.startswith("$((", j):
                word.text += buf
                buf = ""
                j = self._arith(j, word)
                continue
            if c == "$" and text.startswith("$(", j):
                word.text += buf
                buf = ""
                inner, end = self.lex(j + 2, ")")
                word.subs.append(Sub("$(", j, split_statements(inner)))
                word.text += "$(…)"
                j = end
                continue
            if c == "`":
                word.text += buf
                buf = ""
                inner, end = self.lex(j + 1, "`")
                word.subs.append(Sub("`", j, split_statements(inner)))
                word.text += "`…`"
                j = end
                continue
            if c == "$":
                word.text += buf
                buf = ""
                j = self._dollar(j, word)
                continue
            buf += c
            j += 1
        word.text += buf
        return j

    def _dollar(self, i, word):
        # `$name`, `${name…}` — record the expanded variable; `${!x}`, `$1`,
        # `$?` and friends are not tracked.
        text = self.text
        if text.startswith("${", i):
            j = i + 2
            m = _IDENT_RE.match(text, j)
            if m:
                word.refs.add(m.group(0))
            elif text.startswith("#", j):
                m2 = _IDENT_RE.match(text, j + 1)
                if m2:
                    word.refs.add(m2.group(0))
            depth = 1
            k = j
            while k < len(text) and depth:
                ch = text[k]
                if ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                elif ch == "\\":
                    k += 1
                elif text.startswith("$'", k) or ch == "'":
                    # A quoted stretch inside `${…}` (`${v%%$'\t'*}`) is a
                    # pattern; skip it whole so its quote cannot pair with a
                    # later one.
                    k += 2 if ch == "$" else 1
                    while k < len(text) and text[k] != "'":
                        k += 2 if text[k] == "\\" else 1
                k += 1
            word.text += text[i:k]
            return k
        m = _IDENT_RE.match(text, i + 1)
        if m:
            word.refs.add(m.group(0))
            word.text += text[i:m.end()]
            return m.end()
        word.text += "$"
        return i + 1

    def _arith(self, i, word):
        # Skip a balanced `((…))` / `$((…))` region as opaque text.
        text = self.text
        j = text.index("((", i) + 2
        depth = 2
        while j < len(text) and depth:
            if text[j] == "(":
                depth += 1
            elif text[j] == ")":
                depth -= 1
            j += 1
        word.text += text[i:j]
        return j

    def _skip_heredocs(self, i, tags):
        text = self.text
        for tag in tags:
            while i < len(text):
                j = text.find("\n", i)
                line = text[i:] if j < 0 else text[i:j]
                i = len(text) if j < 0 else j + 1
                if line.lstrip("\t") == tag:
                    break
        return i


def lex_file(text):
    items, _ = _Lexer(text).lex(0, None)
    return items


# --- Statements ------------------------------------------------------------

class Command:
    __slots__ = ("words", "pos")

    def __init__(self, words):
        self.words = words
        self.pos = words[0].pos

    @property
    def name(self):
        """The command name: the first word that is neither a keyword nor an
        assignment prefix, so `while IFS= read -r x` names `read`; a
        declaration keyword (`local v=…`) is the name itself."""
        skip_assigns = self.first not in _ASSIGN_KEYWORDS
        for w in self.words:
            if w.arith:
                return "(("
            if w.funcdef or w.text in _KEYWORDS or (skip_assigns and _ASSIGN_RE.match(w.text)):
                continue
            return w.text
        return ""

    @property
    def first(self):
        return self.words[0].text if self.words else ""

    @property
    def text(self):
        return " ".join(w.text for w in self.words)


class Pipeline:
    __slots__ = ("commands", "pos")

    def __init__(self, commands):
        self.commands = commands
        self.pos = commands[0].pos


def split_statements(items):
    """Group lexed items into pipelines; each pipeline is a list of commands."""
    pipelines = []
    commands = []
    words = []

    def end_command():
        nonlocal words
        if words:
            commands.append(Command(words))
            words = []

    def end_pipeline():
        nonlocal commands
        end_command()
        if commands:
            pipelines.append(Pipeline(commands))
            commands = []

    for it in items:
        if isinstance(it, Op):
            if it.text in _PIPES:
                end_command()
            else:
                end_pipeline()
        else:
            words.append(it)
    end_pipeline()
    return pipelines


# --- Classification ----------------------------------------------------------

def _is_native_producer(cmd):
    """Return the producer kind for a `python3 -c` / raw-output `jq` command."""
    words = [w.text for w in cmd.words]
    name = cmd.name
    try:
        idx = words.index(name)
    except ValueError:
        return None
    args = words[idx + 1:]
    base = name.rsplit("/", 1)[-1]
    if base in ("python3", "python"):
        return _PRODUCER_PYTHON if "-c" in args else None
    if base == "jq":
        for a in args:
            if a == "--raw-output":
                return _PRODUCER_JQ
            if a.startswith("-") and not a.startswith("--") and "r" in a[1:]:
                return _PRODUCER_JQ
        return None
    return None


def _is_guard(cmd):
    """`tr -d` naming CR, or `dos2unix`: strips CR from the stream."""
    base = cmd.name.rsplit("/", 1)[-1]
    if base == "dos2unix":
        return True
    if base != "tr":
        return False
    words = [w.text for w in cmd.words]
    has_delete = any(w == "-d" or (w.startswith("-") and not w.startswith("--")
                                   and "d" in w[1:]) or w == "--delete"
                     for w in words)
    return has_delete and any(_CR_LITERAL_RE.search(w) for w in words)


def _is_reader(cmd):
    return cmd.name in _READERS


class Finding:
    __slots__ = ("path", "line", "producer", "command", "function", "variable",
                 "consumer_line", "consumer")

    def __init__(self, path, line, producer, command, function, variable,
                 consumer_line, consumer):
        self.path = path
        self.line = line              # the producer's line
        self.producer = producer      # "python3 -c", "jq -r", or "<fn>() (forwards …)"
        self.command = command        # the producer command's source text
        self.function = function      # enclosing function, or None at top level
        self.variable = variable      # the capture variable, or None
        self.consumer_line = consumer_line
        self.consumer = consumer

    def render(self):
        where = f" in {self.function}()" if self.function else ""
        via = f" captured into ${self.variable}" if self.variable else ""
        return (f"{self.path}:{self.line}: unguarded {self.producer} stdout{via}{where} "
                f"reaches {self.consumer} at line {self.consumer_line}; strip CR at the "
                f"producer (`| tr -d '\\r'`) — scripts/fleet/CLAUDE.md §Bash pitfalls")


class Taint:
    __slots__ = ("line", "producer", "command")

    def __init__(self, line, producer, command):
        self.line = line
        self.producer = producer
        self.command = command


class _Analyzer:
    def __init__(self, path, text, forwarding):
        self.path = path
        self.text = text
        self.forwarding = forwarding  # function name -> Taint of its own producer
        self.findings = []
        self._seen = set()
        self.function = None

    def line_of(self, pos):
        return self.text.count("\n", 0, pos) + 1

    def producer_of(self, cmd):
        """A native producer, or a same-file forwarding function; else None."""
        kind = _is_native_producer(cmd)
        if kind:
            return Taint(self.line_of(cmd.pos), kind, cmd.text)
        fwd = self.forwarding.get(cmd.name)
        if fwd:
            return Taint(self.line_of(cmd.pos),
                         f"{cmd.name}() (forwards {fwd.producer} from line {fwd.line})",
                         cmd.text)
        return None

    def report(self, taint, variable, consumer_pos, consumer):
        # One finding per producer site: the first consumer reached is evidence
        # enough, and the fix is at the producer either way.
        if taint.line in self._seen:
            return
        self._seen.add(taint.line)
        self.findings.append(Finding(self.path, taint.line, taint.producer, taint.command,
                                     self.function, variable, self.line_of(consumer_pos),
                                     consumer))

    # -- pipeline-level --------------------------------------------------

    def unguarded_source(self, pipeline, tainted):
        """The taint flowing out of a pipeline's stdout, or None.

        A native/forwarding producer, or an echo/here-string of a tainted
        variable, followed by no CR guard and only pass-through filters."""
        cmds = pipeline.commands
        for i, cmd in enumerate(cmds):
            src = self.producer_of(cmd)
            if src is None:
                src = self.tainted_ref_source(cmd, tainted)
            if src is None:
                continue
            for later in cmds[i + 1:]:
                if _is_guard(later):
                    return None
                if later.name.rsplit("/", 1)[-1] not in _PASS_THROUGH:
                    return None
                if later.name == "grep" and any(w.text in ("-q", "-c", "-l", "-L")
                                                for w in later.words):
                    return None
            return src
        return None

    def tainted_ref_source(self, cmd, tainted):
        """An echo/printf of a tainted variable, or any command fed one via
        `<<<` — the value passes through on stdout with its CR intact."""
        if cmd.name in _ECHOERS:
            for w in cmd.words:
                for r in w.refs:
                    if r in tainted:
                        return tainted[r]
        words = cmd.words
        for k, w in enumerate(words):
            if w.text.startswith("<<<"):
                operand = words[k + 1] if w.text == "<<<" and k + 1 < len(words) else w
                src = self.word_source(operand, tainted)
                if src:
                    return src
            for s in w.subs:
                if s.kind == "<(":
                    src = self.sub_source(s, tainted)
                    if src:
                        return src
        return None

    def sub_source(self, sub, tainted):
        for p in sub.statements:
            src = self.unguarded_source(p, tainted)
            if src:
                return src
        return None

    def word_source(self, word, tainted):
        """Taint carried by a word: a tainted expansion or an unguarded sub."""
        for r in word.refs:
            if r in tainted:
                return tainted[r]
        for s in word.subs:
            if s.kind in ("$(", "`"):
                src = self.sub_source(s, tainted)
                if src:
                    return src
        return None

    # -- scope walk ------------------------------------------------------

    def walk(self, pipelines, tainted, loop_stack, top_level):
        """Walk statements in textual order. `top_level` marks statements
        whose stdout is the enclosing function's stdout (for forwarding)."""
        forwards = None
        for p in pipelines:
            cmds = p.commands
            first = cmds[0].first
            # Direct pipe: producer … | reader, with no guard between.
            for i, cmd in enumerate(cmds):
                src = self.producer_of(cmd) or self.tainted_ref_source(cmd, tainted)
                if src is None:
                    continue
                for later in cmds[i + 1:]:
                    if _is_guard(later):
                        break
                    if _is_reader(later):
                        self.report(src, None, later.pos, f"`{later.name}` via pipe")
                        break
            # Nested substitutions carry their own statements.
            for cmd in cmds:
                for w in cmd.words:
                    for s in w.subs:
                        self.walk(s.statements, dict(tainted), [], False)
            for cmd in cmds:
                self.consume(cmd, tainted, loop_stack)
            # Loop bookkeeping.
            if first in ("while", "until"):
                loop_stack.append("reader" if _is_reader(cmds[0]) else "other")
            elif first == "for":
                loop_stack.append("for")
            elif first == "done" and loop_stack:
                loop_stack.pop()
            # Assignments (outer shell only — a sub's assignments are its own).
            for cmd in cmds:
                self.assign(cmd, tainted)
            # Forwarding: an uncaptured producer pipeline at a function's top
            # level puts CR-terminated lines on the function's stdout.
            if top_level and forwards is None and first not in ("if", "elif", "while", "until"):
                src = self.unguarded_source(p, tainted)
                if src and not any(_is_reader(c) for c in cmds):
                    forwards = src
        return forwards

    def consume(self, cmd, tainted, loop_stack):
        words = cmd.words
        name = cmd.name
        texts = [w.text for w in words]
        # Reader fed by a here-string, process substitution, or a `done`
        # closing a `while read` / `for` loop.
        feeds_reader = _is_reader(cmd) or cmd.first == "for"
        if cmd.first == "done" and loop_stack and loop_stack[-1] in ("reader", "for"):
            feeds_reader = True
        if feeds_reader:
            for k, w in enumerate(words):
                if w.text.startswith("<<<"):
                    operand = words[k + 1] if w.text == "<<<" and k + 1 < len(words) else w
                    src = self.word_source(operand, tainted)
                    if src:
                        self.report(src, self._var(operand, tainted), operand.pos,
                                    f"`{cmd.first}` here-string")
                for s in w.subs:
                    if s.kind == "<(":
                        src = self.sub_source(s, tainted)
                        if src:
                            self.report(src, None, s.pos, f"`{cmd.first}` process substitution")
            if cmd.first == "for" and "in" in texts:
                for w in words[texts.index("in") + 1:]:
                    src = self.word_source(w, tainted)
                    if src:
                        self.report(src, self._var(w, tainted), w.pos, "`for … in` word split")
        # String tests: `[[ "$v" == x ]]`, `[ "$v" = x ]`, `test -n "$v"`.
        if name in ("[[", "[", "test"):
            for k, w in enumerate(words):
                src = self.word_source(w, tainted)
                if not src:
                    continue
                prev = texts[k - 1] if k > 0 else ""
                nxt = texts[k + 1] if k + 1 < len(texts) else ""
                op = prev if prev in _STRING_OPS else nxt if nxt in _STRING_OPS else None
                if op:
                    self.report(src, self._var(w, tainted), w.pos, f"string test `{op}`")
        if cmd.first == "case":
            for w in words[1:]:
                src = self.word_source(w, tainted)
                if src:
                    self.report(src, self._var(w, tainted), w.pos, "`case` match")
        # Label arguments: `--add-label "$v"`, `--remove-label="$v"`.
        for k, w in enumerate(words):
            t = w.text
            flag = None
            target = None
            if t in _LABEL_FLAGS and k + 1 < len(words):
                flag, target = t, words[k + 1]
            else:
                for f in _LABEL_FLAGS:
                    if t.startswith(f + "="):
                        flag, target = f, w
                        break
            if target is None:
                continue
            src = self.word_source(target, tainted)
            if src:
                self.report(src, self._var(target, tainted), target.pos, f"label argument `{flag}`")

    def _var(self, word, tainted):
        for r in word.refs:
            if r in tainted:
                return r
        return None

    def assign(self, cmd, tainted):
        words = cmd.words
        if cmd.first in _ASSIGN_KEYWORDS:
            candidates = words[1:]
        else:
            candidates = []
            for w in words:
                if not _ASSIGN_RE.match(w.text):
                    break
                candidates.append(w)
        for w in candidates:
            m = _ASSIGN_RE.match(w.text)
            if m:
                src = self.word_source(w, tainted)
                if src:
                    tainted[m.group(1)] = src
                elif not w.text[:m.end()].endswith("+="):
                    tainted.pop(m.group(1), None)
            elif cmd.first in _ASSIGN_KEYWORDS and _IDENT_RE.fullmatch(w.text):
                tainted.pop(w.text, None)   # bare `local v` — a fresh, clean local


def _function_ranges(pipelines):
    """Yield (name, body_pipelines) for every `name() { … }` / `function name`
    definition, plus ("", top_level_pipelines) for statements outside any."""
    top = []
    funcs = []
    i = 0
    n = len(pipelines)
    while i < n:
        p = pipelines[i]
        w0 = p.commands[0].words[0]
        name = None
        if w0.funcdef:
            name = w0.text[:-2]
        elif w0.text == "function" and len(p.commands[0].words) > 1:
            name = p.commands[0].words[1].text.rstrip("()")
        if name is None:
            top.append(p)
            i += 1
            continue
        # Body runs from the `{` to its matching `}` at brace depth 0.
        depth = 0
        body = []
        j = i
        started = False
        while j < n:
            for c in pipelines[j].commands:
                for w in c.words:
                    if w.text == "{":
                        depth += 1
                        started = True
                    elif w.text == "}":
                        depth -= 1
            if j > i or started:
                body.append(pipelines[j])
            j += 1
            if started and depth <= 0:
                break
        funcs.append((name, body))
        i = j
    return funcs, top


def analyze_text(path, text):
    """Return the findings for one Bash source."""
    pipelines = split_statements(lex_file(text))
    funcs, top = _function_ranges(pipelines)
    forwarding = {}
    # Fixpoint over same-file forwarding: a function whose top-level stdout is
    # an unguarded producer becomes a producer for its callers.
    for _ in range(4):
        changed = False
        for name, body in funcs:
            if name in forwarding:
                continue
            probe = _Analyzer(path, text, forwarding)
            probe.function = name
            fwd = probe.walk(body, {}, [], True)
            if fwd:
                forwarding[name] = fwd
                changed = True
        if not changed:
            break
    analyzer = _Analyzer(path, text, forwarding)
    for name, body in funcs:
        analyzer.function = name
        analyzer.walk(body, {}, [], True)
    analyzer.function = None
    analyzer.walk(top, {}, [], False)
    analyzer.findings.sort(key=lambda f: f.line)
    return analyzer.findings


def _is_bash(path):
    if path.suffix in (".sh", ".bash"):
        return True
    if path.suffix:
        return False
    try:
        with open(path, "rb") as fh:
            head = fh.readline(200)
    except OSError:
        return False
    return bool(_SHEBANG_RE.match(head.decode("utf-8", "replace")))


def iter_files(roots):
    """Yield Bash sources under each root (a file root is taken as given)."""
    for root in roots:
        root = Path(root)
        if root.is_file():
            if _is_bash(root):
                yield root
            continue
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file() or any(part in _SKIP_DIRS for part in path.parts):
                continue
            if _is_bash(path):
                yield path


def _display(path):
    # POSIX `file:line` refs, relative to the cwd when the file is under it.
    try:
        return path.resolve().relative_to(Path.cwd().resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def scan(roots):
    """Return (findings, swept_count) across `roots`."""
    findings = []
    swept = 0
    for path in iter_files(roots):
        if path.name in EXEMPT_FILES:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        swept += 1
        findings.extend(analyze_text(_display(path), text))
    return findings, swept


def main(argv):
    roots = argv[1:] or [str(Path(__file__).resolve().parent)]
    for r in roots:
        if r.startswith("-"):
            print(f"usage: {Path(argv[0]).name} [<file-or-dir>...]", file=sys.stderr)
            return 2
    findings, swept = scan(roots)
    for f in findings:
        print(f.render())
    print(f"-- swept {swept} Bash file(s) under {', '.join(roots)}", file=sys.stderr)
    if swept == 0:
        print("lint_crlf_producers: scope resolved to zero Bash files", file=sys.stderr)
        return 2
    if findings:
        print(f"\n{len(findings)} unguarded native producer(s) feeding a Bash text consumer.",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
