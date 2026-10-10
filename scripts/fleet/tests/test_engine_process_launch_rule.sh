#!/usr/bin/env bash
# Tests for .claude/rules/engine-process-launches.md — engine executables are
# launched only through ir-run / fleet-run.
#
# The rule's two `fleet-rules-sweep` Detection commands are read verbatim from
# its fenced block and run through the real tool:
#   1. against a throwaway fixture repo — every positive launch shape fires,
#      every non-launch mention stays quiet, and the documented line-based
#      limits are pinned as non-claims;
#   2. against this checkout — both sweeps are a clean pass with non-zero
#      coverage (the deviations register is the negated globs);
#   3. under one mutation per detector arm — each mutated pattern loses the
#      positive case that arm exists for, so the fixture set is proven to
#      depend on it.
# Then the three migrated scripts/dev sweeps run against a stub build tree and
# a logging FLEET_RUN stub, and their launches are compared argv-for-argv.
#
# Every fixture line below is a deliberate positive: this file is registered
# as a live deviation in the rule, and the Detection block excludes it.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib_assert.sh"

REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SWEEP="$SCRIPT_DIR/../fleet-rules-sweep"
RULE="${ENGINE_LAUNCH_RULE:-$REPO_ROOT/.claude/rules/engine-process-launches.md}"

if [[ ! -f "$RULE" ]]; then
    echo "SKIP: rule file not found: $RULE" >&2
    exit 3
fi

TMPROOT="$(mktemp -d "${TMPDIR:-/tmp}/engine-launch-rule.XXXXXX")"
cleanup() { rm -rf "$TMPROOT"; }
trap cleanup EXIT

# ----------------------------------------------------------------------
# The Detection commands, as argv files (one arg per line, LF only).
# ----------------------------------------------------------------------
python3 - "$RULE" "$TMPROOT" <<'PY'
import shlex, sys
from pathlib import Path

rule, out = Path(sys.argv[1]), Path(sys.argv[2])
lines = rule.read_text(encoding="utf-8").replace("\r\n", "\n").split("\n")
section, fence, cmds, cur = False, False, [], ""
for line in lines:
    if line.startswith("## "):
        section = line.strip() == "## Detection"
        continue
    if not section:
        continue
    if line.startswith("```"):
        fence = not fence
        continue
    if not fence:
        continue
    cur += line[:-1] + " " if line.endswith("\\") else line
    if not line.endswith("\\"):
        if cur.strip():
            argv = shlex.split(cur)
            if argv and argv[0] == "fleet-rules-sweep":
                cmds.append(argv)
        cur = ""
for i, argv in enumerate(cmds, 1):
    (out / f"cmd{i}.args").write_text("\n".join(argv) + "\n", encoding="utf-8", newline="\n")
(out / "ncmds").write_text(str(len(cmds)), encoding="utf-8", newline="\n")
PY

ncmds="$(tr -d '\r\n' < "$TMPROOT/ncmds" 2>/dev/null)"
echo "1. the rule's Detection block"
assert_eq "$ncmds" "2" "Detection carries exactly two fleet-rules-sweep commands (shell, python)"
if [[ "$ncmds" != "2" ]]; then
    summarize "engine-process-launch rule tests"
    exit $?
fi
mapfile -t SHELL_CMD < <(tr -d '\r' < "$TMPROOT/cmd1.args")
mapfile -t PY_CMD < <(tr -d '\r' < "$TMPROOT/cmd2.args")

pattern_of() {  # pattern_of <argv...> -> the --pattern value
    local prev=""
    for a in "$@"; do
        [[ "$prev" == "--pattern" ]] && { printf '%s' "$a"; return; }
        prev="$a"
    done
}
SHELL_PAT="$(pattern_of "${SHELL_CMD[@]}")"
PY_PAT="$(pattern_of "${PY_CMD[@]}")"
[[ -n "$SHELL_PAT" ]] && ok "shell command carries a --pattern" || bad "shell command has no --pattern"
[[ -n "$PY_PAT" ]] && ok "python command carries a --pattern" || bad "python command has no --pattern"

for self in ".claude/rules/engine-process-launches.md" "scripts/fleet/tests/test_engine_process_launch_rule.sh"; do
    assert_contains "$(printf '%s\n' "${SHELL_CMD[@]}")" "!$self" "shell sweep registers $self"
    assert_contains "$(printf '%s\n' "${PY_CMD[@]}")" "!$self" "python sweep registers $self"
