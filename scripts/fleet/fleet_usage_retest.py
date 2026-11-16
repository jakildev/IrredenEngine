"""Shared stale usage-window retest state for fleet gate readers."""

from __future__ import annotations

import json
import os
import pathlib
import tempfile
from dataclasses import dataclass

DEFAULT_INTERVAL_SECONDS = 6 * 60 * 60
STATE_DIR_NAME = ".retest"


@dataclass(frozen=True)
class RetestStatus:
    age_seconds: int | None
    eligible: bool
    state: str
    last_started_at: int | None = None


def observation_age(data: dict, now: int) -> int | None:
    observed_at = data.get("observed_at")
    if not isinstance(observed_at, (int, float)):
        return None
    return max(0, now - int(observed_at))


def state_path(usage_dir: pathlib.Path, record_name: str) -> pathlib.Path:
    return usage_dir / STATE_DIR_NAME / record_name


def retest_status(
    usage_dir: pathlib.Path,
    record_name: str,
    data: dict,
    now: int,
    interval_seconds: int,
) -> RetestStatus:
    age = observation_age(data, now)
    if age is None or age < interval_seconds:
        return RetestStatus(age, False, "fresh")

    path = state_path(usage_dir, record_name)
    try:
        state = json.loads(path.read_text()) if path.exists() else {}
    except (OSError, json.JSONDecodeError):
        return RetestStatus(age, False, "state-unreadable")

    started_at = state.get("started_at")
    if started_at is None:
        return RetestStatus(age, True, "available")
    if not isinstance(started_at, (int, float)):
        return RetestStatus(age, False, "state-unreadable")
    started_at = int(started_at)
    if now - started_at >= interval_seconds:
        return RetestStatus(age, True, "available", started_at)
    return RetestStatus(age, False, "held", started_at)


def _write_atomic(path: pathlib.Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", dir=path.parent, prefix=f".{path.stem}.", suffix=".tmp", delete=False
    ) as tmp:
        json.dump(data, tmp, separators=(",", ":"))
        tmp.flush()
        os.fsync(tmp.fileno())
        tmp_path = pathlib.Path(tmp.name)
    os.replace(tmp_path, path)


def acquire(
    usage_dir: pathlib.Path,
    record_name: str,
    now: int,
    interval_seconds: int,
    token: str,
) -> dict | None:
    record_path = usage_dir / record_name
    try:
        data = json.loads(record_path.read_text())
    except (OSError, json.JSONDecodeError):
        return None
    status = retest_status(usage_dir, record_name, data, now, interval_seconds)
    if not status.eligible:
        return None
    observed_at = data.get("observed_at")
    if not isinstance(observed_at, (int, float)):
        return None
    state = {
        "record": record_name,
        "rate_limit_type": str(data.get("rateLimitType", pathlib.Path(record_name).stem)),
        "observed_at": int(observed_at),
        "started_at": now,
        "token": token,
    }
    _write_atomic(state_path(usage_dir, record_name), state)
    return state


def finish(
    usage_dir: pathlib.Path,
    record_name: str,
    observed_at: int,
    started_at: int,
    token: str,
) -> str:
    path = state_path(usage_dir, record_name)
    try:
        state = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError):
        return "state-missing"
    if (
        state.get("token") != token
        or state.get("observed_at") != observed_at
        or state.get("started_at") != started_at
    ):
        return "superseded"

    record_path = usage_dir / record_name
    try:
        current = json.loads(record_path.read_text())
    except FileNotFoundError:
        path.unlink(missing_ok=True)
        return "already-clear"
    except (OSError, json.JSONDecodeError):
        return "record-unreadable"

    current_observed = current.get("observed_at")
    if isinstance(current_observed, (int, float)) and int(current_observed) > observed_at:
        return "refreshed"
    record_path.unlink(missing_ok=True)
    path.unlink(missing_ok=True)
    return "cleared"


def rollback(
    usage_dir: pathlib.Path,
    record_name: str,
    started_at: int,
    token: str,
) -> bool:
    path = state_path(usage_dir, record_name)
    try:
        state = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError):
        return False
    if state.get("token") != token or state.get("started_at") != started_at:
        return False
    path.unlink(missing_ok=True)
    return True


def format_age(seconds: int | None) -> str:
    if seconds is None:
        return "unknown"
    if seconds < 60:
        return f"{seconds}s"
    minutes = seconds // 60
    if minutes < 60:
        return f"{minutes}m"
    hours, minutes = divmod(minutes, 60)
    return f"{hours}h{minutes:02d}m" if minutes else f"{hours}h"
