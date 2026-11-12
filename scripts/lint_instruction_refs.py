#!/usr/bin/env python3
"""Ratchet: an agent-facing instruction file cites only paths, labels and
components that exist.

`docs/agents/CLAUDE-BASELINE.md` §"What belongs in agent-facing docs" requires
every citation to resolve and every cited name to exist. An agent that follows
a dead path or applies a retired label loses a turn or acts on a stale rule,
and a reference dies when code is renamed, so this runs on every pull request
with no path filter: the renaming change is the one that fails.

Population: the instruction files `lint_instruction_size.instruction_class`
selects (every `CLAUDE.md` and `AGENTS.md`, `docs/agents/**`, and the rule,
role, subagent and skill files under `.claude/`). The file set and every
existence check come from `git ls-files` (tracked plus untracked-unignored
files, so a new file counts before it is staged), never a directory walk,
which would descend into gitignored worktree checkouts and build trees.

Four kinds of reference, each reported as `file:line: <kind> '<token>' ...`:

- `path`: a relative Markdown link target (inline, image or reference
  definition), or a whitespace-separated word of an inline code span that
  begins with a tracked top-level directory name. It resolves when it names a
  tracked file or directory relative to the citing file's directory or to the
  repository root, or a gitignored path (cited on purpose, untracked by
  design); an inline code word also resolves when it names a tracked path
  below some directory, as a path given relative to the creation or module
  the document is about. A `#anchor`, a `?query`, a `:line` or `::Symbol`
  suffix and trailing punctuation are stripped first. A word holding a
  placeholder or pattern character (`PLACEHOLDER_CHARS`, or `...`) is not a
  path, nor is a word ending in `:` (a commit scope such as `engine/voxel:`).
- `label`: a `fleet:` or `human:` token anywhere in the file, fenced blocks
  included. It resolves when it is in the `LABELS` catalog of
  `scripts/fleet/fleet-labels`, is a node of `docs/agents/fleet-state-machine.json`,
  or appears in `docs/agents/fleet-labels-reference.md` outside its `## Retired`
  section; a templated name there (`fleet:claim-<host>-<agent>`) also resolves
  every concrete instance of its family, but only a family with a literal stem
  (`fleet:*` resolves nothing). A name listed under `## Retired` resolves only
  inside that section. A token holding a placeholder is skipped, and one
  ending in `-` is a prefix that resolves when a known name starts with it.
- `component`: a `C_<Name>` identifier inside an inline code span. It resolves
  when the name has a whole-word hit in a tracked non-Markdown, non-binary file
  other than this lint's own baseline and suite. A one-letter name (`C_A`) or
  a metasyntactic one (`C_Foo...`) is a placeholder and is skipped.
- `command`: a fenced command-position `fleet-*` token in a nested `CLAUDE.md`
  or an `AGENTS.md` that `scripts/fleet/lint_rules_commands.py` does not read,
  judged by that lint's own predicate (`find_candidates` / `resolve`, its
  `rules-cmd-ok` marker included).

Suppress a deliberate citation with `<!-- lint: ref-ok <token> -- <reason> -->`
on the line before it (blank lines may separate them); above a fence it covers
the whole fenced block.

Baseline: `lint_instruction_refs_baseline.json` maps a file to the sorted list
of its tokens that are dead and accepted. An unresolved token that is not
listed fails; a listed token that now resolves or is no longer cited fails as
stale; `--update-baseline` only removes stale entries. Adding an entry is a
hand edit, so it shows in the PR diff.

Exit 0: no finding. Exit 1: at least one, each on its own line; the summary
names the two ways out. `--against <ref>` also scans `ref`'s tree with the
baseline committed there and splits the findings, one `(file, token)` at a
time through `ratchet_against.split`, into those this tree introduced and
those it inherited; it exits 1 only on an introduced one. Exit 2: `ref`
cannot be read. Gitignored paths are judged by the working tree's ignore
rules on both sides.
"""
import argparse
import json
import posixpath
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

import lint_instruction_size
import ratchet_against

sys.path.insert(0, str(Path(__file__).resolve().parent / "fleet"))
import lint_rules_commands  # noqa: E402

REPO = Path(__file__).resolve().parents[1]
BASELINE_REL = "scripts/lint_instruction_refs_baseline.json"
SUITE_REL = "scripts/fleet/tests/test_lint_instruction_refs.py"
LABEL_CATALOG_REL = "scripts/fleet/fleet-labels"
STATE_MACHINE_REL = "docs/agents/fleet-state-machine.json"
LABEL_REFERENCE_REL = "docs/agents/fleet-labels-reference.md"
RETIRED_HEADING = "Retired"

