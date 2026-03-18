#!/usr/bin/env bash
set -euo pipefail

wait_for_clt() {
    echo "Waiting for Xcode Command Line Tools installation to finish..."
    until xcode-select -p >/dev/null 2>&1; do
        sleep 5
    done
}

ensure_homebrew() {
    if command -v brew >/dev/null 2>&1; then
        return
    fi

    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

    if [[ -x /opt/homebrew/bin/brew ]]; then
        eval "$(/opt/homebrew/bin/brew shellenv)"
    elif [[ -x /usr/local/bin/brew ]]; then
        eval "$(/usr/local/bin/brew shellenv)"
    fi
}

print_exports() {
    local brew_prefix llvm_prefix qt_prefix pkg_config_path
    brew_prefix="$(brew --prefix)"
    llvm_prefix="$(brew --prefix llvm)"
    qt_prefix="$(brew --prefix qt@5)"
    pkg_config_path="${brew_prefix}/lib/pkgconfig:${brew_prefix}/share/pkgconfig:${llvm_prefix}/lib/pkgconfig:${qt_prefix}/lib/pkgconfig"

    cat <<EOF

Add these to your shell profile if they are not already present:
  export PATH="${llvm_prefix}/bin:\$PATH"
  export PKG_CONFIG_PATH="${pkg_config_path}\${PKG_CONFIG_PATH:+:\$PKG_CONFIG_PATH}"
  export CMAKE_PREFIX_PATH="${qt_prefix}\${CMAKE_PREFIX_PATH:+:\$CMAKE_PREFIX_PATH}"
EOF
}

verify_tool() {
    local label="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        echo "[ok] ${label}"
    else
        echo "[warn] ${label}"
    fi
}

if ! xcode-select -p >/dev/null 2>&1; then
    xcode-select --install
    wait_for_clt
fi

ensure_homebrew

brew update
brew install cmake ninja pkg-config ffmpeg llvm qt@5

echo
echo "Verifying toolchain..."
verify_tool "Homebrew ffmpeg pkg-config metadata found" pkg-config --exists libavcodec
verify_tool "clang-format available" "$(brew --prefix llvm)/bin/clang-format" --version
verify_tool "clang-tidy available" "$(brew --prefix llvm)/bin/clang-tidy" --version
verify_tool "Qt5 qmake available" "$(brew --prefix qt@5)/bin/qmake" --version

if xcrun --find metal >/dev/null 2>&1 && xcrun --find metallib >/dev/null 2>&1; then
    echo "[ok] Metal shader tools available via xcrun"
else
    cat <<'EOF'
[warn] Metal shader tools were not found via xcrun.
       Install full Xcode, open it once to finish setup, and select it with:
       sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
EOF
fi

print_exports

echo
echo "Bootstrap complete."
echo "Configure with: cmake --preset macos-debug"
