"""Role-sharing / skill-sharing contract validator (#1667, #2893).

Enforces the wrapper pattern shared by ``docs/design/role-sharing.md`` and
``docs/design/skill-sharing.md`` across two lanes:

  role  — every ``docs/agents/*-protocol.md`` that declares a ``## Repo
          deltas this flow needs`` table must have, in each fleet-enabled
          repo root, a ``.claude/commands/role-*.md`` that references the
          protocol filename AND whose ``## Deltas`` table answers every
          bold ``**key**`` the protocol declares.
  skill — every ``docs/agents/skills/<stem>.md`` that declares the same
          table must have a stem-paired ``.claude/skills/<stem>/SKILL.md``
          whose ``## Deltas`` table answers every declared key. Stem
          pairing (not cite-scan) is deliberate here: every conforming
          skill wrapper carries its own ``## Deltas`` table, so a cite-scan
          predicate misidentifies a wrapper that merely mentions a sibling
          flow in prose as a wrapper *of* that sibling (#2893's first plan
          revision bounced on exactly this — see the false-pairing test).

Severity split (mirrors fleet_validate_stack):
  error   — unambiguous violation (missing required delta key in a present wrapper)
  warn    — ambiguous (extra key not in protocol; no wrapper in a present repo)

``--strict`` in the CLI promotes warns to errors.

Alias map: ``DELTA_KEY_ALIASES`` maps protocol-key-name → list of accepted
alternative wrapper key names. Used for legacy wrapper key names that haven't
been renamed yet; lint accepts any alias as equivalent to the canonical key.

Baselines (skill lane only): a pre-existing, tracked violation that can't be
repaired in the same PR because its fix surface (``.claude/skills/**/SKILL.md``)
is gated self-config no worker class can push. Follows the
``header_global_baseline`` idiom in ``cmake/run_header_convention_checks.cmake``
— a pair may leave the baseline, never join it. Each entry names the issue
that owns its repair.

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

# (flow_stem, missing_key) -> owning issue. A pair may leave this baseline,
# never join it (cmake/run_header_convention_checks.cmake's
# header_global_baseline is the precedent). Skill lane only.
SKILL_WRAPPER_MISSING_KEY_BASELINE = {
    ("commit-and-push", "sha-pin token"): 2912,
}

# flow_stem -> owning issue, for flows with no conforming stem-paired
# wrapper at all. --strict promotes this WARN to an error, so it needs the
# same baseline cover as a missing key. Skill lane only.
SKILL_NO_WRAPPER_BASELINE = {
    "attach-screenshots": 2312,
}

_REPO_DELTAS_HDR_RE = re.compile(
    r"^##\s+Repo deltas this flow needs\s*$",
    re.MULTILINE,
)
_WRAPPER_DELTAS_HDR_RE = re.compile(
    r"^##\s+Deltas\b",
    re.MULTILINE,
)
# Matches `| **key name** |` (table first-column bold key). The character
# class deliberately allows spaces — the skills lane names its keys in prose
# (`**default branch**`, `**raw URL base**`), and a class that excludes the
# space makes those rows invisible to both extraction sides (#2893: 53 of 63
# skill-lane keys were silently unseen before this widened).
_DELTA_KEY_RE = re.compile(
    r"^\|\s*\*\*([^*|]+)\*\*\s*\|",
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


def find_skill_flows(repo_root):
    """Return sorted docs/agents/skills/*.md files that declare Repo deltas.

    Mirrors find_protocols() verbatim, over the skill-sharing lane's canonical
    flow directory instead of the role lane's.
    """
    flows_dir = Path(repo_root) / "docs" / "agents" / "skills"
    if not flows_dir.is_dir():
        return []
    results = []
    for f in sorted(flows_dir.glob("*.md")):
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
    return [k.strip() for k in _DELTA_KEY_RE.findall(_section_body(text, header_re))]


def extract_protocol_keys(protocol_path):
    """Return the ordered list of delta key names declared in the protocol/flow."""
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

    Exact for the role lane's shape (multiple protocol-citing docs can exist
    per repo, and only true wrappers carry ``## Deltas``) but NOT reused for
    the skill lane — see find_skill_wrapper()'s docstring for why.
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


def find_skill_wrapper(repo_root, flow_path):
    """Return the single stem-paired .claude/skills/<stem>/SKILL.md, or [].

    The skill lane's wrapper-identification predicate is stem pairing
    (``docs/agents/skills/<stem>.md`` ↔ ``.claude/skills/<stem>/SKILL.md``),
    not the role lane's cite-scan. Cite-scan is exact only when a citing-only
    file has no ``## Deltas`` table of its own; on the skill lane every
    conforming wrapper carries a Deltas table *for its own flow*, so cite-scan
    can't tell "wrapper of X" from "wrapper of Y that happens to cite X in
    prose" — reproduced as the false-pairing test in
    tests/test_fleet_validate_roles.py. Stem pairing sidesteps the ambiguity
    entirely, since the lane's layout is exactly 1:1.

    Returns a list of 0 or 1 paths, mirroring find_wrappers()'s list contract
    — a stem match with no ``## Deltas`` table is treated the same as no
    match, the same half-written-wrapper handling the role lane uses.
    """
    stem = Path(flow_path).stem
    wrapper = Path(repo_root) / ".claude" / "skills" / stem / "SKILL.md"
    if not wrapper.is_file():
        return []
    try:
        text = _norm(wrapper.read_text(encoding="utf-8", errors="replace"))
    except OSError:
        return []
    if not _WRAPPER_DELTAS_HDR_RE.search(text):
        return []
    return [wrapper]


def extract_wrapper_keys(wrapper_path):
    """Return the ordered list of delta key names in the wrapper's ## Deltas section."""
    return _extract_keys(wrapper_path, _WRAPPER_DELTAS_HDR_RE)


def validate_wrapper(protocol_keys, wrapper_path, aliases=None,
                      missing_key_baseline=None, baseline_key=None):
    """Validate a single wrapper against the protocol/flow's delta keys.

    ``missing_key_baseline``, keyed on ``(baseline_key, delta_key)``, drops a
    missing-key finding entirely rather than reporting it — the skill lane's
    ratchet for a tracked, unrepairable-in-this-PR gap (see
    SKILL_WRAPPER_MISSING_KEY_BASELINE). The role lane never passes one.

    Returns a list of ``{"severity": str, "msg": str}`` findings.
    """
    if aliases is None:
        aliases = DELTA_KEY_ALIASES
    if missing_key_baseline is None:
        missing_key_baseline = {}
    wrapper_keys = extract_wrapper_keys(wrapper_path)
    wrapper_set = set(wrapper_keys)
    alias_targets = {alias: canon for canon, alts in aliases.items() for alias in alts}

    findings = []
    for key in protocol_keys:
        accepted = {key} | set(aliases.get(key, []))
        if not accepted & wrapper_set:
            if (baseline_key, key) in missing_key_baseline:
                continue
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


def _role_find_wrappers(repo_root, proto_path):
    return find_wrappers(repo_root, proto_path.name)


# Each lane supplies its own flow glob and its own wrapper-identification
# predicate; validate_roles(), validate_wrapper(), _extract_keys(), the
# severity split, and the alias map are lane-agnostic and loop over this
# table unchanged.
LANES = [
    {
        "label": "role",
        "find_flows": find_protocols,
        "find_wrappers": _role_find_wrappers,
        "no_wrapper_msg": "no role-*.md in %s/.claude/commands/ references %s",
    },
    {
        "label": "skill",
        "find_flows": find_skill_flows,
        "find_wrappers": find_skill_wrapper,
        "no_wrapper_msg": "no stem-paired %s/.claude/skills/<stem>/SKILL.md "
                           "carries ## Deltas for %s",
    },
]


def validate_roles(repo_roots, aliases=None,
                    skill_missing_key_baseline=None,
                    skill_no_wrapper_baseline=None):
    """Validate the role-sharing and skill-sharing contracts across repo_roots.

    Parameters
    ----------
    repo_roots
        List of ``(absolute_path, label)`` tuples.  The first entry is the
        canonical repo (where ``docs/agents/`` lives).  Subsequent entries are
        downstream repos that should also have wrappers.
    aliases
        Override the module-level DELTA_KEY_ALIASES map.
    skill_missing_key_baseline, skill_no_wrapper_baseline
        Override the module-level skill-lane baselines (tests only — pass
        ``{}`` to prove a gap would otherwise be reported).

    Returns a dict::

        {
          "ok": bool,
          "n_errors": int,
          "n_warnings": int,
          "empty": bool,
          "lanes": [{"label": str, "n_flows": int, "n_wrappers": int}],
          "protocols": [
            {
              "path": str, "name": str, "lane": str, "keys": [str],
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
    if skill_missing_key_baseline is None:
        skill_missing_key_baseline = SKILL_WRAPPER_MISSING_KEY_BASELINE
    if skill_no_wrapper_baseline is None:
        skill_no_wrapper_baseline = SKILL_NO_WRAPPER_BASELINE

    if not repo_roots:
        return {"ok": True, "empty": True, "n_errors": 0, "n_warnings": 0,
                "protocols": [], "lanes": []}

    canonical_root = repo_roots[0][0]

    total_errors = total_warnings = 0
    protocol_results = []
    lane_summaries = []

    for lane in LANES:
        is_skill_lane = lane["label"] == "skill"
        flows = lane["find_flows"](canonical_root)
        lane_wrapper_count = 0

        for proto_path in flows:
            protocol_keys = extract_protocol_keys(proto_path)
            proto_name = proto_path.name
            flow_stem = proto_path.stem
            repo_results = []

            for root, label in repo_roots:
                root_path = Path(root)
                if not root_path.is_dir():
                    continue

                wrappers = lane["find_wrappers"](root, proto_path)

                if not wrappers:
                    if is_skill_lane and flow_stem in skill_no_wrapper_baseline:
                        repo_results.append({
                            "label": label,
                            "no_wrapper": True,
                            "wrappers": [],
                            "findings": [],
                        })
                        continue
                    total_warnings += 1
                    repo_results.append({
                        "label": label,
                        "no_wrapper": True,
                        "wrappers": [],
                        "findings": [
                            {
                                "severity": WARN,
                                "msg": lane["no_wrapper_msg"] % (label, proto_name),
                            }
                        ],
                    })
                    continue

                lane_wrapper_count += len(wrappers)
                wrapper_results = []
                for wp in wrappers:
                    findings = validate_wrapper(
                        protocol_keys, wp, aliases,
                        missing_key_baseline=(
                            skill_missing_key_baseline if is_skill_lane else None
                        ),
                        baseline_key=flow_stem if is_skill_lane else None,
                    )
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
                "lane": lane["label"],
                "keys": protocol_keys,
                "repos": repo_results,
            })

        lane_summaries.append({
            "label": lane["label"],
            "n_flows": len(flows),
            "n_wrappers": lane_wrapper_count,
        })

    return {
        "ok": total_errors == 0,
        "empty": not protocol_results,
        "n_errors": total_errors,
        "n_warnings": total_warnings,
        "protocols": protocol_results,
        "lanes": lane_summaries,
    }
