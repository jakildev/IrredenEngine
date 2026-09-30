#!/usr/bin/env python3
"""Detect the host's CPU, GPU, OS, and RAM; emit a deterministic JSON
fingerprint plus a short slug.

Output schema (stable — `host.json` sidecars in docs/perf/baseline_latest/
read these keys directly):

    {
      "slug": "linux-x86_64-ryzen-7950x-rtx-4080",
      "cpu":  {"model": "...", "cores": 16, "threads": 32, "mhz": 4500},
      "gpu":  {"model": "..."},
      "os":   {"kernel": "Linux 6.5.0", "distro": "Ubuntu 24.04"},
      "ram":  {"gb": 64}
    }

Determinism: the JSON content for a given machine must not change between
invocations (no timestamps, no uptime-derived values, no random ordering).
"""

import json
import os
import platform
import re
import subprocess
from pathlib import Path


def _run(*args, default=""):
    try:
        out = subprocess.run(
            args,
            capture_output=True,
            text=True,
            check=False,
            timeout=5,
        )
        return out.stdout.strip()
    except (FileNotFoundError, PermissionError, subprocess.TimeoutExpired):
        # FileNotFoundError: binary missing from PATH.
        # PermissionError: binary exists but is not exec'able — happens on
        # WSL2 hosts when PATH contains Windows-side stubs that match a
        # Linux tool name but cannot be invoked from a Linux subprocess.
        return default


def _cpu_linux():
    info = {"model": "unknown", "cores": 0, "threads": 0, "mhz": 0}
    try:
        text = Path("/proc/cpuinfo").read_text()
    except (FileNotFoundError, PermissionError):
        return info
    model_match = re.search(r"^model name\s*:\s*(.+)$", text, re.MULTILINE)
    if model_match:
        info["model"] = model_match.group(1).strip()
    info["threads"] = len(re.findall(r"^processor\s*:", text, re.MULTILINE))
    core_match = re.search(r"^cpu cores\s*:\s*(\d+)", text, re.MULTILINE)
    if core_match:
        info["cores"] = int(core_match.group(1))
    mhz_match = re.search(r"^cpu MHz\s*:\s*([\d.]+)", text, re.MULTILINE)
    if mhz_match:
        info["mhz"] = int(float(mhz_match.group(1)))
    return info


def _cpu_macos():
    info = {"model": "unknown", "cores": 0, "threads": 0, "mhz": 0}
    model = _run("sysctl", "-n", "machdep.cpu.brand_string")
    if model:
        info["model"] = model
    cores = _run("sysctl", "-n", "hw.physicalcpu")
    threads = _run("sysctl", "-n", "hw.logicalcpu")
    freq = _run("sysctl", "-n", "hw.cpufrequency_max")
    if cores.isdigit():
        info["cores"] = int(cores)
    if threads.isdigit():
        info["threads"] = int(threads)
    if freq.isdigit():
        info["mhz"] = int(freq) // 1_000_000
    return info


def _gpu_linux():
    out = _run("lspci", "-nn")
    if not out:
        return {"model": "unknown"}
    # Apple-style: first VGA / 3D / Display line is the primary GPU.
    for line in out.splitlines():
        if re.search(r"(VGA compatible controller|3D controller|Display controller)",
                     line):
            # Strip the leading bus address and the trailing [vendor:device] tag.
            tail = line.split(":", 1)[-1].strip()
            tail = re.sub(r"\s*\[[0-9a-fA-F:]+\]\s*$", "", tail)
            return {"model": tail}
    return {"model": "unknown"}


def _gpu_macos():
    out = _run("system_profiler", "SPDisplaysDataType")
    if not out:
        return {"model": "unknown"}
    # Chipset Model: ...
    match = re.search(r"Chipset Model:\s*(.+)$", out, re.MULTILINE)
    if match:
        return {"model": match.group(1).strip()}
    return {"model": "unknown"}


_WIN_CPU_KEY = r"HARDWARE\DESCRIPTION\System\CentralProcessor\0"
# The display-adapter device class; each numbered subkey is one adapter.
_WIN_DISPLAY_CLASS_KEY = (
    r"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"
)


def _win_physical_cores():
    import ctypes
    from ctypes import wintypes

    # SYSTEM_LOGICAL_PROCESSOR_INFORMATION: ULONG_PTR mask, int relationship,
    # then a 16-byte union — one record per relation, RelationProcessorCore = 0.
    class _Info(ctypes.Structure):
        _fields_ = [("mask", ctypes.c_size_t),
                    ("relationship", ctypes.c_int),
                    ("union", ctypes.c_ubyte * 16)]

    fn = ctypes.windll.kernel32.GetLogicalProcessorInformation
    size = wintypes.DWORD(0)
    fn(None, ctypes.byref(size))
    if size.value == 0:
        return 0
    buf = (_Info * (size.value // ctypes.sizeof(_Info)))()
    if not fn(buf, ctypes.byref(size)):
        return 0
    return sum(1 for rec in buf if rec.relationship == 0)


def _cpu_windows():
    import winreg

    info = {"model": "unknown", "cores": 0, "threads": os.cpu_count() or 0, "mhz": 0}
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, _WIN_CPU_KEY) as key:
            info["model"] = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
            info["mhz"] = int(winreg.QueryValueEx(key, "~MHz")[0])
    except OSError:
        pass
    try:
        info["cores"] = _win_physical_cores()
    except (OSError, AttributeError):
        pass
    return info


def _gpu_windows():
    import winreg

    # Adapter subkeys are "0000", "0001", ...; the lowest-numbered one with a
    # description is the primary adapter. Sibling non-numeric subkeys
    # ("Configuration", "Properties") carry no DriverDesc and are unreadable.
    try:
        cls = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, _WIN_DISPLAY_CLASS_KEY)
    except OSError:
        return {"model": "unknown"}
    with cls:
        names = []
        i = 0
        while True:
            try:
                names.append(winreg.EnumKey(cls, i))
            except OSError:
                break
            i += 1
        for name in sorted(n for n in names if n.isdigit()):
            try:
                with winreg.OpenKey(cls, name) as sub:
                    desc = winreg.QueryValueEx(sub, "DriverDesc")[0].strip()
            except OSError:
                continue
            if desc and "basic display" not in desc.lower():
                return {"model": desc}
    return {"model": "unknown"}


