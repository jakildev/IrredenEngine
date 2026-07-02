"""Role-sharing contract validator (#1667).

Enforces the role-wrapper pattern introduced in ``docs/design/role-sharing.md``:
every ``docs/agents/*-protocol.md`` that declares a ``## Repo deltas this flow
needs`` table must have, in each fleet-enabled repo root, a
``.claude/commands/role-*.md`` whose body references the protocol filename AND
whose ``## Deltas`` table answers every bold ``**key**`` the protocol declares.

Severity split (mirrors fleet_validate_stack):
  error   — unambiguous violation (missing required delta key in a present wrapper)
  warn    — ambiguous (extra key not in protocol; no wrapper in a present repo)

``--strict`` in the CLI promotes warns to errors.

Alias map: ``DELTA_KEY_ALIASES`` maps protocol-key-name → list of accepted
alternative wrapper key names. Used for legacy wrapper key names that haven't
been renamed yet; lint accepts any alias as equivalent to the canonical key.

Pure file I/O — no subprocess / gh calls.

Source of truth: scripts/fleet/fleet_validate_roles.py in the engine repo.
Installed to ~/bin/fleet-validate-roles by scripts/fleet/install.sh.
"""
import re
from pathlib import Path

ERROR = "error"
WARN = "warn"

# Maps canonical protocol key name → list of accepted wrapper key aliases.
# Add an entry here when a wrapper uses a legacy key name that hasn't been
# renamed yet (the rename itself is deferred).
DELTA_KEY_ALIASES = {}

_REPO_DELTAS_HDR_RE = re.compile(
    r"^##\s+Repo deltas this flow needs\s*$",
    re.MULTILINE,
)
_WRAPPER_DELTAS_HDR_RE = re.compile(
    r"^##\s+Deltas\b",
    re.MULTILINE,
)
# Matches `| **key-name** |` (table first-column bold key)
_DELTA_KEY_RE = re.compile(
    r"^\|\s*\*\*([A-Za-z0-9_-]+)\*\*\s*\|",
    re.MULTILINE,
)


def _norm(text):
    """Normalise CRLF/CR so ``^``/``$`` anchors work uniformly."""
    return (text or "").replace("\r\n", "\n").replace("\r", "\n")


def _section_body(text, header_re):
    """Return the text between the first match of header_re and the next ``##``."""
    m = header_re.search(text)
    if not m:
        return ""
    start = m.end()
    nxt = re.search(r"^##\s", text[start:], re.MULTILINE)
    if nxt:
        return text[start : start + nxt.start()]
    return text[start:]


def find_protocols(repo_root):
    """Return sorted *-protocol.md files under docs/agents/ that declare Repo deltas.

    Files without ``## Repo deltas this flow needs`` are naturally exempt.
    """
    protocols_dir = Path(repo_root) / "docs" / "agents"
    if not protocols_dir.is_dir():
        return []
    results = []
    for f in sorted(protocols_dir.glob("*-protocol.md")):
        try:
            text = _norm(f.read_text(encoding="utf-8", errors="replace"))
        except OSError:
            continue
        if _REPO_DELTAS_HDR_RE.search(text):
            results.append(f)
    return results


def _extract_keys(path, header_re):
    try:
        text = _norm(Path(path).read_text(encoding="utf-8", errors="replace"))
    except OSError:
        return []
    return _DELTA_KEY_RE.findall(_section_body(text, header_re))


def extract_protocol_keys(protocol_path):
    """Return the ordered list of delta key names declared in the protocol."""
    return _extract_keys(protocol_path, _REPO_DELTAS_HDR_RE)


def find_wrappers(repo_root, protocol_filename):
    """Return .claude/commands/role-*.md files that WRAP protocol_filename.

    A wrapper both references the protocol filename and carries a ``## Deltas``
    section — the second condition is what separates a wrapper from a role doc
    that merely *cites* the protocol in prose (e.g. role-opus-reviewer.md points
    at architect-protocol.md §"plan reviewer"; validating it against the
    architect's 10 delta keys produced 10 false errors). A citing-only file is
    ignored here; a protocol with no true wrapper in the repo still surfaces via
    the caller's no-wrapper WARN, which also covers the half-written-wrapper
    case (referenced the protocol, never added its Deltas table).
    """
    cmds_dir = Path(repo_root) / ".claude" / "commands"
    if not cmds_dir.is_dir():
        return []
    basename = Path(protocol_filename).name
    results = []
    for f in sorted(cmds_dir.glob("role-*.md")):
        try:
            text = _norm(f.read_text(encoding="utf-8", errors="replace"))
        except OSError:
            continue
        if basename in text and _WRAPPER_DELTAS_HDR_RE.search(text):
            results.append(f)
    return results


