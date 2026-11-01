#!/usr/bin/env python3
"""Host-wide benchmark quiet-window state machine."""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes
import functools
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

ROOT = Path(os.environ["IR_LOCK_ROOT"]) / "quiet"
RECORDS = ROOT / "records"
LEASES = ROOT / "leases"
DISABLED = ROOT / "disabled"
REQUIRED_RECORD_FIELDS = frozenset(
    {"pid", "phase", "requested_at", "linger_seconds", "max_seconds"}
)
# A hung table read freezes covered_through, which reads as an uncovered lease.
MSYS_PS_TIMEOUT_SECONDS = 5.0
REPLACE_ATTEMPTS = 20
REPLACE_BACKOFF_SECONDS = 0.025


def now() -> float:
    return float(os.environ.get("IR_QUIET_NOW", time.time()))


def replace_retrying_sharing_violation(source: str, destination: Path) -> None:
    """os.replace that outlasts a concurrent reader of the destination.

    Windows refuses the replace with ERROR_ACCESS_DENIED while any process
    holds the destination open without FILE_SHARE_DELETE, which Python's
    open() never passes. Readers hold it for microseconds, so a short bounded
    retry always lands; POSIX rename has no such window.
    """
    for attempt in range(REPLACE_ATTEMPTS):
        try:
            os.replace(source, destination)
            return
        except PermissionError:
            if attempt == REPLACE_ATTEMPTS - 1:
                raise
            time.sleep(REPLACE_BACKOFF_SECONDS)


def atomic_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(value, handle, sort_keys=True)
            handle.write("\n")
        replace_retrying_sharing_violation(temp_name, path)
    finally:
        try:
            os.unlink(temp_name)
        except FileNotFoundError:
            pass


def read_json(path: Path) -> dict | None:
    try:
        with path.open(encoding="utf-8") as handle:
            value = json.load(handle)
        return value if isinstance(value, dict) else None
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return None


def windows_native_pid(pid: int, owner_token: str = "") -> int:
    match = re.match(r"\s*(\d+)\b", owner_token)
    return int(match.group(1)) if match else pid


def owner_token_root(owner_token: str) -> str:
    """The runtime root of a "<windows-pid> <cygwin-root>" token; may contain spaces."""
    match = re.match(r"\s*\d+\s+(.*\S)", owner_token)
    return match.group(1) if match else ""


def windows_native_alive(pid: int) -> bool:
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(0x1000, False, pid)
    if not handle:
        return False
    try:
        code = ctypes.c_ulong()
        return bool(kernel32.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259
    finally:
        kernel32.CloseHandle(handle)


def windows_pid_alive(pid: int, owner_token: str = "") -> bool:
    return windows_native_alive(windows_native_pid(pid, owner_token))


def pid_alive(pid: int, owner_token: str = "") -> bool:
    if pid <= 0:
        return False
    if os.name == "nt":
        return windows_pid_alive(pid, owner_token)
    try:
        os.kill(pid, 0)
        return True
    except PermissionError:
        return True
    except ProcessLookupError:
        return False


class ProcBsdInfo(ctypes.Structure):
    _fields_ = [
        ("flags", ctypes.c_uint32),
        ("status", ctypes.c_uint32),
        ("xstatus", ctypes.c_uint32),
        ("pid", ctypes.c_uint32),
        ("ppid", ctypes.c_uint32),
        ("ids", ctypes.c_uint32 * 7),
        ("comm", ctypes.c_char * 16),
        ("name", ctypes.c_char * 32),
        ("process_fields", ctypes.c_uint32 * 6),
        ("start_tvsec", ctypes.c_uint64),
        ("start_tvusec", ctypes.c_uint64),
    ]


class ProcessEntry32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", ctypes.c_uint32),
        ("cntUsage", ctypes.c_uint32),
        ("th32ProcessID", ctypes.c_uint32),
        ("th32DefaultHeapID", ctypes.c_size_t),
        ("th32ModuleID", ctypes.c_uint32),
        ("cntThreads", ctypes.c_uint32),
        ("th32ParentProcessID", ctypes.c_uint32),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", ctypes.c_uint32),
        ("szExeFile", ctypes.c_wchar * 260),
    ]


