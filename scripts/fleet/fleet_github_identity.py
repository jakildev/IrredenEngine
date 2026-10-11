"""GitHub authentication lanes and identity-qualified quota admission."""

import json
import os
import time
from datetime import datetime
from pathlib import Path

LANES = {
    "scout": "daemon",
    "dispatcher": "daemon",
    "queue-ingest": "daemon",
    "cleanup": "daemon",
    "reconcile": "daemon",
    "stalled-sweep": "daemon",
    "pre-claim": "daemon",
    "completion": "daemon",
    "dispatched-pane": "user",
    "babysit": "user",
    "pane-claim-mutation": "user",
    "human-shell": "user",
}


def user_environment(env=None):
    result = dict(os.environ if env is None else env)
    result.pop("GH_TOKEN", None)
    result.pop("GITHUB_TOKEN", None)
    result["FLEET_GH_IDENTITY"] = "user"
    return result


def current_identity(env=None):
    env = os.environ if env is None else env
    if env.get("FLEET_GH_IDENTITY") == "user":
        return "user"
    token = env.get("GH_TOKEN") or env.get("GITHUB_TOKEN") or ""
    return "app" if token.startswith("ghs_") else "user"


def quota_path(usage_dir, pool, identity, rejected=False):
    if identity not in ("user", "app") or pool not in ("core", "graphql", "search"):
        raise ValueError("invalid GitHub quota identity or pool")
    suffix = ".rejected" if rejected else ""
    return Path(usage_dir) / f"github-{identity}-{pool}{suffix}.json"


def read_quota(usage_dir, pool, identity, rejected=False):
    path = quota_path(usage_dir, pool, identity, rejected)
    # An upgrade may leave a still-binding refusal in the unqualified file.
    suffix = ".rejected" if rejected else ""
    legacy = Path(usage_dir) / f"github-{pool}{suffix}.json"
    records = []
    for candidate in (path, legacy):
        try:
            data = json.loads(candidate.read_text())
        except (OSError, ValueError):
            continue
        if isinstance(data, dict) and data.get("identity") == identity:
            records.append(data)
    return records


def reset_epoch(value):
    if isinstance(value, (int, float)):
        return value
    if isinstance(value, str):
        try:
            return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()
        except ValueError:
            pass
    return None


def quota_threshold(rate_type):
    for name in (f"FLEET_DISPATCHER_USAGE_GATE_{rate_type.upper()}",
                 "FLEET_DISPATCHER_USAGE_GATE"):
        try:
            return float(os.environ[name])
        except (KeyError, ValueError):
            pass
    return 0.90


def quota_gate(usage_dir, identity, threshold_for=quota_threshold, *, now=None, stale_s=3600,
               reset_grace_s=600):
    """A refusal survives stale telemetry until its own reset plus grace."""
    now = time.time() if now is None else now
    reasons = []
    for pool in ("core", "graphql"):
        samples = read_quota(usage_dir, pool, identity)
        rejections = read_quota(usage_dir, pool, identity, rejected=True)
        fresh = False
        for sample in samples + rejections:
            reset = reset_epoch(sample.get("resetsAt"))
            observed = sample.get("observed_at")
            recent = isinstance(observed, (int, float)) and 0 <= now - observed <= stale_s
            if reset is not None and reset + reset_grace_s <= now:
                continue
            rejected = sample.get("status") == "rejected"
            if rejected and ((reset is not None and reset + reset_grace_s > now) or recent):
                reasons.append(f"github_{pool}[{identity}] rejected")
                continue
            if not recent:
                continue
            utilization = sample.get("utilization")
            if not isinstance(utilization, (int, float)):
                continue
            fresh = True
            if utilization >= threshold_for(f"github_{pool}"):
                reasons.append(f"github_{pool}[{identity}] util={utilization:.0%}")
        if not fresh and not any(f"github_{pool}[" in reason for reason in reasons):
            reasons.append(f"github_{pool}[{identity}] missing or stale")
    return {"state": "closed" if reasons else "open", "identity": identity,
            "reasons": reasons}
