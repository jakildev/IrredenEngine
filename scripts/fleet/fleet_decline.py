"""Decline-memory parsing shared by queue election and runtime routing."""

import os
import re
import time

DECLINE_TTL_SECONDS = 7 * 24 * 3600
DECLINE_TRANSIENT_TTL_SECONDS = 30 * 60
DECLINE_TRANSIENT_RE = re.compile(
    r"HTTP 401|Bad credentials|HTTP 5\d\d|rate limit|API rate|timed out|Could not resolve host",
    re.IGNORECASE)
DECLINE_HOST_RE = re.compile(
    r"RESULT=NO-DISPLAY|0 display monitors|online displays? 0|no online display|no display|"
    r"(?:GPU|ir-acquire).{0,80}(?:lock|lease).{0,80}(?:held|unavailable|"
    r"did not become available)", re.IGNORECASE)
DECLINE_SANDBOX_RE = re.compile(
    r"sandbox|operation not permitted|rejected by .{0,40}policy|"
    r"approval required by policy|(?:command|tool) window", re.IGNORECASE)


def reason_class(detail):
    if DECLINE_SANDBOX_RE.search(detail):
        return "sandbox"
    if DECLINE_HOST_RE.search(detail):
        return "host"
    if DECLINE_TRANSIENT_RE.search(detail):
        return "transient"
    return "substantive"


def target(kind, record, extra=None):
    number = record.get("number")
    if number is None:
        number = str(record.get("issue") or "").lstrip("#") or None
    if number is None:
        return None
    line = f"{kind}:{record.get('repo') or 'engine'}:{number}"
    return f"{line}:{extra}" if extra is not None else line


def metadata(kind, record, role=None, extra=None):
    current = record.get("updatedAt") or ""
    target_line = target(kind, record, extra)
    if target_line is None or not current:
        return None
    fleet_home = os.environ.get("FLEET_HOME") or os.path.join(os.path.expanduser("~"), ".fleet")
    state = os.environ.get("FLEET_STATE_DIR") or os.path.join(fleet_home, "state")
    path = os.path.join(state, "declined", target_line.replace(":", "-"))
    try:
        age = time.time() - os.stat(path).st_mtime  # lint: state-mtime-ok decline TTL
        if age > DECLINE_TTL_SECONDS:
            return None
        with open(path, encoding="utf-8") as handle:
            lines = handle.read().splitlines()
    except OSError:
        return None
    stored = lines[0].strip() if lines else ""
    detail = lines[1] if len(lines) > 1 else ""
    stored_role = lines[2].strip() if len(lines) > 2 else ""
    keyed = {}
    for line in lines[3:]:
        key, separator, value = line.partition("=")
        if separator:
            keyed[key] = value
    decline_class = reason_class(detail)
    if decline_class != "substantive" and age > DECLINE_TRANSIENT_TTL_SECONDS:
        return None
    if stored_role and role and stored_role != role:
        return None
    if not stored or current > stored:
        return None
    shadow = int(keyed["shadow_merged_pr"]) \
        if keyed.get("shadow_merged_pr", "").isdigit() else None
    return {"updated_at": stored, "reason": detail, "role": stored_role,
            "shadow_merged_pr": shadow, "transient": decline_class != "substantive",
            "class": decline_class, "runtime": keyed.get("runtime") or None,
            "age_seconds": max(0, int(age))}