@functools.lru_cache(maxsize=None)
def kernel32():
    """A private kernel32 handle, so these prototypes never leak into ctypes.windll."""
    library = ctypes.WinDLL("kernel32", use_last_error=True)
    handle = ctypes.wintypes.HANDLE
    library.CreateToolhelp32Snapshot.restype = handle
    library.CreateToolhelp32Snapshot.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
    for name in ("Process32FirstW", "Process32NextW"):
        function = getattr(library, name)
        function.restype = ctypes.wintypes.BOOL
        function.argtypes = [handle, ctypes.POINTER(ProcessEntry32W)]
    library.OpenProcess.restype = handle
    library.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.wintypes.BOOL, ctypes.c_uint32]
    library.GetProcessTimes.restype = ctypes.wintypes.BOOL
    library.GetProcessTimes.argtypes = [handle] + [ctypes.POINTER(ctypes.c_uint64)] * 4
    library.CloseHandle.restype = ctypes.wintypes.BOOL
    library.CloseHandle.argtypes = [handle]
    return library


def win32_parents() -> dict[int, int]:
    library = kernel32()
    snapshot = library.CreateToolhelp32Snapshot(0x2, 0)
    if snapshot in (None, ctypes.c_void_p(-1).value):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        entry = ProcessEntry32W()
        entry.dwSize = ctypes.sizeof(entry)
        parents = {}
        found = library.Process32FirstW(snapshot, ctypes.byref(entry))
        while found:
            parents[int(entry.th32ProcessID)] = int(entry.th32ParentProcessID)
            found = library.Process32NextW(snapshot, ctypes.byref(entry))
        return parents
    finally:
        library.CloseHandle(snapshot)


def parse_msys_ps(text: str) -> dict[int, tuple[int, int]]:
    """Map MSYS pid -> (MSYS ppid, Windows pid) from a runtime's bare `ps`."""
    rows = {}
    for line in text.splitlines():
        fields = line.split()
        if fields and not fields[0].isdigit():
            fields = fields[1:]
        if len(fields) < 4 or not all(field.isdigit() for field in fields[:4]):
            continue
        pid, ppid, _, winpid = (int(field) for field in fields[:4])
        rows[pid] = (ppid, winpid)
    return rows


def read_msys_table(runtime_root: Path) -> dict[int, tuple[int, int]]:
    """One runtime's process table. Raises when it cannot be read: callers fail closed."""
    result = subprocess.run(
        [str(Path(runtime_root) / "usr" / "bin" / "ps.exe")],
        capture_output=True,
        text=True,
        timeout=MSYS_PS_TIMEOUT_SECONDS,
        check=True,
    )
    return parse_msys_ps(result.stdout)


def merge_msys_parents(
    win32: dict[int, int], tables: list[dict[int, tuple[int, int]]]
) -> dict[int, int]:
    """Win32 creator links, overridden by MSYS parentage wherever the MSYS parent is live.

    An MSYS exec replaces the image with a new Windows process whose creator has
    exited, so the Win32 chain breaks there; the runtime's table still knows the
    logical parent.
    """
    parents = dict(win32)
    for table in tables:
        for ppid, winpid in table.values():
            parent = table.get(ppid)
            if parent is not None and parent[1] != winpid:
                parents[winpid] = parent[1]
    return parents


