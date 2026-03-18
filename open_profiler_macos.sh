#!/usr/bin/env bash
set -euo pipefail

dump_file="${1:-}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${script_dir}/build"

profiler_gui=""
if [[ -d "${build_dir}" ]]; then
    profiler_gui="$(find "${build_dir}" -type f -name profiler_gui -perm -111 | head -n 1)"
fi

if [[ -z "${profiler_gui}" ]]; then
    echo "profiler_gui not found under ${build_dir}. Make sure the project has been built with EasyProfiler GUI enabled." >&2
    exit 1
fi

if [[ -z "${dump_file}" ]]; then
    for candidate in \
        "${build_dir}/profiler_dump.prof" \
        "${script_dir}/profiler_dump.prof"; do
        if [[ -f "${candidate}" ]]; then
            dump_file="${candidate}"
            break
        fi
    done
fi

echo "Profiler GUI: ${profiler_gui}"

if [[ -n "${dump_file}" && -f "${dump_file}" ]]; then
    echo "Opening dump:  ${dump_file}"
    exec "${profiler_gui}" "${dump_file}"
fi

if [[ -n "${dump_file}" ]]; then
    echo "Warning: dump file not found: ${dump_file}" >&2
else
    echo "Warning: no profiler_dump.prof found in build/ or project root." >&2
    echo "Run your app first so it writes profiler_dump.prof on exit, then re-run this script." >&2
fi

echo "Launching profiler GUI without a dump file..."
exec "${profiler_gui}"