def extract_wrapper_keys(wrapper_path):
    """Return the ordered list of delta key names in the wrapper's ## Deltas section."""
    return _extract_keys(wrapper_path, _WRAPPER_DELTAS_HDR_RE)


def validate_wrapper(protocol_keys, wrapper_path, aliases=None):
    """Validate a single wrapper against the protocol's delta keys.

    Returns a list of ``{"severity": str, "msg": str}`` findings.
    """
    if aliases is None:
        aliases = DELTA_KEY_ALIASES
    wrapper_keys = extract_wrapper_keys(wrapper_path)
    wrapper_set = set(wrapper_keys)
    alias_targets = {alias: canon for canon, alts in aliases.items() for alias in alts}

    findings = []
    for key in protocol_keys:
        accepted = {key} | set(aliases.get(key, []))
        if not accepted & wrapper_set:
            findings.append({"severity": ERROR, "msg": "missing delta key `**%s**`" % key})

    proto_set = set(protocol_keys)
    for key in wrapper_keys:
        if key in proto_set or key in alias_targets:
            continue
        findings.append(
            {"severity": WARN,
             "msg": "extra delta key `**%s**` not declared in protocol" % key}
        )

    return findings


def validate_roles(repo_roots, aliases=None):
    """Validate the role-sharing contract across repo_roots.

    Parameters
    ----------
    repo_roots
        List of ``(absolute_path, label)`` tuples.  The first entry is the
        canonical repo (where ``docs/agents/`` lives).  Subsequent entries are
        downstream repos that should also have wrappers.
    aliases
        Override the module-level DELTA_KEY_ALIASES map.

    Returns a dict::

        {
          "ok": bool,
          "n_errors": int,
          "n_warnings": int,
          "empty": bool,
          "protocols": [
            {
              "path": str, "name": str, "keys": [str],
              "repos": [
                {
                  "label": str, "no_wrapper": bool,
                  "wrappers": [{"path", "findings", "n_errors", "n_warnings", "ok"}],
                  "findings": [{"severity", "msg"}],
                }
              ],
            }
          ],
        }
    """
    if aliases is None:
        aliases = DELTA_KEY_ALIASES

    if not repo_roots:
        return {"ok": True, "empty": True, "n_errors": 0, "n_warnings": 0, "protocols": []}

    canonical_root = repo_roots[0][0]
    protocols = find_protocols(canonical_root)

    if not protocols:
        return {"ok": True, "empty": True, "n_errors": 0, "n_warnings": 0, "protocols": []}

    total_errors = total_warnings = 0
    protocol_results = []

    for proto_path in protocols:
        protocol_keys = extract_protocol_keys(proto_path)
        proto_name = proto_path.name
        repo_results = []

        for root, label in repo_roots:
            root_path = Path(root)
            if not root_path.is_dir():
                continue

            wrappers = find_wrappers(root, proto_name)

            if not wrappers:
                total_warnings += 1
                repo_results.append({
                    "label": label,
                    "no_wrapper": True,
                    "wrappers": [],
                    "findings": [
                        {
                            "severity": WARN,
                            "msg": "no role-*.md in %s/.claude/commands/ references %s"
                                   % (label, proto_name),
                        }
                    ],
                })
                continue

            wrapper_results = []
            for wp in wrappers:
                findings = validate_wrapper(protocol_keys, wp, aliases)
                e = sum(1 for f in findings if f["severity"] == ERROR)
                w = sum(1 for f in findings if f["severity"] == WARN)
                total_errors += e
                total_warnings += w
                wrapper_results.append({
                    "path": str(wp),
                    "findings": findings,
                    "n_errors": e,
                    "n_warnings": w,
                    "ok": e == 0,
                })

            repo_results.append({
                "label": label,
                "no_wrapper": False,
                "wrappers": wrapper_results,
                "findings": [],
            })

        protocol_results.append({
            "path": str(proto_path),
            "name": proto_name,
            "keys": protocol_keys,
            "repos": repo_results,
        })

    return {
        "ok": total_errors == 0,
        "empty": False,
        "n_errors": total_errors,
        "n_warnings": total_warnings,
        "protocols": protocol_results,
    }