def process_parents(runtime_roots: tuple[Path, ...] = ()) -> dict[int, int]:
    system = platform.system()
    if system == "Linux":
        parents: dict[int, int] = {}
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                stat = (entry / "stat").read_text(encoding="utf-8")
                tail = stat[stat.rfind(")") + 2 :].split()
                parents[int(entry.name)] = int(tail[1])
            except (FileNotFoundError, ProcessLookupError, ValueError, IndexError):
                continue
        return parents
    if system == "Darwin":
        libproc = ctypes.CDLL("/usr/lib/libproc.dylib")
        count = libproc.proc_listallpids(None, 0)
        pids = (ctypes.c_int * max(count * 2, 1))()
        count = libproc.proc_listallpids(pids, ctypes.sizeof(pids))
        parents = {}
        for pid in pids[:count]:
            info = ProcBsdInfo()
            size = libproc.proc_pidinfo(pid, 3, 0, ctypes.byref(info), ctypes.sizeof(info))
            if size == ctypes.sizeof(info):
                parents[int(info.pid)] = int(info.ppid)
        return parents
    if system == "Windows":
        return merge_msys_parents(
            win32_parents(), [read_msys_table(root) for root in runtime_roots]
        )
    output = subprocess.check_output(
        ["ps", "-A", "-o", "pid=,ppid="], text=True, stderr=subprocess.DEVNULL
    )
    parents = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 2:
            parents[int(fields[0])] = int(fields[1])
    return parents


class RusageInfoV2(ctypes.Structure):
    _fields_ = [
        ("uuid", ctypes.c_ubyte * 16),
        ("user_time", ctypes.c_uint64),
        ("system_time", ctypes.c_uint64),
        ("pkg_idle_wkups", ctypes.c_uint64),
        ("interrupt_wkups", ctypes.c_uint64),
        ("pageins", ctypes.c_uint64),
        ("wired_size", ctypes.c_uint64),
        ("resident_size", ctypes.c_uint64),
        ("phys_footprint", ctypes.c_uint64),
        ("proc_start_abstime", ctypes.c_uint64),
        ("proc_exit_abstime", ctypes.c_uint64),
        ("child_user_time", ctypes.c_uint64),
        ("child_system_time", ctypes.c_uint64),
        ("child_pkg_idle_wkups", ctypes.c_uint64),
        ("child_interrupt_wkups", ctypes.c_uint64),
        ("child_pageins", ctypes.c_uint64),
        ("child_elapsed_abstime", ctypes.c_uint64),
        ("diskio_bytesread", ctypes.c_uint64),
        ("diskio_byteswritten", ctypes.c_uint64),
    ]


class MachTimebaseInfo(ctypes.Structure):
    _fields_ = [("numer", ctypes.c_uint32), ("denom", ctypes.c_uint32)]


def cpu_time(pid: int) -> float:
    system = platform.system()
    if system == "Linux":
        stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
        fields = stat[stat.rfind(")") + 2 :].split()
        ticks = sum(int(fields[index]) for index in (11, 12, 13, 14))
        return ticks / os.sysconf("SC_CLK_TCK")
    if system == "Darwin":
        info = RusageInfoV2()
        libproc = ctypes.CDLL("/usr/lib/libproc.dylib")
        result = libproc.proc_pid_rusage(
            pid, 2, ctypes.byref(info)
        )
        if result != 0:
            raise ProcessLookupError(pid)
        absolute = (
            info.user_time
            + info.system_time
            + info.child_user_time
            + info.child_system_time
        )
        timebase = MachTimebaseInfo()
        libsystem = ctypes.CDLL(None)
        if libsystem.mach_timebase_info(ctypes.byref(timebase)) != 0 or timebase.denom == 0:
            raise OSError("mach_timebase_info failed")
        return absolute * timebase.numer / timebase.denom / 1_000_000_000
    # Own user + kernel time only: Windows never folds a reaped child into its parent.
    # A process that cannot be opened or read is reported gone, so a member exiting
    # mid-snapshot is skipped rather than failing the whole sample.
    times = windows_process_times(pid)
    if times is None:
        raise ProcessLookupError(pid)
    return (times[2] + times[3]) / 10_000_000


def windows_process_times(pid: int) -> tuple[int, int, int, int] | None:
    """GetProcessTimes' creation, exit, kernel and user FILETIMEs; None when unreadable."""
    library = kernel32()
    handle = library.OpenProcess(0x1000, False, pid)
    if not handle:
        return None
    try:
        times = [ctypes.c_uint64() for _ in range(4)]
        if not library.GetProcessTimes(handle, *(ctypes.byref(value) for value in times)):
            return None
        return tuple(value.value for value in times)
    finally:
        library.CloseHandle(handle)