PLACEHOLDER_CHARS = set("<>*{}$…[]()|\\^?+")
BINARY_SUFFIXES = {
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".tga", ".webp", ".ttf",
    ".otf", ".wav", ".mp3", ".ogg", ".flac", ".mp4", ".mov", ".zip", ".gz",
    ".a", ".so", ".dylib", ".dll", ".exe", ".bin", ".vox", ".mid", ".pdf",
}

FENCE_RE = re.compile(r"^\s*(```|~~~)")
SPAN_RE = re.compile(r"(`+)(.+?)\1")
LINK_RE = re.compile(r"!?\[[^\]\n]*\]\(\s*<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\s*\)")
REF_DEF_RE = re.compile(r"^\s{0,3}\[[^\]\n]+\]:\s+<?([^\s>]+)>?")
HEADING_RE = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
SUPPRESS_RE = re.compile(r"<!--\s*lint:\s*ref-ok\s+(\S+)")
LABEL_RE = re.compile(r"(?<![\w-])(?:fleet|human):[^\s`'\"),;|]+")
LABEL_SHAPE_RE = re.compile(r"^(?:fleet|human):[a-z0-9][a-z0-9.-]*$")
# A template names a label family: a literal stem ending in `-` before its
# first placeholder, so a namespace-wide mention (`fleet:*`) resolves nothing.
TEMPLATE_RE = re.compile(r"(?:fleet|human):[a-z0-9.]+(?:-[a-z0-9.]+)*-(?:[a-z0-9.-]|<[^<>]*>|\*)+")
CATALOG_ENTRY_RE = re.compile(r"^\s*\"((?:fleet|human):[^|\"]+)\|", re.M)
COMPONENT_RE = re.compile(r"\bC_[A-Z][A-Za-z0-9_]*\b")
PLACEHOLDER_COMPONENT_RE = re.compile(r"^C_(?:[A-Z]|(?:Foo|Bar|Baz|Qux)(?:[A-Z_].*)?)$")
COMPONENT_BYTES_RE = re.compile(rb"\bC_[A-Z][A-Za-z0-9_]*\b")
LINE_SUFFIX_RE = re.compile(r"(?::\d+(?:[-:]\d+)*|::.*)$")
URL_RE = re.compile(r"^[a-zA-Z][a-zA-Z0-9+.-]*:")

FIX_ADVICE = (
    "Fix the citation to name what exists now — the change that renamed or removed the "
    "target is the one to update the document — or, for a reference that is dead on "
    "purpose, add `<!-- lint: ref-ok <token> -- <reason> -->` on the line before it, or "
    f"add the token under its file in {BASELINE_REL} by hand so the addition shows in the "
    "PR diff; `--update-baseline` only removes."
)


class Tree:
    """The files one scan reads: `paths` (every tracked path, symlinks
    included), `symlinks`, `dirs` (every directory holding a path), and
    `read(rel)`."""

    def __init__(self, paths, symlinks, reader):
        self.paths = set(paths)
        self.symlinks = set(symlinks)
        self.dirs = {posixpath.dirname(p) for p in self.paths}
        for d in list(self.dirs):
            while d:
                d = posixpath.dirname(d)
                self.dirs.add(d)
        self.dirs.discard("")
        self.top_dirs = {p.split("/", 1)[0] for p in self.paths if "/" in p}
        self._read = reader
        self._suffixes = None

    def exists(self, rel):
        return rel in self.paths or rel in self.dirs

    def exists_under_some_dir(self, rel):
        """Whether `rel` names a tracked path below some directory: a path a
        document gives relative to a creation or module it is talking about."""
        if self._suffixes is None:
            self._suffixes = set()
            for p in self.paths | self.dirs:
                parts = p.split("/")
                self._suffixes.update("/".join(parts[i:]) for i in range(1, len(parts)))
        return rel in self._suffixes

    def read(self, rel):
        """Text of `rel`, or None when it cannot be read."""
        data = self._read(rel)
        return None if data is None else ratchet_against.decode(data)

    def read_bytes(self, rel):
        return self._read(rel)


def _git(root, *args):
    return subprocess.run(["git", "-C", str(root), *args], capture_output=True,
                          check=True).stdout


def worktree_tree(root):
    out = _git(root, "ls-files", "-z", "-co", "--exclude-standard").decode()
    paths = [p for p in out.split("\0") if p and (root / p).exists()]
    links = {p for p in paths if (root / p).is_symlink()}

    def reader(rel):
        if rel in links:
            return None
        try:
            return (root / rel).read_bytes()
        except OSError:
            return None

    return Tree(paths, links, reader)


