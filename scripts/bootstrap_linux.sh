#!/usr/bin/env bash
# Bootstrap a Debian/Ubuntu Linux host for building Irreden Engine.
#
# This mirrors scripts/bootstrap_macos.sh. Run once after cloning to install
# the system-level toolchain and the dev headers that FetchContent-built
# dependencies (GLFW, RtAudio, RtMidi, easy_profiler Qt GUI, FFmpeg) need.
#
# The engine itself is built from source via CMake + FetchContent, so the
# only things we install here are:
#   - a C/C++ toolchain + cmake + ninja + pkg-config
#   - OpenGL, X11 extensions, and libxkbcommon for GLFW
#   - ALSA for RtAudio / RtMidi
#   - FFmpeg dev libraries for IrredenEngineVideo
#   - Qt5 (optional) for the easy_profiler GUI target
#   - clang-format + clang-tidy for the quality targets
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
    cat <<'EOF'
bootstrap_linux.sh currently only supports Debian/Ubuntu-family distributions
(apt-get). On other distros, install the equivalents of the package list in
this script manually and then re-run `cmake --preset linux-debug`.
EOF
    exit 1
fi

SUDO=""
if [[ ${EUID} -ne 0 ]]; then
    SUDO="sudo"
fi

${SUDO} apt-get update

${SUDO} apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    ca-certificates \
    libgl1-mesa-dev \
    libx11-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libxext-dev \
    libxkbcommon-dev \
    libwayland-dev \
    libasound2-dev \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    clang-format \
    clang-tidy

# Qt5 is optional and only used by the easy_profiler GUI. Install on a
# best-effort basis so a missing package on newer Ubuntu (which renamed
# qtbase5-dev) does not fail the whole bootstrap.
${SUDO} apt-get install -y qtbase5-dev qttools5-dev-tools || \
    echo "[warn] qtbase5-dev not installed; easy_profiler GUI will be disabled."

verify_tool() {
    local label="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        echo "[ok] ${label}"
    else
        echo "[warn] ${label}"
    fi
}

echo
echo "Verifying toolchain..."
verify_tool "cmake available" cmake --version
verify_tool "g++ available" g++ --version
verify_tool "pkg-config finds libavcodec" pkg-config --exists libavcodec
verify_tool "pkg-config finds alsa" pkg-config --exists alsa
verify_tool "pkg-config finds gl" pkg-config --exists gl
verify_tool "clang-format available" clang-format --version
verify_tool "clang-tidy available" clang-tidy --version

echo
echo "Bootstrap complete."
echo "Configure with: cmake --preset linux-debug"
echo "Build a demo with: cmake --build build --target IRShapeDebug"