def windows_process_start(pid: int) -> int | None:
    times = windows_process_times(pid)
    return times[0] if times else None


def descendants(root: int, children: dict[int, list[int]]) -> set[int]:
    result = {root}
    pending = [root]
    while pending:
        for child in children.get(pending.pop(), []):
            if child not in result:
                result.add(child)
                pending.append(child)
    return result


def tree_snapshot(
    holder: int, excluded_roots: set[int], runtime_roots: tuple[Path, ...] = ()
) -> tuple[dict[int, float], dict[int, int]]:
    parents = process_parents(runtime_roots)
    children: dict[int, list[int]] = {}
    for child, parent in parents.items():
        children.setdefault(parent, []).append(child)
    members = descendants(holder, children)
    for root in excluded_roots:
        members.difference_update(descendants(root, children))
    values = {}
    for pid in members:
        try:
            values[pid] = cpu_time(pid)
        except (FileNotFoundError, ProcessLookupError, subprocess.SubprocessError):
            continue
    if holder not in values:
        raise ProcessLookupError(holder)
    return values, parents


def sample_cores(
    holder: int, excluded: set[int], duration: float, runtime_roots: tuple[Path, ...] = ()
) -> tuple[float, float, float]:
    before, before_parents = tree_snapshot(holder, excluded, runtime_roots)
    started = time.time()
    time.sleep(duration)
    after, _ = tree_snapshot(holder, excluded, runtime_roots)
    finished = time.time()
    used = sum(max(0.0, value - before.get(pid, 0.0)) for pid, value in after.items())
    if platform.system() != "Windows":
        # A child that was visible in the first snapshot and reaped before the
        # second is now included in its live parent's child total. Remove the
        # child's already-observed lifetime so it is not counted twice.
        reaped_seen = sum(
            value
            for pid, value in before.items()
            if pid not in after and before_parents.get(pid) in after
        )
        used = max(0.0, used - reaped_seen)
    return used / max(finished - started, 0.001), started, finished


def lease_path(tag: str) -> Path:
    return LEASES / tag


def parker_alive(row: dict) -> bool:
    """The row's writer is still running: a Windows pid recycled to another process is not."""
    pid = int(row.get("pid", 0))
    owner_token = str(row.get("owner_token", ""))
    if not pid_alive(pid, owner_token):
        return False
    start_time = int(row.get("start_time", 0))
    if os.name != "nt" or not start_time:
        return True
    return windows_process_start(windows_native_pid(pid, owner_token)) == start_time


def live_parkers(path: Path) -> list[dict]:
    rows = []
    for entry in (path / "parkers").glob("*.json"):
        row = read_json(entry)
        if row and parker_alive(row):
            rows.append(row)
    return rows


def lease_rows() -> list[dict]:
    rows = []
    for path in LEASES.iterdir() if LEASES.exists() else []:
        meta = read_json(path / "lease.json")
        if not meta:
            continue
        if not pid_alive(
            int(meta.get("holder_pid", 0)), str(meta.get("owner_token", ""))
        ):
            shutil.rmtree(path, ignore_errors=True)
            continue
        parkers = live_parkers(path)
        meta["state"] = (
            "parked"
            if parkers and all(row.get("state") == "parked" for row in parkers)
            else "busy"
        )
        meta["parkers"] = parkers
        rows.append(meta)
    return rows