done
assert_contains "$(sed -n '/^## Live deviations/,$p' "$RULE")" "scripts/fleet/tests/test_engine_process_launch_rule.sh" \
    "Live deviations names this suite"

# run_with_pattern <argv-array-name> <pattern> <repo-root> -> --files-only output
run_with_pattern() {
    local -n _argv="$1"
    local pat="$2" root="$3" out=() prev=""
    for a in "${_argv[@]:1}"; do
        if [[ "$prev" == "--pattern" ]]; then out+=("$pat"); else out+=("$a"); fi
        prev="$a"
    done
    "$SWEEP" --repo-root "$root" --files-only "${out[@]}" 2>/dev/null | tr -d '\r'
}

# ----------------------------------------------------------------------
# Fixture repo: one case per file, so a hit names its case.
# ----------------------------------------------------------------------
FIX="$TMPROOT/fixture"
mkdir -p "$FIX/scripts/pos" "$FIX/scripts/neg" "$FIX/scripts/limit" "$FIX/.claude/skills/demo"
w() { printf '%s\n' "$2" > "$FIX/$1"; }

# Shell / Markdown positives.
w scripts/pos/subshell_dot_slash.sh   '( cd "$EXE_DIR" && ./IRShapeDebug --spin-shape box )'
w scripts/pos/line_start.sh           './IRShapeDebug --auto-screenshot 5'
w scripts/pos/build_tree_exe.sh       'build/creations/demos/shape_debug/IRShapeDebug.exe --auto-screenshot 5'
w scripts/pos/bare_exe.sh             'cd build/creations/demos/shape_debug && IRShapeDebug.exe --auto-screenshot 5'
w scripts/pos/windows_path_exe.sh     'C:\build\creations\demos\shape_debug\IRShapeDebug.exe --auto-screenshot 5'
w scripts/pos/var_prefixed_path.sh    '"$BUILD/creations/demos/shape_debug/IRShapeDebug" --zoom 4'
w scripts/pos/after_wrapper.sh        'fleet-run IRShapeDebug --timeout 5 && ./IRShapeDebug --zoom 4'
w scripts/pos/timeout_prefix.sh       'timeout 30 ./IRShapeDebug --auto-screenshot 5'
w .claude/skills/demo/SKILL.md        'Run `./IRShapeDebug --auto-screenshot 5` from the exe dir.'

# Python argv positives.
w scripts/pos/py_run_dot_slash.py     'subprocess.run(["./IRShapeDebug", "--auto-screenshot", "5"])'
w scripts/pos/py_run_exe.py           "subprocess.run(['build/IRShapeDebug.exe', '--zoom', '4'])"
w scripts/pos/py_popen_bare.py        'proc = subprocess.Popen(["IRShapeDebug", "--zoom", "4"])'
w scripts/pos/py_list_literal.py      'cmd = ["./IRPerfGrid", "--zoom", "4"]'

# Negatives: wrapper arguments, target names, identifiers, process tables,
# path mentions.
w scripts/neg/fleet_run.sh            'fleet-run IRShapeDebug --auto-screenshot 5'
w scripts/neg/fleet_run_build_dir.sh  'fleet-run --build-dir "$BUILD_DIR" IRShapeDebug --spin-shape box'
w scripts/neg/ir_run_timeout.sh       'ir-run --timeout 5 IRShapeDebug'
w scripts/neg/ir_run_path.sh          'ir-run ./IRShapeDebug --zoom 4'
w scripts/neg/cmake_target.sh         'cmake --build build --target IRShapeDebug -j 8'
w scripts/neg/add_executable.md       'add_executable(IRYourCreation main.cpp)'
w scripts/neg/api_identifier.py       'mgr = IRRender::getRenderManager()'
w scripts/neg/label_list.py           'unsafe_base_reason(["IRRender", "fleet:changes-made"])'
w scripts/neg/process_table.py        'table = "10 1 /bin/sh\n12 11 ./IRPerfGrid"'
w scripts/neg/process_table_line.txt  '12 11 ./IRPerfGrid'
w scripts/neg/path_arg.py             'find_demo_pid(10, Path("/repo/build/IRPerfGrid"), table)'
w scripts/neg/path_mention.md         'the binary lands at `<wt>/build/creations/demos/x/IRRepositionStress` (15.7 MB)'
w scripts/neg/echo_target.sh          'echo "[1/3] building IRShapeDebug ($BUILD_DIR) ..."'
w scripts/neg/exe_dir_assign.sh       'EXE_DIR="$BUILD_DIR/creations/demos/shape_debug"'
w scripts/neg/wrapper_seam.sh         '"$FLEET_RUN" --build-dir "$BUILD_DIR" IRCanvasStress --no-spin'