def ref_tree(root, ref):
    """The tree at `ref`; raises `ratchet_against.RefError`."""
    regular = ratchet_against.ref_files(root, ref)
    try:
        names = _git(root, "ls-tree", "-r", "-z", "--full-tree", "--name-only", ref).decode()
    except subprocess.CalledProcessError as e:
        raise ratchet_against.RefError(e.stderr.decode(errors="replace").strip()) from e
    paths = [p for p in names.split("\0") if p]
    blobs = ratchet_against.read_blobs(root, [oid for rel, oid in regular.items()
                                              if _wanted_blob(rel)])
    return Tree(paths, set(paths) - set(regular), lambda rel: blobs.get(regular.get(rel)))


def _wanted_blob(rel):
    """Whether a scan ever reads `rel`: an instruction file, a label source,
    or a component source."""
    return (lint_instruction_size.instruction_class(rel, on_disk=False) is not None
            or rel in (LABEL_CATALOG_REL, STATE_MACHINE_REL, LABEL_REFERENCE_REL)
            or _component_source(rel))


def _component_source(rel):
    return (not rel.endswith(".md") and rel not in (BASELINE_REL, SUITE_REL)
            and Path(rel).suffix.lower() not in BINARY_SUFFIXES)


def population(tree):
    return sorted(rel for rel in tree.paths
                  if rel not in tree.symlinks
                  and lint_instruction_size.instruction_class(rel, on_disk=False) is not None)


def has_placeholder(token):
    return "..." in token or any(c in PLACEHOLDER_CHARS for c in token)


# ---------------------------------------------------------------- labels

class LabelIndex:
    def __init__(self, known, templates, retired):
        self.known = known - retired
        self.templates = [re.compile(t) for t in sorted(templates)]
        self.retired = retired

    def resolves(self, token, in_retired_section=False):
        if token in self.retired:
            return in_retired_section
        if token in self.known:
            return True
        if token.endswith("-"):
            return any(name.startswith(token) for name in self.known) or any(
                t.pattern.startswith(re.escape(token)) for t in self.templates)
        return any(t.fullmatch(token) for t in self.templates)


def _template_regex(token):
    parts = re.split(r"(<[^>]*>|\*)", token)
    return "".join("[a-z0-9][a-z0-9.-]*" if p.startswith("<") else
                   "[a-z0-9.-]*" if p == "*" else re.escape(p) for p in parts if p)


def label_tokens(line):
    """`(token, is_concrete)` for each label-shaped token on `line`."""
    out = []
    for m in LABEL_RE.finditer(line):
        token = m.group(0).rstrip(".:")
        if has_placeholder(token):
            out.append((token, False))
        elif LABEL_SHAPE_RE.match(token):
            out.append((token, True))
    return out


def build_label_index(tree):
    known, templates, retired = set(), set(), set()
    catalog = tree.read(LABEL_CATALOG_REL) or ""
    labels_block = catalog.split("LABELS=(", 1)[-1].split("\n)", 1)[0]
    known.update(CATALOG_ENTRY_RE.findall(labels_block))
    machine = tree.read(STATE_MACHINE_REL)
    if machine:
        known.update(entry["name"] for entry in json.loads(machine).get("labels", []))
    reference = tree.read(LABEL_REFERENCE_REL) or ""
    for section, line in _sections(reference.splitlines()):
        for token, concrete in label_tokens(line):
            if section == RETIRED_HEADING:
                if concrete:
                    retired.add(token)
            elif concrete:
                known.add(token)
            elif TEMPLATE_RE.fullmatch(token):
                templates.add(_template_regex(token))
    return LabelIndex(known, templates, retired)


def _sections(lines):
    """`(level-2 heading text or None, line)` for each line."""
    section = None
    for line in lines:
        m = HEADING_RE.match(line)
        if m and len(m.group(1)) == 2:
            section = m.group(2)
        yield section, line


# ---------------------------------------------------------------- scan

def component_names(tree):
    names = set()
    for rel in tree.paths:
        if rel in tree.symlinks or not _component_source(rel):
            continue
        data = tree.read_bytes(rel)
        if data and b"C_" in data:
            names.update(m.decode() for m in COMPONENT_BYTES_RE.findall(data))
    return names


def _path_candidates(target):
    """The cleaned path a citation names, or None when it is not a relative
    repository path."""
    target = target.strip().strip("\"'")
    if not target or target.startswith(("#", "/", "~")) or URL_RE.match(target):
        return None
    target = target.split("#", 1)[0].split("?", 1)[0]
    target = LINE_SUFFIX_RE.sub("", target.rstrip(".,;:)!"))
    if not target or has_placeholder(target):
        return None
    return target


