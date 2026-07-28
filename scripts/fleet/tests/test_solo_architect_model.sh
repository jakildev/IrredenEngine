#!/usr/bin/env bash
# Tests for solo-architect's standalone model resolution — the
# MODEL="${FLEET_MODEL_FABLE:-${FLEET_FABLE_CANDIDATES_DEFAULT[0]:-fable[1m]}}"
# line, the fable-class sibling of fleet-dispatcher's standalone default
# resolution (test_dispatcher_class_dispatch.sh T19 covers the dispatcher arm).
#
# solo-architect has no non-interactive print seam, so the resolution is
# observed by intercepting its final `exec claude --model <MODEL> ...` with a
# PATH-stubbed `claude` that echoes only the resolved --model value and exits.
# Hermetic: HOME and FLEET_ENGINE_ROOT redirect into a tmp tree so the real
# ~/.fleet/fleet-up.conf and session sidecar are never touched; --no-scout
# skips the scout tick and --fresh skips reading a persisted session.
#
# Covers:
#   - FLEET_MODEL_FABLE unset, no conf -> fleet-common.sh alias default (fable[1m])
#   - FLEET_MODEL_FABLE pinned in env -> honors the pin
#   - FLEET_MODEL_FABLE unset, ~/.fleet/fleet-up.conf pins it -> honors the pin
#   - game-architect shares the same MODEL= line (alias default when unset)

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
SOLO="$SCRIPT_DIR/solo-architect"

if [[ ! -x "$SOLO" ]]; then
    echo "test setup: solo-architect not found at $SOLO" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

TMPROOT=$(mktemp -d)
mkdir -p "$TMPROOT/bin" "$TMPROOT/home/.fleet" \
    "$TMPROOT/engine/.claude/worktrees/opus-architect" \
    "$TMPROOT/engine/creations/game/.claude/worktrees/game-architect"

# Stub claude: solo-architect exec's `claude --model <MODEL> ...`; print only
# the resolved model value and exit before anything real launches.
cat > "$TMPROOT/bin/claude" <<'EOF'
#!/usr/bin/env bash
while [[ $# -gt 0 ]]; do
    if [[ "$1" == "--model" ]]; then echo "MODEL=$2"; exit 0; fi
    shift
done
echo "MODEL=<none>"; exit 0
EOF
chmod +x "$TMPROOT/bin/claude"

# Resolve the model solo-architect would launch with. $@ = extra env
# assignments / `env -u VAR` tokens, then the solo-architect argv.
resolve_model() {
    env HOME="$TMPROOT/home" FLEET_ENGINE_ROOT="$TMPROOT/engine" \
        PATH="$TMPROOT/bin:$PATH" "$@" 2>/dev/null
}

# --- T1: unset + no conf -> alias default ------------------------------------
echo "T1: FLEET_MODEL_FABLE unset (no conf) resolves to the alias default"
rm -f "$TMPROOT/home/.fleet/fleet-up.conf"
assert_eq "$(resolve_model env -u FLEET_MODEL_FABLE "$SOLO" --no-scout --fresh)" \
    "MODEL=fable[1m]" \
    "unpinned engine architect -> FLEET_FABLE_CANDIDATES_DEFAULT[0]=fable[1m]"

# --- T2: env pin wins --------------------------------------------------------
echo "T2: FLEET_MODEL_FABLE env pin is honored"
assert_eq "$(resolve_model FLEET_MODEL_FABLE='opus[1m]' "$SOLO" --no-scout --fresh)" \
    "MODEL=opus[1m]" \
    "FLEET_MODEL_FABLE=opus[1m] pins the model (fable ladder lost to opus floor)"

# --- T3: conf pin used when env is unset -------------------------------------
echo "T3: unset env falls back to ~/.fleet/fleet-up.conf pin"
printf "FLEET_MODEL_FABLE='claude-fable-5[1m]'\n" > "$TMPROOT/home/.fleet/fleet-up.conf"
assert_eq "$(resolve_model env -u FLEET_MODEL_FABLE "$SOLO" --no-scout --fresh)" \
    "MODEL=claude-fable-5[1m]" \
    "conf-file FLEET_MODEL_FABLE sourced when env is unset"
rm -f "$TMPROOT/home/.fleet/fleet-up.conf"

# --- T4: game-architect shares the same resolution ---------------------------
echo "T4: game-architect uses the same MODEL= line"
assert_eq "$(resolve_model env -u FLEET_MODEL_FABLE "$SOLO" game --no-scout --fresh)" \
    "MODEL=fable[1m]" \
    "unpinned game architect -> same fleet-common.sh alias default"

summarize "solo-architect model-resolution tests"