def record_rows(owner: str | None = None) -> list[dict]:
    rows = []
    stamp = now()
    for path in RECORDS.iterdir() if RECORDS.exists() else []:
        row = read_json(path / "record.json")
        if not row:
            continue
        if not REQUIRED_RECORD_FIELDS.issubset(row):
            shutil.rmtree(path, ignore_errors=True)
            continue
        row["path"] = str(path)
        phase = row.get("phase")
        alive = pid_alive(int(row.get("pid", 0)), str(row.get("owner_token", "")))
        if phase != "released" and not alive:
            shutil.rmtree(path, ignore_errors=True)
            continue
        phase_started_at = None
        if phase == "waiting":
            phase_started_at = row["requested_at"]
        elif phase == "draining":
            phase_started_at = row.get("draining_at", row["requested_at"])
        elif phase == "held":
            phase_started_at = row.get("barrier_at", row["requested_at"])
        if phase_started_at is not None and stamp - float(phase_started_at) >= float(
            row["max_seconds"]
        ):
            row["state"] = "expired"
        elif phase == "released":
            released = float(row.get("released_at", 0))
            if stamp - released >= float(row["linger_seconds"]):
                shutil.rmtree(path, ignore_errors=True)
                continue
            row["state"] = "linger"
        else:
            row["state"] = phase
        if owner and row.get("owner") == owner and row["state"] == "linger":
            continue
        rows.append(row)
    return rows


def disabled_row() -> dict | None:
    return read_json(DISABLED)


def status_payload(owner: str | None) -> dict:
    disabled = disabled_row()
    leases = lease_rows()
    if disabled:
        return {"state": "disabled", "switch": disabled, "leases": leases}
    records = record_rows(owner)
    in_force = [row for row in records if row["state"] not in {"expired"}]
    selected = min(in_force or records, key=lambda row: row["requested_at"], default=None)
    state = selected["state"] if selected else "off"
    return {
        "state": state,
        "record": selected,
        "records": records,
        "leases": leases,
        "busy": sum(row["state"] == "busy" for row in leases),
        "parked": sum(row["state"] == "parked" for row in leases),
    }


def event(kind: str, tag: str, detail: str = "") -> None:
    stamp = now()
    for record in record_rows():
        if record.get("phase") != "held":
            continue
        events = Path(record["path"]) / "events"
        events.mkdir(parents=True, exist_ok=True)
        name = f"{time.time_ns()}-{uuid.uuid4().hex}.json"
        atomic_json(events / name, {"kind": kind, "tag": tag, "detail": detail, "at": stamp})


def cmd_lease_create(args: argparse.Namespace) -> int:
    path = lease_path(args.tag)
    path.mkdir(parents=True, exist_ok=True)
    atomic_json(
        path / "lease.json",
        {
            "tag": args.tag,
            "holder_pid": args.pid,
            "owner_token": args.owner_token,
            "created_at": now(),
        },
    )
    event("lease-created", args.tag)
    return 0


def cmd_lease_drop(args: argparse.Namespace) -> int:
    event("lease-dropped", args.tag)
    shutil.rmtree(lease_path(args.tag), ignore_errors=True)
    return 0


def excluded_roots(path: Path, own_pid: int, measurement_root: int) -> set[int]:
    roots = {own_pid}
    if measurement_root > 0:
        roots.add(measurement_root)
    for row in live_parkers(path):
        roots.add(int(row["pid"]))
        root = int(row.get("measurement_root", 0))
        if root > 0:
            roots.add(root)
    return roots


def resolve_measurement_root(measurement_root: int, runtime_roots: tuple[Path, ...]) -> int:
    """The root itself when it is this parker's ancestor, else 0 with a warning."""
    if measurement_root <= 0:
        return 0
    try:
        parents = process_parents(runtime_roots)
    except (OSError, ValueError, subprocess.SubprocessError):
        parents = {}
    cursor = os.getppid()
    ancestors = set()
    while cursor > 0 and cursor not in ancestors:
        ancestors.add(cursor)
        cursor = parents.get(cursor, 0)
    if measurement_root in ancestors:
        return measurement_root
    print(
        f"ir-acquire: ignoring non-ancestor IR_QUIET_MEASURE_ROOT={measurement_root}",
        file=sys.stderr,
    )
    return 0


