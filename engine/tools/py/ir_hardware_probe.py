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
import platform
import re
import subprocess
import sys
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
    except (OSError, subprocess.TimeoutExpired):
        # OSError catches FileNotFoundError (binary missing from PATH) and
        # PermissionError (binary exists but is not exec'able — happens on
        # WSL2 hosts when PATH contains Windows-side stubs that match a
        # Linux tool name but cannot be invoked from a Linux subprocess).
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
    return {"kernel": kernel, "distro": distro}


def _slug(probe):
    parts = [
        platform.system().lower(),
        platform.machine().lower(),
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
    s = re.sub(r"\b(amd|intel|nvidia|apple|corporation|inc|ltd|co|with radeon graphics)\b",
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
