#!/usr/bin/env bash
# Executed control for the hosted gate's both-target watchdog behavior.

set -u

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
MATRIX="$REPO_ROOT/scripts/perf/perf_grid_matrix.sh"
CHECK="$REPO_ROOT/scripts/perf/check_regression.py"
LAB="$(mktemp -d "${TMPDIR:-/tmp}/perf-both-target.XXXXXX")"
trap 'rm -rf "$LAB"' EXIT

ROOT="$LAB/root"
mkdir -p "$ROOT" "$LAB/bin"
touch "$ROOT/CMakePresets.json"
git init -q "$ROOT"
git -C "$ROOT" add CMakePresets.json
git -C "$ROOT" -c user.email=perf-test@example.com -c user.name="perf test" \
  commit -q -m root
SHA="$(git -C "$ROOT" rev-parse --short HEAD)"
export FLEET_ENGINE_ROOT="$ROOT"
export STUB_ENGINE_ROOT="$ROOT"

cat > "$LAB/bin/fleet-run" <<'STUB'
#!/usr/bin/env bash
shift 2
target="$1"
if [[ "$target" == "IRLuaPerfGrid" && "${STUB_LUA_TIMEOUT:-0}" == 1 ]]; then
  exit 124
fi
dir="$STUB_ENGINE_ROOT/build/stub/save_files"
mkdir -p "$dir"
printf 'Frame time: avg=1.0ms p50=1.0ms p95=1.0ms p99=1.0ms min=1.0ms max=1.0ms\n' \
  > "$dir/profile_report.txt"
STUB
chmod +x "$LAB/bin/fleet-run"
export PATH="$LAB/bin:$PATH"

run_matrix () {
  local label="$1" timeout="$2"
  (cd "$ROOT" && STUB_LUA_TIMEOUT="$timeout" "$MATRIX" --quick --frames 1 \
    --target both --label "$label") > "$LAB/$label.out" 2>&1
}

echo "both-target no-report control"

if ! run_matrix measured 0; then
  echo "FAIL mutation control: measured both-target matrix failed"
  exit 1
fi
BASELINE="$ROOT/save_files/perf/$SHA-measured"
python3 - "$BASELINE/manifest.json" <<'PY'
import json
import sys

cells = json.load(open(sys.argv[1]))["cells"]
assert len(cells) == 4
assert {cell["target"] for cell in cells} == {"IRPerfGrid", "IRLuaPerfGrid"}
assert all(cell["status"] == "ok" and cell["exit_status"] == 0 for cell in cells)
PY
echo "  PASS mutation control writes four measured cells"

if run_matrix timeout 1; then
  echo "FAIL watchdog control: timeout matrix exited zero"
  exit 1
fi
HEAD="$ROOT/save_files/perf/$SHA-timeout"
python3 - "$HEAD/manifest.json" <<'PY'
import json
import sys

cells = json.load(open(sys.argv[1]))["cells"]
lua = [cell for cell in cells if cell["target"] == "IRLuaPerfGrid"]
cpp = [cell for cell in cells if cell["target"] == "IRPerfGrid"]
assert len(lua) == 2 and len(cpp) == 2
assert all(cell["status"] == "no_report" and cell["exit_status"] == 124 for cell in lua)
assert all(cell["status"] == "ok" and cell["exit_status"] == 0 for cell in cpp)
PY
echo "  PASS watchdog records only Lua cells as no_report/124"

set +e
python3 "$CHECK" "$BASELINE" "$HEAD" > "$LAB/check.out" 2> "$LAB/check.err"
RC=$?
set -e
if [[ "$RC" -ne 2 ]]; then
  echo "FAIL comparator: expected exit 2, got $RC"
  exit 1
fi
if ! grep -q 'unmeasured cells in head: target=IRLuaPerfGrid' "$LAB/check.err"; then
  echo "FAIL comparator: Lua unmeasured diagnostic missing"
  exit 1
fi
echo "  PASS comparator exits 2 before producing a perf verdict"