def _ram_windows():
    import ctypes

    class _MemStatus(ctypes.Structure):
        _fields_ = [("dwLength", ctypes.c_ulong),
                    ("dwMemoryLoad", ctypes.c_ulong),
                    ("ullTotalPhys", ctypes.c_ulonglong),
                    ("ullAvailPhys", ctypes.c_ulonglong),
                    ("ullTotalPageFile", ctypes.c_ulonglong),
                    ("ullAvailPageFile", ctypes.c_ulonglong),
                    ("ullTotalVirtual", ctypes.c_ulonglong),
                    ("ullAvailVirtual", ctypes.c_ulonglong),
                    ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]

    status = _MemStatus()
    status.dwLength = ctypes.sizeof(_MemStatus)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        return {"gb": 0}
    return {"gb": status.ullTotalPhys // (1024 ** 3)}


def _ram_linux():
    try:
        text = Path("/proc/meminfo").read_text()
    except (FileNotFoundError, PermissionError):
        return {"gb": 0}
    match = re.search(r"^MemTotal:\s+(\d+)\s+kB", text, re.MULTILINE)
    if not match:
        return {"gb": 0}
    return {"gb": int(match.group(1)) // (1024 * 1024)}


def _ram_macos():
    bytes_str = _run("sysctl", "-n", "hw.memsize")
    if not bytes_str.isdigit():
        return {"gb": 0}
    return {"gb": int(bytes_str) // (1024 ** 3)}


def _os_info():
    kernel = f"{platform.system()} {platform.release()}"
    distro = ""
    if platform.system() == "Linux":
        # /etc/os-release is the canonical Linux source; fall back to uname.
        try:
            for line in Path("/etc/os-release").read_text().splitlines():
                if line.startswith("PRETTY_NAME="):
                    distro = line.split("=", 1)[1].strip().strip('"')
                    break
        except (FileNotFoundError, PermissionError):
            pass
    elif platform.system() == "Darwin":
        version = _run("sw_vers", "-productVersion")
        if version:
            distro = f"macOS {version}"
    elif platform.system() == "Windows":
        distro = f"Windows {platform.version()}"
    return {"kernel": kernel, "distro": distro}


# Windows reports x86-64 as "AMD64"; one spelling keeps slugs comparable
# across OSes.
_MACHINE_ALIASES = {"amd64": "x86_64"}


def _slug(probe):
    machine = platform.machine().lower()
    parts = [
        platform.system().lower(),
        _MACHINE_ALIASES.get(machine, machine),
        # Drop vendor noise and shorten — "AMD Ryzen 9 7950X 16-Core" → "ryzen-9-7950x".
        _slugify(probe["cpu"]["model"]),
        _slugify(probe["gpu"]["model"]),
    ]
    # Filter empty pieces so the slug doesn't end up with double dashes.
    return "-".join(p for p in parts if p)


def _slugify(s):
    if not s or s == "unknown":
        return "unknown"
    # Aggressive normalization: lowercase, strip common vendor tokens,
    # collapse runs of non-alphanumerics to single dashes.
    s = s.lower()
    s = re.sub(r"\b(amd|intel|nvidia|geforce|apple|corporation|inc|ltd|co|with radeon graphics)\b",
               "", s)
    s = re.sub(r"\(r\)|\(tm\)", "", s)
    s = re.sub(r"[^a-z0-9]+", "-", s)
    s = re.sub(r"-+", "-", s).strip("-")
    # Keep the first 3 hyphen-separated chunks — enough for identity, short
    # enough for filenames.
    chunks = s.split("-")[:3]
    return "-".join(chunks)


def probe():
    system = platform.system()
    if system == "Linux":
        cpu, gpu, ram = _cpu_linux(), _gpu_linux(), _ram_linux()
    elif system == "Darwin":
        cpu, gpu, ram = _cpu_macos(), _gpu_macos(), _ram_macos()
    elif system == "Windows":
        cpu, gpu, ram = _cpu_windows(), _gpu_windows(), _ram_windows()
    else:
        cpu = {"model": "unknown", "cores": 0, "threads": 0, "mhz": 0}
        gpu = {"model": "unknown"}
        ram = {"gb": 0}
    result = {
        "cpu": cpu,
        "gpu": gpu,
        "os": _os_info(),
        "ram": ram,
    }
    result["slug"] = _slug(result)
    return result


def main():
    print(json.dumps(probe(), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
