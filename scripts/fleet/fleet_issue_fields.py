"""The machine-parsed header lines of a fleet issue body: one reader, one editor.

Reader. `parse_issue_field` and the `**Host:**` pin (`host_field_keys`,
`body_required_host`) are what the scout projects as `needs_host`; they live
here rather than in `fleet-state-scout` so `fleet-issue set-field` checks an
edit with the scout's own reading instead of a second copy. The `**Blocked
by:**` reader is `fleet_blocked_by`, shared the same way.

Editor. `compose_field_lines` rewrites the standalone `**<Field>:** <value>`
lines of one field and leaves every other byte of the body alone, line
endings included. It is deliberately textual: whether the parser reads the
lines it wrote is the caller's post-edit check, never the editor's guess.
"""
import re

# --- reader -----------------------------------------------------------------


def parse_issue_field(body, field):
    """Value of the first `**<field>:**` anywhere in `body`, to end of line.

    Not dequoted and not anchored: prose, quotes and code spans count, so a
    line written for this reader must be the body's first match."""
    if not body:
        return ""
    m = re.search(rf'\*\*(?:Suggested\s+)?{re.escape(field)}:\*\*\s*(.+?)(?:\n|$)', body)
    return m.group(1).strip() if m else ""


def host_requirement_re(verb_hosts, adj_hosts):
    """The prose host-requirement grammar, instantiated for one host-word set.

    Two precision-first shapes: a verb form ("must run on a Linux host", "only
    builds on an OpenGL host") and the adjective form ("GL-host only"). The
    scout's backend gate and the OS pin below share this one skeleton, so a
    phrasing added to one cannot silently miss the other. The named groups
    carry the matched host word for the OS-pin caller."""
    return re.compile(
        r"\b(?:"
        r"(?:must\s+(?:be\s+)?run\s+on|only\s+(?:runs?|builds?)\s+on)\s+"
        rf"(?:an?\s+|the\s+)?(?P<verb_os>{verb_hosts})[\w/+-]*\s+host"
        rf"|(?P<adj_os>{adj_hosts})[\s-]?host[\s-]?only"
        r")\b",
        re.IGNORECASE,
    )


# The OS a task's body pins it to, as a dispatch-host key. A mac declaration
# DOES gate the task off linux/windows (the scout's GL backstop deliberately
# leaves that half out). `opengl` is a backend, not an OS, so it yields no host
# here. Keys match `fleet_task_class._current_host` (`mac`, not `macos`).
HOST_REQUIRED_RE = host_requirement_re(
    "linux|windows|mac(?:os)?", "linux|windows|mac(?:os)?")
HOST_KEYS = {"linux": "linux", "windows": "windows", "mac": "mac",
             "macos": "mac"}


def host_field_words(body):
    """Lower-cased words of an explicit `**Host:**` body field, in order. A
    parenthetical is the filer's aside (`windows (linux-debug refs differ)`),
    not a second host, so it is dropped before the words are read."""
    value = re.sub(r"\([^)]*\)", " ", parse_issue_field(body, "Host"))
    return re.findall(r"[a-z]+", value.lower())


def host_field_keys(body):
    """Dispatch-host keys the `**Host:**` field names, in order, deduped."""
    keys = []
    for word in host_field_words(body):
        key = HOST_KEYS.get(word)
        if key and key not in keys:
            keys.append(key)
    return keys


def body_required_host(body):
    """`linux` | `windows` | `mac` when the body pins the task to ONE OS, else
    None. An explicit `**Host:**` field wins over the prose forms, including
    when it names several hosts: that is no single pin (the scout's GL gate
    reads the all-GL case), and the prose must not narrow it back to one."""
    body = body or ""
    keys = host_field_keys(body)
    if keys:
        return keys[0] if len(keys) == 1 else None
    m = HOST_REQUIRED_RE.search(body)
    if not m:
        return None
    return HOST_KEYS[(m.group("verb_os") or m.group("adj_os")).lower()]