def write_parker(path: Path, state: dict) -> None:
    """A failed write leaves the last row standing; the parker keeps sampling.

    The row is advisory to the holder's barrier, and coverage only accepts a
    row whose covered_through reaches the release, so a lagging row can cost a
    measurement but never certify one. Exiting would fail the queued run.
    """
    try:
        atomic_json(path, state)
    except OSError as error:
        print(f"ir-acquire: parker row write failed, parker continues: {error}", file=sys.stderr)


def cmd_park(args: argparse.Namespace) -> int:
    path = lease_path(args.tag)
    meta = read_json(path / "lease.json")
    if not meta:
        return 0
    measurement_root = args.measurement_root
    runtime_roots: tuple[Path, ...] = ()
    if os.name == "nt":
        measurement_root = windows_native_pid(measurement_root, args.measurement_root_token)
        named = (owner_token_root(str(meta.get("owner_token", ""))), args.runtime_root)
        runtime_roots = tuple({Path(root) for root in named if root})
    measurement_root = resolve_measurement_root(measurement_root, runtime_roots)
    parker_path = path / "parkers" / f"{os.getpid()}.json"
    state = {
        "pid": os.getpid(),
        "owner_token": str(os.getpid()) if os.name == "nt" else "",
        "state": "busy",
        "quiet_since": now(),
        "covered_through": now(),
        "measurement_root": measurement_root,
        "launcher_token": args.launcher_token,
    }
    if os.name == "nt":
        state["start_time"] = windows_process_start(os.getpid()) or 0
    write_parker(parker_path, state)
    try:
        while True:
            payload = status_payload(args.tag)
            if payload["state"] in {"off", "expired", "disabled"}:
                state["state"] = "busy"
                state["quiet_since"] = now()
                state["covered_through"] = now()
                write_parker(parker_path, state)
                hold = os.environ.get("IR_QUIET_TEST_UNPARK_HOLD", "")
                if hold:
                    try:
                        time.sleep(float(hold))
                    except ValueError:
                        entered = Path(hold)
                        release = Path(f"{hold}.release")
                        entered.write_text("busy\n", encoding="utf-8")
                        while not release.exists():
                            time.sleep(0.01)
                payload = status_payload(args.tag)
                if payload["state"] in {"off", "expired", "disabled"}:
                    return 0
                continue
            records = [row for row in payload.get("records", []) if row["state"] != "expired"]
            sample = min(float(row["settle_sample_seconds"]) for row in records)
            threshold = min(float(row["settle_cpu"]) for row in records)
            try:
                cores, started, finished = sample_cores(
                    windows_native_pid(
                        int(meta["holder_pid"]), str(meta.get("owner_token", ""))
                    )
                    if os.name == "nt"
                    else int(meta["holder_pid"]),
                    excluded_roots(path, os.getpid(), measurement_root),
                    sample,
                    runtime_roots,
                )
            except (OSError, ValueError, subprocess.SubprocessError):
                cores, started, finished = threshold, time.time(), time.time()
            if cores < threshold:
                if state["state"] != "parked":
                    state["quiet_since"] = started
                state["state"] = "parked"
            else:
                state["state"] = "busy"
                state["quiet_since"] = finished
                event("busy", args.tag, f"{cores:.3f} {started:.6f}-{finished:.6f}")
            state["covered_through"] = finished
            write_parker(parker_path, state)
    finally:
        try:
            parker_path.unlink()
        except FileNotFoundError:
            pass


def cmd_park_drop(args: argparse.Namespace) -> int:
    """Remove the rows a killed parker could not: its launcher's, and any whose writer is gone.

    The launcher token matches a parker that is still dying, which the liveness
    sweep would keep. An empty token matches nothing, since every parker outside a
    benchmark carries an empty one.
    """
    for entry in (lease_path(args.tag) / "parkers").glob("*.json"):
        row = read_json(entry)
        if row is None:
            continue
        token = row.get("launcher_token")
        if (args.launcher_token and token == args.launcher_token) or not parker_alive(row):
            try:
                entry.unlink()
            except FileNotFoundError:
                pass
    return 0