def path_resolves(tree, rel, target, ignored, inline):
    base = posixpath.dirname(rel)
    for cand in (posixpath.normpath(posixpath.join(base, target)),
                 posixpath.normpath(target)):
        if cand.startswith("../") or cand == "..":
            continue
        if tree.exists(cand.rstrip("/")) or cand == ".":
            return True
    target = posixpath.normpath(target)
    return ignored(target) or (inline and tree.exists_under_some_dir(target))


def scan_file(tree, rel, labels, components, ignored, command_names):
    """Yield `(lineno, kind, token, resolved)` for every reference in `rel`
    that is not suppressed."""
    text = tree.read(rel)
    if text is None:
        return
    lines = text.splitlines()
    in_fence = False
    pending, fence_allow = set(), set()
    is_reference = rel == LABEL_REFERENCE_REL
    for idx, (section, line) in enumerate(_sections(lines)):
        lineno = idx + 1
        marker = SUPPRESS_RE.findall(line)
        if marker:
            pending |= {t.rstrip(":,") for t in marker}
            continue
        if FENCE_RE.match(line):
            in_fence = not in_fence
            fence_allow = pending if in_fence else set()
            pending = set()
            continue
        if not line.strip():
            continue
        allow = pending | fence_allow
        pending = set()
        in_retired = is_reference and section == RETIRED_HEADING
        for token, concrete in label_tokens(line):
            if concrete and token not in allow:
                yield lineno, "label", token, labels.resolves(token, in_retired)
        if in_fence:
            continue
        spans = [m.group(2).strip() for m in SPAN_RE.finditer(line)]
        prose = SPAN_RE.sub(" ", line)
        targets = [(t, False) for t in LINK_RE.findall(prose) + REF_DEF_RE.findall(prose)]
        for span in spans:
            targets += [(w, True) for w in span.split()
                        if "/" in w and w.split("/", 1)[0] in tree.top_dirs
                        and not w.endswith(":")]
            for name in COMPONENT_RE.findall(span):
                if name not in allow and not PLACEHOLDER_COMPONENT_RE.match(name):
                    yield lineno, "component", name, name in components
        for raw, inline in targets:
            target = _path_candidates(raw)
            if target and target not in allow and raw not in allow:
                yield lineno, "path", target, path_resolves(tree, rel, target, ignored,
                                                            inline)
    if command_names is not None:
        for lineno, token, allowed in lint_rules_commands.find_candidates(None, text=text):
            if not allowed and token not in _suppressed_at(lines, lineno):
                yield lineno, "command", token, lint_rules_commands.resolve(
                    token, *command_names)


def _suppressed_at(lines, lineno):
    """Tokens a `ref-ok` marker names above the fence holding `lineno`."""
    idx = lineno - 1
    while idx > 0 and not FENCE_RE.match(lines[idx]):
        idx -= 1
    idx -= 1
    while idx >= 0 and not lines[idx].strip():
        idx -= 1
    return {t.rstrip(":,") for t in SUPPRESS_RE.findall(lines[idx])} if idx >= 0 else set()


def commands_population(rel):
    """Whether the command arm reads `rel`: a `CLAUDE.md` or `AGENTS.md` the
    command lint's own globs miss."""
    if Path(rel).name not in ("CLAUDE.md", "AGENTS.md"):
        return False
    return not any(Path(rel).match(g) or (g.endswith("/**/*.md") and
                                          rel.startswith(g[:-len("**/*.md")]))
                   for g in lint_rules_commands.FORBIDDEN_DOC_GLOBS)


def scan(tree, ignored):
    """`{file: [(lineno, kind, token, resolved)]}` over the population."""
    labels = build_label_index(tree)
    components = component_names(tree)
    command_names = (
        {posixpath.basename(p) for p in tree.paths},
        {Path(p).stem for p in tree.paths},
    )
    refs = {}
    for rel in population(tree):
        refs[rel] = list(scan_file(tree, rel, labels, components, ignored,
                                   command_names if commands_population(rel) else None))
    return refs


