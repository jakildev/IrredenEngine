---
paths:
  - "scripts/**"
  - ".claude/**"
---

> **Sweeping for violations?** `paths:` is an injection scope, not a search
> root. `rg`/`Grep` rooted at `.claude/` reads a **false clean**, so run the
> detectors below through `fleet-rules-sweep`. See [`README.md`](README.md).

# Engine executables launch only through `ir-run` / `fleet-run`

Rule:

> A script, skill, role, rule, or instruction example under `scripts/**` or
> `.claude/**` starts an engine executable **only** through `ir-run` or its
> `fleet-run` shim. It never runs the binary by path, never uses a
> `cd <exe-dir> && ./<exe>` subshell, and never passes a Python argv whose
> first element is the executable.

Naming an `IR*` build target, CMake target, or API identifier is not a launch
and is always allowed.

The wrapper is load-bearing. On the native-Windows NVIDIA host, two engine
processes that link shader programs at the same time park each other inside
`glLinkProgram` for minutes. A process killed while parked keeps its GL
context until the driver call returns. `ir-run` takes the host-wide `gpu`
lock for every run there (and for every `--auto-*` run on every host), holds
it until the child has actually exited, and reports a `RESULT=` verdict. A
direct launch skips all three, so it stalls the fleet's smoke and capture
runs and is stalled by them.

## Sanctioned patterns

| Need | Pattern |
|---|---|
| Run a demo from a script with a custom build dir | `fleet-run --build-dir "$BUILD_DIR" IRShapeDebug --auto-screenshot 5`. ir-run finds the exe under the build tree and runs it from its own directory, so sibling `shaders/`, `scripts/` and `save_files/` resolve as they would for a `cd <exe-dir>` launch. |
| Watchdog a GUI run | `fleet-run --timeout <secs> IR<Name> …`. The wrapper kills the child, then waits for it. |
| Launch from Python | `subprocess.run(platform_launch_argv(["fleet-run", …]))` (`scripts/verify_common.py`). Native-Windows Python cannot execute the extensionless wrapper directly. |

A script that must be testable without a real build takes the wrapper path
from an env seam (`FLEET_RUN="${FLEET_RUN:-$ROOT/scripts/fleet/fleet-run}"`),
and a hermetic suite points it at a logging stub.

## Detection

Two line-based sweeps, both through `fleet-rules-sweep` (exit **1** = clean
pass, **0** = violations, **2** = scope resolved to zero files). The negated
globs are the §"Live deviations" register; keep the two in sync.

Shell and Markdown command position: a path-qualified `IR<Name>` or any
`IR<Name>.exe` at the start of a line or command segment. A segment starts
after `;`, `&&`, `|`, a subshell `(`, `$(`, an inline-code backtick,
`then`/`do`/`exec`/`env`/`nohup`/`time`/`command`, `VAR=val` assignments, or
a `timeout N` prefix.

```
fleet-rules-sweep \
  --glob '!.claude/rules/engine-process-launches.md' \
  --glob '!scripts/fleet/tests/test_engine_process_launch_rule.sh' \
  --pattern '(?:^|[;&|`]|(?<![\w.\]])\(|\$\(|\b(?:then|do|else|exec|nohup|time|command|env)\s)\s*(?:\w+=\S*\s+)*(?:timeout\s+(?:-\S+\s+)*\S+\s+)?["\x27]?(?:[^\s;&|\x27"()`=]*[/\\]IR[A-Z][A-Za-z0-9]*(?:\.exe)?|IR[A-Z][A-Za-z0-9]*\.exe)(?=["\x27\s;&|)]|$)' \
  scripts .claude
```

Python argv: the first element of a single-line list literal passed to
`subprocess.run` / `Popen` / `call` / `check_call` / `check_output` (bare or
path-qualified), or of any single-line list literal when that element is
path-qualified or ends in `.exe`.

```
fleet-rules-sweep \
  --glob '!.claude/rules/engine-process-launches.md' \
  --glob '!scripts/fleet/tests/test_engine_process_launch_rule.sh' \
  --pattern '(?:\b(?:run|Popen|call|check_call|check_output)\(\s*\[\s*[rRbBfF]{0,2}["\x27](?:[^"\x27\s]*[/\\])?IR[A-Z][A-Za-z0-9]*(?:\.exe)?["\x27]|\[\s*[rRbBfF]{0,2}["\x27](?:[^"\x27\s]*[/\\]IR[A-Z][A-Za-z0-9]*(?:\.exe)?|IR[A-Z][A-Za-z0-9]*\.exe)["\x27]\s*[,\]])' \
  scripts .claude
```

A wrapper argument is not in command position, so `fleet-run IRShapeDebug`,
`ir-run --timeout 5 IRShapeDebug` and `ir-run ./IRShapeDebug` never fire. A
second, direct launch later on the same line still does. These are not
launches either: process-table text (`12 11 ./IRPerfGrid`), a
`Path("…/IRPerfGrid")` argument, a backtick span naming a path with no
arguments, and target names (`--target IRShapeDebug`, `add_executable(IRFoo …)`).

**Detector limits, not exemptions.** `fleet-rules-sweep` reads one line at a
time, so the sweeps cannot prove these launch shapes, which still violate the
rule: a Python argv list that spans lines, an executable path built in a
variable (`"$EXE" …`, `[exe, …]`), and a launch through an interpreter the
patterns do not model (`os.exec*`, `cmd.exe /c`). Review catches those.

`scripts/fleet/tests/test_engine_process_launch_rule.sh` runs both commands
verbatim from this block: positive and negative fixtures, the whole-tree
clean pass, and one mutation control per detector arm.

## Live deviations

Both are the rule's own fixtures, not launches:

- `.claude/rules/engine-process-launches.md`: this file documents the shape.
- `scripts/fleet/tests/test_engine_process_launch_rule.sh`: the positive
  fixtures the detectors must fire on.