# --- editor -----------------------------------------------------------------

_FENCE_RE = re.compile(r"^ {0,3}(?:```|~~~)")
_HEADING_RE = re.compile(r"^ {0,3}#{1,6}(?:[ \t]|$)")
_ANY_FIELD_RE = re.compile(r"^[ \t]*-?[ \t]*\*\*[^*\n]+:\*\*")


def _field_line_re(field):
    # A leading quote marker is tolerated on purpose: a quoted line is still
    # "the existing line", and editing it is a wrong edit the caller's parser
    # check must refuse.
    return re.compile(
        r"^(?P<prefix>(?:[ \t]*>)*[ \t]*-?[ \t]*\*\*(?:Suggested\s+)?"
        + re.escape(field) + r":\*\*)[ \t]*(?P<value>.*?)[ \t]*$",
        re.IGNORECASE,
    )


def _split_lines(body):
    """`body` as `[text, eol]` pairs that join back to it byte for byte."""
    parts = body.split("\n")
    pairs = []
    for i, part in enumerate(parts):
        if i == len(parts) - 1:
            pairs.append([part, ""])
        elif part.endswith("\r"):
            pairs.append([part[:-1], "\r\n"])
        else:
            pairs.append([part, "\n"])
    return pairs


def _fenced_flags(pairs):
    flags, inside = [], False
    for text, _ in pairs:
        if _FENCE_RE.match(text):
            flags.append(True)
            inside = not inside
        else:
            flags.append(inside)
    return flags


def _insert_index(pairs, fenced):
    """Below the last bold header field above the first heading; else just
    above that heading; else the top of the body."""
    heading = next((i for i, (text, _) in enumerate(pairs)
                    if not fenced[i] and _HEADING_RE.match(text)), len(pairs))
    fields = [i for i in range(heading)
              if not fenced[i] and _ANY_FIELD_RE.match(pairs[i][0])]
    if fields:
        return fields[-1] + 1
    return heading if heading < len(pairs) else 0


def compose_field_lines(body, field, values):
    """`(new_body, old_values)`: `body` with `field` written as one standalone
    `**<field>:** <value>` line per entry of `values`.

    The existing lines of that field are the targets: those outside quotes and
    code when any exist, otherwise every one. The first target is replaced in
    place (its prefix — list marker, `Suggested ` — and line ending kept),
    further values follow it, and the remaining targets are removed. With no
    existing line, new ones are inserted below the bold header fields that
    precede the first heading. `old_values` lists the targets' values; empty
    means the field was absent."""
    body = body or ""
    nl = "\r\n" if "\r\n" in body else "\n"
    pairs = _split_lines(body)
    fenced = _fenced_flags(pairs)
    line_re = _field_line_re(field)
    found = [(i, m) for i, (text, _) in enumerate(pairs)
             if (m := line_re.match(text))]
    plain = [(i, m) for i, m in found
             if not fenced[i] and not m.group("prefix").lstrip().startswith(">")
             and not pairs[i][0].startswith("    ")]
    targets = plain or found
    old_values = [m.group("value") for _, m in targets]

    if not targets:
        texts = [f"**{field}:** {v}" for v in values]
        at = _insert_index(pairs, fenced)
        if at == len(pairs):
            pairs[-1][1] = nl
            block = [[t, nl] for t in texts[:-1]] + [[texts[-1], ""]]
        else:
            block = [[t, nl] for t in texts]
        pairs[at:at] = block
        return "".join(t + e for t, e in pairs), old_values

    first, first_match = targets[0]
    prefix = first_match.group("prefix")
    texts = [f"{prefix} {v}" for v in values]
    block = [[t, nl] for t in texts[:-1]] + [[texts[-1], pairs[first][1]]]
    drop = {i for i, _ in targets[1:]}
    out = []
    for i, pair in enumerate(pairs):
        if i == first:
            out.extend(block)
        elif i not in drop:
            out.append(pair)
    return "".join(t + e for t, e in out), old_values