def findings(refs, baseline):
    """`(dead, stale)`: `dead` lists `(file, lineno, kind, token)` for each
    unlisted unresolved citation; `stale` lists `(file, token, why)` for each
    baseline entry that resolves or is no longer cited."""
    dead, stale = [], []
    for rel, items in sorted(refs.items()):
        listed = set(baseline.get(rel, []))
        cited, unresolved = set(), set()
        for lineno, kind, token, resolved in items:
            cited.add(token)
            if not resolved:
                unresolved.add(token)
                if token not in listed:
                    dead.append((rel, lineno, kind, token))
        for token in sorted(listed):
            if token not in cited:
                stale.append((rel, token, "no longer cited"))
            elif token not in unresolved:
                stale.append((rel, token, "now resolves"))
    for rel in sorted(set(baseline) - set(refs)):
        for token in baseline[rel]:
            stale.append((rel, token, "file is no longer in the population"))
    return dead, stale


def finding_keys(dead, stale):
    keys = {(rel, token) for rel, _, _, token in dead}
    keys |= {(rel, token) for rel, token, _ in stale}
    return keys


def print_dead(dead):
    for rel, lineno, kind, token in dead:
        print(f"{rel}:{lineno}: {kind} '{token}' does not resolve")


def print_stale(stale):
    for rel, token, why in stale:
        print(f"{rel}: baseline entry '{token}' is stale ({why})")


# ---------------------------------------------------------------- driver

def make_ignored(root):
    cache = {}

    def ignored(rel):
        if rel not in cache:
            r = subprocess.run(["git", "-C", str(root), "check-ignore", "-q", "--no-index",
                                "--", rel], capture_output=True)
            cache[rel] = r.returncode == 0
        return cache[rel]
    return ignored


def load_baseline(text):
    return json.loads(text) if text else {}


def write_baseline(path, baseline):
    path.write_text(json.dumps({rel: sorted(tokens) for rel, tokens in sorted(baseline.items())
                                if tokens}, indent=1) + "\n")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--root", type=Path, default=REPO, help=argparse.SUPPRESS)
    ap.add_argument("--update-baseline", action="store_true",
                    help="drop baseline entries that now resolve or are no longer cited; "
                         "never adds one")
    ap.add_argument("--against", metavar="REF",
                    help="split the findings into those introduced relative to REF's tree "
                         "and those inherited from it; exit 1 only on an introduced one")
    args = ap.parse_args(argv)
    if args.against and args.update_baseline:
        ap.error("--against does not combine with --update-baseline")
    root = args.root.resolve()
    baseline_path = root / BASELINE_REL
    ignored = make_ignored(root)

    tree = worktree_tree(root)
    baseline = load_baseline(baseline_path.read_text() if baseline_path.exists() else "")
    refs = scan(tree, ignored)
    dead, stale = findings(refs, baseline)

    if args.update_baseline:
        drop = defaultdict(set)
        for rel, token, _ in stale:
            drop[rel].add(token)
        write_baseline(baseline_path, {rel: set(tokens) - drop[rel]
                                       for rel, tokens in baseline.items()})
        print(f"baseline updated: {len(stale)} stale entr(y/ies) removed")
        print_dead(dead)
        return 1 if dead else 0

    if args.against:
        try:
            base_tree = ref_tree(root, args.against)
            base_baseline = load_baseline(
                ratchet_against.ref_text(root, args.against, BASELINE_REL))
        except (ratchet_against.RefError, ValueError) as e:
            print(f"cannot measure {args.against}: {e}", file=sys.stderr)
            return 2
        base_keys = finding_keys(*findings(scan(base_tree, ignored), base_baseline))
        introduced, inherited = ratchet_against.split(
            {k: (1, 0) for k in finding_keys(dead, stale)},
            {k: (1, 0) for k in base_keys})
        if introduced or inherited:
            new = set(introduced)
            for header, keep in (("introduced:", True), ("inherited:", False)):
                print(header)
                print_dead([d for d in dead if ((d[0], d[3]) in new) == keep])
                print_stale([s for s in stale if ((s[0], s[1]) in new) == keep])
            if introduced:
                print(f"\n{len(introduced)} dead reference(s) or stale baseline entr(y/ies) "
                      f"beyond what {args.against} already carries. {FIX_ADVICE}",
                      file=sys.stderr)
                return 1
            print(f"\n{len(inherited)} inherited finding(s): present on {args.against} "
                  "already, so not this change's to fix.", file=sys.stderr)
            return 0

    print_dead(dead)
    print_stale(stale)
    if dead or stale:
        print(f"\n{len(dead)} dead reference(s), {len(stale)} stale baseline entr(y/ies). "
              f"{FIX_ADVICE} A stale entry is removed with `--update-baseline`.",
              file=sys.stderr)
        return 1
    total = sum(len(v) for v in refs.values())
    accepted = sum(len(v) for v in baseline.values())
    print(f"ok: {len(refs)} instruction file(s), {total} reference(s), {accepted} accepted "
          "dead reference(s) in the baseline, none new")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