# Documented detector limits: real violations a line-based sweep cannot see.
# (A path-qualified element alone on a line reads as a quoted command-position
# path to the shell sweep; a bare name carries no signal on its own line.)
printf '%s\n' 'subprocess.run(' '    [' '        "IRShapeDebug",' '        "--zoom",' '    ]' ')' \
    > "$FIX/scripts/limit/py_multiline_argv.py"
w scripts/limit/py_variable_exe.py    'subprocess.run([exe, "--auto-screenshot", "5"])'
w scripts/limit/shell_variable_exe.sh '"$EXE" --auto-screenshot 5'

git -C "$FIX" init --quiet
git -C "$FIX" config user.email "test@example.com"
git -C "$FIX" config user.name "test"
git -C "$FIX" add -A
git -C "$FIX" commit --quiet -m fixture

SHELL_HITS="$(run_with_pattern SHELL_CMD "$SHELL_PAT" "$FIX")"
PY_HITS="$(run_with_pattern PY_CMD "$PY_PAT" "$FIX")"
ALL_HITS="$(printf '%s\n%s\n' "$SHELL_HITS" "$PY_HITS")"

echo "2. positives fire"
for f in subshell_dot_slash.sh line_start.sh build_tree_exe.sh bare_exe.sh windows_path_exe.sh \
         var_prefixed_path.sh after_wrapper.sh timeout_prefix.sh; do
    assert_contains "$SHELL_HITS" "scripts/pos/$f" "shell sweep fires: $f"
done
assert_contains "$SHELL_HITS" ".claude/skills/demo/SKILL.md" "shell sweep fires: inline-code launch in Markdown"
for f in py_run_dot_slash.py py_run_exe.py py_popen_bare.py py_list_literal.py; do
    assert_contains "$PY_HITS" "scripts/pos/$f" "python sweep fires: $f"
done

echo "3. non-launches stay quiet"
for f in fleet_run.sh fleet_run_build_dir.sh ir_run_timeout.sh ir_run_path.sh cmake_target.sh \
         add_executable.md api_identifier.py label_list.py process_table.py process_table_line.txt \
         path_arg.py path_mention.md echo_target.sh exe_dir_assign.sh wrapper_seam.sh; do
    assert_absent "$ALL_HITS" "scripts/neg/$f" "no sweep fires: $f"
done

echo "4. documented limits are non-claims"
for f in py_multiline_argv.py py_variable_exe.py shell_variable_exe.sh; do
    assert_absent "$ALL_HITS" "scripts/limit/$f" "line-based sweep cannot see: $f"
done

echo "5. this checkout is a clean pass"
for which in SHELL_CMD PY_CMD; do
    declare -n cmd="$which"
    tree_err="$("$SWEEP" --repo-root "$REPO_ROOT" "${cmd[@]:1}" 2>&1 >"$TMPROOT/tree.out")"
    tree_rc=$?
    tree_out="$(tr -d '\r' < "$TMPROOT/tree.out")"
    assert_eq "$tree_rc" "1" "$which: exact Detection command exits 1 (clean) over scripts + .claude"
    [[ -n "$tree_out" ]] && echo "$tree_out" | sed 's/^/          unregistered: /'
    swept="$(printf '%s' "$tree_err" | sed -n 's/^-- swept \([0-9]*\) file.*/\1/p' | tr -d '\r')"
    if [[ "${swept:-0}" -gt 0 ]]; then
        ok "$which: coverage is non-zero ($swept files)"
    else
        bad "$which: no coverage line — a zero-file sweep is not a clean pass"
    fi
    unset -n cmd
done

echo "6. mutation controls: each arm is load-bearing"
# M1: a line-level wrapper exemption instead of command position.
M1_PAT='^(?!.*\b(?:fleet|ir)-run\b)'"$SHELL_PAT"
m1="$(run_with_pattern SHELL_CMD "$M1_PAT" "$FIX")"
assert_contains "$SHELL_HITS" "scripts/pos/after_wrapper.sh" "M1 control: real pattern catches a direct launch after a wrapper"
assert_absent "$m1" "scripts/pos/after_wrapper.sh" "M1: broadened wrapper exemption loses after_wrapper.sh"
assert_absent "$m1" "scripts/neg/fleet_run.sh" "M1 is still a wrapper exemption (not a vacuous mutation)"