def cmd_status(args: argparse.Namespace) -> int:
    payload = status_payload(args.owner)
    if args.json:
        print(json.dumps(payload, sort_keys=True))
    else:
        record = payload.get("record") or {}
        detail = ""
        if record:
            age = int(now() - float(record["requested_at"]))
            detail = f" pid={record.get('pid')} owner={record.get('owner') or '-'} age={age}s"
        switch = payload.get("switch") or {}
        if switch:
            detail = f" reason={switch.get('reason', '-')} age={int(now() - float(switch['at']))}s"
        busy = payload.get("busy", 0)
        parked = payload.get("parked", 0)
        print(f"{payload['state']}{detail} busy={busy} parked={parked}")
    return 0 if payload["state"] in {"waiting", "draining", "held", "linger"} else 1


def cmd_disable(args: argparse.Namespace) -> int:
    ROOT.mkdir(parents=True, exist_ok=True)
    atomic_json(DISABLED, {"pid": os.getpid(), "at": now(), "reason": args.reason or "operator"})
    return 0


def cmd_enable(_: argparse.Namespace) -> int:
    try:
        DISABLED.unlink()
    except FileNotFoundError:
        pass
    return 0


def cmd_record_create(args: argparse.Namespace) -> int:
    if disabled_row():
        print("disabled")
        return 0
    record_id = f"{os.getpid()}-{time.time_ns()}"
    path = RECORDS / record_id
    path.mkdir(parents=True, exist_ok=False)
    (path / "events").mkdir()
    atomic_json(
        path / "record.json",
        {
            "id": record_id,
            "pid": args.pid,
            "owner_token": args.owner_token,
            "owner": args.owner,
            "phase": "waiting",
            "requested_at": now(),
            "linger_seconds": args.linger,
            "max_seconds": args.maximum,
            "drain_seconds": args.drain,
            "settle_cpu": args.settle_cpu,
            "settle_sample_seconds": args.settle_sample,
        },
    )
    print(record_id)
    return 0


def update_record(record_id: str, **values: object) -> dict:
    path = RECORDS / record_id / "record.json"
    record = read_json(path)
    if not record:
        raise FileNotFoundError(record_id)
    record.update(values)
    atomic_json(path, record)
    return record


def cmd_barrier(args: argparse.Namespace) -> int:
    draining_at = now()
    record = update_record(args.record, phase="draining", draining_at=draining_at)
    deadline = draining_at + min(
        float(record["drain_seconds"]), float(record["max_seconds"])
    )
    while now() < deadline:
        if disabled_row():
            return 77
        leases = lease_rows()
        if all(row["state"] == "parked" for row in leases):
            barrier_at = time.time()
            update_record(
                args.record,
                phase="held",
                barrier_at=barrier_at,
                snapshot=[row["tag"] for row in leases],
            )
            return 0
        time.sleep(0.1)
    for row in lease_rows():
        if row["state"] != "parked":
            age = int(now() - float(row["created_at"]))
            detail = f"{row['tag']} pid={row['holder_pid']} state={row['state']} age={age}s"
            print(
                f"ir-acquire: QUIET-REFUSED {detail}",
                file=sys.stderr,
            )
    return 75


def coverage(record: dict, released_at: float) -> list[str]:
    failures = []
    deadline = time.time() + 2 * float(record["settle_sample_seconds"]) + 1
    snapshot = set(record.get("snapshot", []))
    while time.time() < deadline:
        by_tag = {row["tag"]: row for row in lease_rows()}
        incomplete = []
        for tag in snapshot:
            row = by_tag.get(tag)
            parkers = row.get("parkers", []) if row else []
            covered = any(
                entry.get("state") == "parked"
                and float(entry.get("quiet_since", released_at + 1)) <= float(record["barrier_at"])
                and float(entry.get("covered_through", 0)) >= released_at
                for entry in parkers
            )
            if not covered:
                incomplete.append(tag)
        if not incomplete:
            break
        time.sleep(0.1)
    failures.extend(f"{tag} uncovered" for tag in incomplete)
    events = Path(record["path"]) / "events"
    for entry in events.glob("*.json"):
        row = read_json(entry)
        if row:
            detail = f"{row.get('tag', '-')} {row.get('kind')} {row.get('detail', '')}"
            failures.append(detail.strip())
    if released_at - float(record["barrier_at"]) >= float(record["max_seconds"]):
        failures.append("- cap")
    if disabled_row():
        failures.append("- disabled")
    return failures