# M2: drop the .exe arm (the optional suffix and the bare-.exe alternative).
M2_PAT="${SHELL_PAT//'(?:\.exe)?'/}"
M2_PAT="${M2_PAT//'|IR[A-Z][A-Za-z0-9]*\.exe'/}"
if [[ "$M2_PAT" == "$SHELL_PAT" ]]; then
    bad "M2 mutation did not apply — the shell pattern's .exe arm changed shape; update this control"
else
    m2="$(run_with_pattern SHELL_CMD "$M2_PAT" "$FIX")"
    for f in build_tree_exe.sh bare_exe.sh windows_path_exe.sh; do
        assert_absent "$m2" "scripts/pos/$f" "M2: removed .exe arm loses $f"
    done
    assert_contains "$m2" "scripts/pos/line_start.sh" "M2 still fires on an extensionless launch"
fi

# M3: drop the Python-argv sweep; the shell sweep alone must miss every argv case.
m3="$(run_with_pattern SHELL_CMD "$SHELL_PAT" "$FIX")"
for f in py_run_dot_slash.py py_run_exe.py py_popen_bare.py py_list_literal.py; do
    assert_absent "$m3" "scripts/pos/$f" "M3: without the python sweep, $f escapes"
done

# ----------------------------------------------------------------------
# The migrated dev sweeps launch through FLEET_RUN with every original arg.
# ----------------------------------------------------------------------
echo "7. scripts/dev sweeps launch through fleet-run"
STUB_BIN="$TMPROOT/stub-bin"
BUILD="$TMPROOT/build"
RUN_LOG="$TMPROOT/fleet-run.log"
mkdir -p "$STUB_BIN" "$BUILD"
printf '#!/usr/bin/env bash\nexit 0\n' > "$STUB_BIN/cmake"
printf '#!/usr/bin/env bash\nprintf "%%s|" "$@" >> "%s"\necho >> "%s"\nexit 0\n' "$RUN_LOG" "$RUN_LOG" \
    > "$STUB_BIN/fleet-run-stub"
chmod +x "$STUB_BIN/cmake" "$STUB_BIN/fleet-run-stub"

run_dev() {  # run_dev <VAR=val>... <script> <args...>; the script's own output is irrelevant
    local envs=()
    while [[ "$1" == *=* ]]; do envs+=("$1"); shift; done
    : > "$RUN_LOG"
    env PATH="$STUB_BIN:$PATH" FLEET_RUN="$STUB_BIN/fleet-run-stub" ${envs[@]+"${envs[@]}"} \
        bash "$REPO_ROOT/scripts/dev/$1" "${@:2}" >/dev/null 2>&1
    tr -d '\r' < "$RUN_LOG"
}

shape_log="$(run_dev RENDER=both SPIN_RATE=30 shape-rotate-jitter-sweep "$BUILD" box 8 3)"
assert_eq "$shape_log" \
"--build-dir|$BUILD|IRShapeDebug|--spin-shape|box|--spin-yaw|30|--auto-screenshot|3|--zoom|8|
--build-dir|$BUILD|IRShapeDebug|--spin-shape|box|--spin-shape-voxel|--spin-yaw|30|--auto-screenshot|3|--zoom|8|" \
    "shape-rotate-jitter-sweep: sdf + voxel runs, original args"

perf_log="$(run_dev perf-grid-rotate-sweep "$BUILD" dense 80)"
assert_eq "$perf_log" \
"--build-dir|$BUILD|IRPerfGrid|--mode|dense|--yaw-ramp|--no-overlay|--auto-screenshot|80|--grid-size|64|--zoom|0.8|
--build-dir|$BUILD|IRPerfGrid|--mode|dense|--yaw-ramp|--no-overlay|--auto-screenshot|80|--grid-size|12|--zoom|4|--yaw-ramp-crops|
--build-dir|$BUILD|IRPerfGrid|--mode|voxel_set|--wave-freeze|--grid-size|32|--zoom|0.8|--yaw-ramp|--yaw-ramp-wave|--no-overlay|--auto-screenshot|80|" \
    "perf-grid-rotate-sweep: coverage, zoom and wave-freeze runs, original args"

canvas_log="$(run_dev WARMUP=60 canvas-stress-rotate-jitter-sweep "$BUILD" 0 0.2618 36)"
assert_eq "$canvas_log" \
"--build-dir|$BUILD|IRCanvasStress|--sweep-yaw|0|0.2618|36|--no-spin|--auto-screenshot|60|" \
    "canvas-stress-rotate-jitter-sweep: one sweep run, original args"

summarize "engine-process-launch rule tests"