def cmd_finish(args: argparse.Namespace) -> int:
    path = RECORDS / args.record
    record = read_json(path / "record.json")
    if not record:
        return args.command_exit
    record["path"] = str(path)
    released_at = time.time()
    failures = coverage(record, released_at)
    state = "BREACH" if failures else "GUARDED"
    if args.report:
        report = state + "\n" + "\n".join(failures) + ("\n" if failures else "")
        Path(args.report).write_text(report, encoding="utf-8")
    for failure in failures:
        print(f"ir-acquire: QUIET-BREACH {failure}", file=sys.stderr)
    update_record(args.record, phase="released", released_at=now())
    if args.command_exit != 0:
        return args.command_exit
    return 76 if failures else 0


def cmd_refuse(args: argparse.Namespace) -> int:
    if args.report:
        Path(args.report).write_text("REFUSED\n", encoding="utf-8")
    shutil.rmtree(RECORDS / args.record, ignore_errors=True)
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser()
    sub = result.add_subparsers(dest="command", required=True)
    lease_create = sub.add_parser("lease-create")
    lease_create.add_argument("tag")
    lease_create.add_argument("--pid", type=int, required=True)
    lease_create.add_argument("--owner-token", default="")
    lease_create.set_defaults(func=cmd_lease_create)
    lease_drop = sub.add_parser("lease-drop")
    lease_drop.add_argument("tag")
    lease_drop.set_defaults(func=cmd_lease_drop)
    park = sub.add_parser("park")
    park.add_argument("tag")
    park.add_argument("--measurement-root", type=int, default=0)
    park.add_argument("--measurement-root-token", default="")
    park.add_argument("--runtime-root", default="")
    park.add_argument("--launcher-token", default="")
    park.set_defaults(func=cmd_park)
    park_drop = sub.add_parser("park-drop")
    park_drop.add_argument("tag")
    park_drop.add_argument("--launcher-token", default="")
    park_drop.set_defaults(func=cmd_park_drop)
    status = sub.add_parser("status")
    status.add_argument("--owner")
    status.add_argument("--json", action="store_true")
    status.set_defaults(func=cmd_status)
    disable = sub.add_parser("disable")
    disable.add_argument("reason", nargs="?")
    disable.set_defaults(func=cmd_disable)
    enable = sub.add_parser("enable")
    enable.set_defaults(func=cmd_enable)
    create = sub.add_parser("record-create")
    create.add_argument("--pid", type=int, required=True)
    create.add_argument("--owner-token", default="")
    create.add_argument("--owner", default="")
    create.add_argument("--linger", type=float, required=True)
    create.add_argument("--maximum", type=float, required=True)
    create.add_argument("--drain", type=float, required=True)
    create.add_argument("--settle-cpu", type=float, required=True)
    create.add_argument("--settle-sample", type=float, required=True)
    create.set_defaults(func=cmd_record_create)
    barrier = sub.add_parser("barrier")
    barrier.add_argument("record")
    barrier.set_defaults(func=cmd_barrier)
    finish = sub.add_parser("finish")
    finish.add_argument("record")
    finish.add_argument("command_exit", type=int)
    finish.add_argument("--report")
    finish.set_defaults(func=cmd_finish)
    refuse = sub.add_parser("refuse")
    refuse.add_argument("record")
    refuse.add_argument("--report")
    refuse.set_defaults(func=cmd_refuse)
    return result


def main() -> int:
    ROOT.mkdir(parents=True, exist_ok=True)
    RECORDS.mkdir(exist_ok=True)
    LEASES.mkdir(exist_ok=True)
    args = parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
