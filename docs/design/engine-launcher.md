# Engine launcher — the hub that builds, runs, and browses creations

The engine has 30 demos, 2 editors, private creations, and at least 16 more
executables inside one demo directory, and the only way to know what any of
them is, build it, or run it is the command line plus memory. The launcher is
an engine creation (`IRLauncher`) that lists every creation with a
description, grouped hierarchically, and builds, runs, and opens it in the
IDE from the trixel GUI, on all three hosts. A second panel browses the
assets the editor produces, with live previews.

Decisions locked with the owner on 2026-10-17 (interview in the
`opus-architect` pane). The epic that implements it is filed from this doc; the design PR names the umbrella.

## Survey — what exists and where the gaps are

### Already in the tree

| Need | What covers it today |
|---|---|
| Build a target in the right tree | `ir-build` (`engine/tools/bin/ir-build`): worktree-aware build dir, auto-configure, `IRREDEN_BUILD_DIR` override, cpu lock |
| Run a built exe from its own directory | `ir-run`: finds the exe anywhere under the build tree, `--build-dir`, window modes, gpu lock, `RESULT=` verdicts |
| Enumerate runnables | `ir-run --targets --built` / `--plan`, grouped by directory |
| One creation launching another | `IREngine.handOff` ([`engine-hand-off.md`](engine-hand-off.md)): drain, lend the gpu lease, block in `ir-run`, resume |
| Durable background jobs | `fleet-jobs` (Python supervisor, `meta.json` + `job.log`) |
| GUI widgets from Lua | `IRGui.*` over `engine/prefabs/irreden/render/widgets.hpp`: panel, label, button, list, dropdown, radio, text input, scroll, checkbox, slider, color swatch and hit region. The C++ prefab layer also has the `ui_dockspace` layout, but it is not part of `IRGui`. |
| Live 3D preview in a panel | secondary viewports ([`secondary-viewport.md`](secondary-viewport.md)), `IRRender.createViewport` |
| Pixel images on screen | the sprite layer ([`sprites.md`](sprites.md)), composited below the GUI canvas |
| Thumbnails already rendered | render-verify references under `creations/*/test/references/<preset>/*.png`; `--auto-screenshot` output under `save_files/screenshots/` |
| Asset metadata | `.vxs.json` sidecars (bounds, mode, frame count), prefab v2 manifests, `author.json` |
| Per-creation settings persistence | `.irkv` through `IRSave` |
| Distributable | `irreden_package_target` → `<target>Package` zip |

### Gaps

1. **No creation metadata.** No demo or editor carries a description, group,
   tags, or run presets; `creations/CMakeLists.txt` is the only registry; the
   lighting directory owns 16 executables that nothing groups; target names are
   inconsistent (`DemoMidiDevice`, `EditorFontMaker`, `IRCreationDefault`,
   `IRUIWidgetsDemo` beside `IRUiDockspace`).
2. **No catalog.** Nothing joins source-level creations (including gitignored
   private ones) with build-tree state: built or not, last `RESULT=`, where its
   screenshots landed.
3. **No OS surface for a GUI launcher inside the engine.** `IRFile` is
   run-dir-relative with `io` / `os` closed; there is no directory listing, no
   non-blocking child process with streamed output, no open-in-editor.
   `handOff` runs only `ir-run` children and blocks the main loop.
4. **No JSON reader.** `engine/asset` has a write-only sidecar emitter by
   design; nothing in the engine parses JSON.
5. **No image widget.** The GUI canvas is iso-shaped texels; a PNG thumbnail
   has no widget to land in.
6. **No asset index.** Assets sit in `assets/`, per-creation `data/`,
   `test/references/`, and `save_files/`, with no preview field anywhere.
7. **`RESULT=` is stdout-only.** A parent cannot read the child's verdict;
   `handOff` returns the exit code alone.

Prior art the design follows: Godot's project manager (a manifest at the
project root is the source of truth; the manager scans roots; the external
editor is a setting) and O3DE's Project Manager (build from the UI with a
streamed log, then launch).

## Decisions

### D1 — The launcher is an engine creation on the trixel GUI

`creations/launcher/` is the third tracked subtree under `creations/`
(`.gitignore` allowlist), target `IRLauncher`, Lua-driven (`main_lua.cpp`
registers the engine's widget, layout, viewport and catalog bindings;
`main.lua` builds the UI). It dogfoods `IRGui`, needs no second toolchain,
and runs where the engine runs.

Rejected: a standalone app (Python server + browser page, Tauri, Qt) — rich
lists and log streaming for free, survives a broken engine build, but a
second toolchain and no trixel dogfooding; a terminal UI — keeps the terminal.

### D2 — `creation.json` at each creation root is the source of truth

One file per creation directory, scanned by the launcher; CMake does not
declare catalog data. A creation is a directory; it may own several runnable
targets (the lighting family). Schema v1:

```json
{
  "version": 1,
  "name": "Shape debug",
  "description": "SDF primitives, LOD swaps and yaw pivots; the render-verify smoke target.",
  "group": "render/shapes",
  "tags": ["reference", "render-verify"],
  "kind": "demo",
  "thumbnail": "test/references/macos-debug/zoom4_origin.png",
  "docs": ["docs/design/voxel-and-sdf-rendering.md"],
  "targets": [
    {
      "name": "IRShapeDebug",
      "display": "Shape debug",
      "description": "The reference scene.",
      "presets": [
        { "name": "Interactive", "args": [] },
        { "name": "Load composite proof", "args": ["--load-prefab", "assets/voxel/entities/composite_proof"] },
        { "name": "Screenshot sweep", "args": ["--auto-screenshot", "5"] }
      ]
    }
  ]
}
```

- `group` is a slash path; the tree is built from it. `kind` is one of
  `demo | editor | game | tool | test`. `tags` are free strings for filters.
- `targets[].name` is the CMake / `ir-run` name; `display` decouples the
  shown name from the target name, so the inconsistent target names are a
  separate cleanup, not a launcher blocker.
- `presets[].args` are the exe's own arguments; the first preset is the Run
  button's default.
- The two path keys have different bases. `thumbnail` (optional) is
  relative to the **creation directory**: a manifest need not know its own
  location, and thumbnails live under the creation's `test/references/`.
  Each `docs` entry is relative to the **tool root** (D4), the checkout:
  docs are repo-level files outside creation directories by convention,
  `..` is banned, and the root is the one base that names both
  `docs/design/x.md` and `creations/demos/x/README.md`. The example above
  resolves to the real `docs/design/voxel-and-sdf-rendering.md`.
- Unknown keys are ignored (forward compatible); a missing required key
  (`version`, `name`, `targets[].name`) fails the manifest, and the launcher
  lists the directory as "invalid manifest" rather than hiding it.
- **`parseCreationManifest` checks every value that later reaches a
  process or a path, for every manifest it reads.** Tracked or private, linted
  or not, the parser is the gate, because the scan reads gitignored
  directories no lint sees.
  - `targets[].name` must pass `IRAsset::isTargetName`: the D4 pattern
    `^[A-Za-z0-9_][A-Za-z0-9_.+-]*$`, and no reserved Windows device
    **stem**. After the pattern matches, the stem is the name up to its
    first `.`; a case-insensitive match against `CON`, `PRN`, `AUX`, `NUL`,
    `COM0`–`COM9` or `LPT0`–`LPT9` rejects. Win32 applies the device test to
    that stem, so `NUL.txt`, `con.log.bak` and `nul.` all name the device
    and could not be the stem of a result file. `COM0` and `LPT0` are in the
    set because Microsoft lists them. Still accepted: `COM10`, `LPT10`,
    `CONSOLE`, `NULL`, `nul_x`, `x.nul` (stem `x`). The name becomes `ir-run`'s first argument, where a leading `-` would be
    read as an `ir-run` option. `ir-run` then hands it to `find -name` as a
    pattern, where `*`, `?` and `[` select other binaries. It is also the
    stem of the D5 result file, where `/` or `\` would leave
    `ir-run-results/`.
  - `thumbnail` and each `docs` entry go through the D4 path model, each
    against its own base. The parser sees only the text, so it rejects the
    lexical cases on both keys (empty, a NUL byte, absolute, any `..`
    component). The D3 scan knows both bases: it resolves symlinks, and an
    escape invalidates the manifest the same way.
  - A value that fails marks the whole manifest invalid, with the key and
    the reason, so a bad target is never offered for Build or Run.

Rejected: extending each demo's `config.lua` — mixes runtime config with
catalog data and needs a Lua reader in the scanner; `irreden_creation(...)`
in CMake — no catalog in an unconfigured or broken tree; a README with
front matter — structured fields need a parser anyway.

### D3 — A stdlib JSON reader in `engine/asset`, a catalog module above it

`engine/asset/include/irreden/asset/json_reader.hpp` gains a minimal
stdlib-only reader (objects, arrays, strings, numbers, booleans, null; line
and column on error) beside the existing writer. The sidecar rule — sidecars
are write-only, the binary is the truth — is untouched: `creation.json` is a
manifest, not a sidecar, and the reader is for manifests.
`creation_manifest.hpp` in the same module owns the schema above and its
parse (`parseCreationManifest(text) → CreationManifest | error`).

A new low-level module `engine/catalog/` (`ir_catalog.hpp`, depends on
`asset/`, `utility/` and `common/` only) owns the scan: walk `creations/**`
of a root to depth 3 for `creation.json` (gitignored directories included),
parse each, and join it with build-tree state from a build dir: which
targets have a built executable (the same basename search `ir-run` does),
its mtime, and the last run verdict (D5). The C++ calls take paths:
`IRCatalog::scan(root, buildDir)` and `IRCatalog::assets(root)`, so tests
pass a fixture tree. Lua: `IRCatalog.scan{ root = ..., buildDir = ... }`
returns an array of creations with their targets and state;
`IRCatalog.assets{ root = ... }` returns the asset index (D9).

The catalog reads only under the tool root (D4), and it receives that root
at bind time; it never reads `IREngine::toolRoot()`.
`LuaScript::bindLuaCatalog(const std::filesystem::path &toolRoot)` installs
the `IRCatalog` table with the root captured, the shape of
`bindFiles(script, runDirectory)`. A process that never binds it has no
`IRCatalog` table at all. `root` and `buildDir` are optional root-relative
paths, checked by the D4 path model against the captured root, and default
to `.` and `build`. A refused path raises before anything is read.

The scan resolves each manifest path against its D2 base, through
`resolveContainedPath` (D4): `thumbnail` against the creation directory,
each `docs` entry against the tool root. An escape marks the manifest
invalid. Existence is not a scan concern: a renamed doc is the lint's catch
(Validators), and the `editor` tool fails visibly on a missing file. Every
path the catalog returns (a thumbnail, a reference, a screenshot, a docs
file) is relative to the tool root, so it passes unchanged to `makeImage`
(D9) and to the `editor` and `open-folder` tools.

Rejected: scanning in Lua over a generic `IRFile.listDir` — spreads
filesystem policy into scripts; a Python catalog generator — a second
toolchain the trixel host cannot call back into.

### D4 — Build runs through a new tool-spawn service, not `handOff`

`handOff` blocks the main loop, which is right for a run (D5) and wrong for a
build. `IREngine.spawnTool{ tool = "ir-build", target = "IRShapeDebug" }`
starts a child from a fixed tool table and returns a handle;
`handle:poll()` returns the lines written since the last poll, `handle:exit()`
the exit code once it has one, `handle:kill()` stops it. One tool runs at a
time; a RENDER-phase Lua system polls the handle into the log panel.
Ownership and shutdown are covered below the table.

**Lua names no program, directory, or environment.** Each table entry fixes
its argv shape, working directory and environment, and takes only the typed
fields listed below; any other key raises, and so does a field that fails
its check. Every failure happens before a child is started.

| Tool | Shape | Lua fields | Child |
|---|---|---|---|
| `ir-build` | work | `target` | `ir-build --target <target>`, cwd `<root>`, `IRREDEN_BUILD_DIR=<root>/build` |
| `editor` | hand-off | `path`, `editor` (optional, a name from the fixed list) | `<editor> <resolved path>`, cwd `<root>` (D8) |
| `open-folder` | hand-off | `path` | `open` / `explorer` / `xdg-open` `<resolved path>`, cwd `<root>` |

- **The root is trusted state, never a Lua value.** `<root>` is the checkout
  the launcher was built from (D7). CMake defines `IR_LAUNCHER_ROOT` on the
  `IRLauncher` target as `CMAKE_SOURCE_DIR`. Before any script runs,
  `main_lua.cpp` calls `IREngine::setToolRoot(IR_LAUNCHER_ROOT)`. That C++
  call canonicalizes the path, requires an existing directory, and may run
  only once: a second call raises. Lua can read the root
  (`IREngine.toolRoot()`) but has no call that changes it. `spawnTool` raises
  in any process that never set a root, so no other creation gets the
  service. The service class takes the root as a constructor argument, so
  tests can point it at a fixture tree.
- **The tool root has one owner and no other engine reader.**
  `IREngine::setToolRoot` / `toolRoot()` live on `World` in `engine/world`,
  the owner `cpp-globals.md` sanctions; only the tool service and
  `handOff` read it there. The catalog (D3) and the image binding (D9) take
  their base as a bind-time parameter instead, so neither depends on this
  surface. `IRLauncher`'s `main_lua.cpp` calls `setToolRoot` first, then
  passes `IREngine::toolRoot()` to `bindLuaCatalog` and to the
  `bindWidgets` overload: one compile-time constant, one setter, two
  bind-time reads.
- **`target`** must pass `IRAsset::isTargetName` (D2): a CMake target
  name, which cannot start with `-`. `ir-build` forwards everything else to
  `cmake --build`, and anything after `--` to the native tool (for example
  `-- -C <dir>` under Ninja). So free-form `args` would be a second way out
  of the root, and the entry takes none.
- **`path` is root-relative and must resolve inside the root.** These are
  rejected:
  - an empty string or a NUL byte;
  - an absolute path, including a Windows drive (`C:`), a root-relative path
    (`\x`), and UNC (`\\host`);
  - any `..` component.

  The survivor is joined to `<root>`, and every existing component is
  resolved, following symlinks and Windows junctions
  (`std::filesystem::weakly_canonical`). The result must equal `<root>` or
  lie beneath it, compared component by component, never as a string prefix
  (`/src/IrredenEngine2` is not under `/src/IrredenEngine`). The child gets
  the resolved path, never the Lua string. Lua has no call that creates a
  symlink (`io` / `os` stay closed), so a check-then-spawn race needs another
  host process. That is outside this boundary.
- **The check is one function with one home.**
  `IRUtility::resolveContainedPath` in
  `engine/utility/include/irreden/utility/path_utils.hpp`:
  `std::optional<std::filesystem::path> resolveContainedPath(const
  std::filesystem::path &canonicalBase, std::string_view path)`. It returns
  `nullopt` for every rejection above. The base arrives already canonical,
  as `bindFiles` canonicalizes the run directory today. `engine/utility` is
  the file-and-path helper module: `IrredenEngineAsset` links it `PUBLIC`,
  scripting reaches it through `asset`, and `IrredenEngineWorld` links it
  directly. So the catalog (D3), `spawnTool`, the image binding (D9) and
  `IRFile` all reach it with no new CMake edge.
  It does not go in `common/` (compile-time constants only) or `asset/`
  (formats; a filesystem rule is not a format). The first engine child
  lands it beside `isTargetName` and retargets
  `IRScript::detail::resolveRunPath` to call it, so the tree holds one copy
  of the rule and the existing `IRFile` tests prove no behaviour change.
- **No caller-chosen cwd.** `ir-build` resolves its tree from
  `git rev-parse --show-toplevel` of its cwd. A cwd that is merely contained
  could still name a nested checkout under the root: a `.claude/worktrees/`
  pool or the private `creations/game` clone. Building one runs that
  checkout's CMake. So the cwd is always `<root>`.
- **No caller-chosen build dir.** A configured build dir carries its own
  source dir in `CMakeCache.txt`, so `cmake --build` on another checkout's
  tree builds that checkout. The service therefore sets `IRREDEN_BUILD_DIR`
  to `<root>/build`, replacing the inherited value. `ir-build` honours that
  variable before worktree detection. `ir-run` exports it to its child, so
  the launcher (D6) inherits `build-launcher/installed/<id>/`, and without the
  override a launcher Build would compile the demo into the launcher's own
  tree. For
  every other tool the service removes `IRREDEN_BUILD_DIR` from the child's
  environment. Lua sets no environment variable. Run uses the same fixed
  tree (D5): containment alone cannot tell `<root>/build` from a nested
  checkout's `.claude/worktrees/pool-1/build`, so neither service takes a
  build dir from Lua.

**Ownership and shutdown.** The service owns exactly what it must be able
to stop. A tool table entry has one of two shapes:

- **Work** (`ir-build`) is tree-contained. Its descendants (`cmake`, Ninja,
  the compilers) are the run, so `kill()` and teardown take the whole tree,
  and no work child outlives the engine.
- **Hand-off** (`editor`, `open-folder`) runs a launcher CLI (`cursor` /
  `code`, `open`, `explorer`, `xdg-open`) whose job is to pass the path to a
  desktop application and exit. The service owns the direct child only: it
  holds the tool slot until that child exits, drains and reaps it like any
  run, and `kill()` or teardown stop that child alone. Its descendants
  belong to the desktop session and outlive the call. There is no process
  group on POSIX and no job on Windows.

In both shapes the service holds the same handle, pipe, reap and slot for
the direct child. Only the kill set differs.

- **The service owns everything a run holds:** the child, its process tree,
  the output pipe and the line buffer. `ToolService` is a member of `World`.
  - The Lua handle is a value naming one run (an id plus a generation); it
    owns nothing. Garbage-collecting a handle does not stop the child.
  - A handle from an earlier run raises on `poll`, `exit` and `kill`.
  - Every handle method raises off the main thread, as `spawnTool` does.
- **There is no reader thread.** The service drains the pipe without
  blocking, on the main thread: on every `poll()`, and once per frame from
  the world loop while a run is live. On POSIX the read end is
  `O_NONBLOCK`; on Windows the service reads only what `PeekNamedPipe`
  reports. Lines wait in a bounded buffer whose oldest lines are dropped and
  counted. A script that stops polling cannot stall the child on a full
  pipe, and there is never a thread to join.
- **A work kill takes the whole tree.** `ir-build` runs `cmake`, which runs
  Ninja and the compilers, so killing the wrapper alone would leave them
  running. A hand-off kill signals the direct child only.
  - On POSIX the child starts as the leader of a new process group, with
    stdin on `/dev/null` so a read cannot stop it with `SIGTTIN`. `kill()`
    sends `SIGTERM` to the group, waits up to 2 s for the child to be reaped
    and the group to empty, then sends `SIGKILL` to the group, and reaps the
    child with `waitpid`. A member that ignores `SIGTERM` dies at the
    `SIGKILL` even when the child exited on the first signal.
  - On Windows the child starts suspended. It is assigned to a job object
    with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` before it resumes, and
    `kill()` is `TerminateJobObject` followed by the wait.
  - `exit()` after a kill reports the signal (or the job's exit code).
- **Teardown kills a live run.** `World::end()` destroys the service before
  any manager it could reach. A live run gets the same kill for its shape,
  the child is reaped, and the pipe and job handles close. The bound is the
  2 s grace plus one `SIGKILL` round trip. Nothing is detached or abandoned
  from the service's view of its direct child.
- **One child at a time, across both services.** `handOff` raises while a
  tool run is live. Run would otherwise block the main loop with a build
  still writing into the pipe, or start an executable the build is about to
  relink.
- **Stated limit.** If the launcher process itself crashes on POSIX, no
  destructor runs. The orphaned group finishes its build under the init
  process, and `ir-acquire` releases the cpu lock when the build ends. On
  Windows the job handle closes with the process, which kills the tree. A
  hand-off child has no group or job and keeps running.
- **Stated limit: an opener that execs.** An opener that execs the
  application instead of forking it (an `xdg-open` fallback with no desktop
  opener) makes the window the direct child. That window closes with the
  launcher, because the direct child is the one thing a hand-off kill stops.
- **Stated limit: a descendant on the pipe.** A hand-off descendant may keep
  the pipe's write end open. That is harmless: nothing waits on end of file,
  and the slot frees when the direct child exits.

The launcher's own update is not a tool run (D6); it happens after the
engine has exited.

A tool name outside the table raises; argv is never joined into a shell
command; on native Windows the extensionless wrappers run through
`IR_RUN_BASH` exactly as `handOff` does. The spawn, pipe and wait
primitives are factored out of `engine/hand_off_process.cpp` into one
platform file both services use.

Rejected: arbitrary `IREngine.spawn(argv)` — reopens what closing `io` / `os`
in Lua deliberately closed; `fleet-jobs` as the backend — the engine would
tail Python job logs through absolute paths `IRFile` refuses.

### D5 — Run is `handOff`; the launcher freezes while the demo runs

Run builds the child argv from the manifest preset (`--window-mode normal`
appended: a human is watching) and calls `IREngine.handOff`. The last frame
stays on screen, the gpu lease is lent, and the launcher resumes with the
result. This is identical on all three hosts today and needs no new
lifecycle; on native Windows a live launcher could not render anyway while a
child owns the gpu lock.

Three extensions to the hand-off contract:

- **`argv[0]` is a target name.** `runHandOff` refuses an `argv[0]` that
  fails `IRAsset::isTargetName` (D2), before the drain and before any child
  starts. It also places `--` ahead of `argv[0]` in the `ir-run` command, so
  `ir-run` stops reading options there. Today `handOff` accepts any first
  string. A Lua caller could pass `{ "--build-dir", "/elsewhere", "x" }`
  and choose the tree itself, defeating the fixed tree below. The check
  also keeps the result file name below to a single safe path component.
- **In a process with a tool root, the hand-off runs from `<root>/build`.**
  `runHandOff` passes `ir-run --build-dir <root>/build`, built from the tool
  root (D4), never from a Lua value. Without it the child inherits the
  launcher's own `IRREDEN_BUILD_DIR` (`build-launcher/installed/<id>/`, D6)
  and looks for the demo in the wrong tree. Lua has no `buildDir` key, and
  `handOff` raises on any key other than `argv` and `resume`, so a passed
  directory fails at the call instead of being ignored. The reason:
  `ir-run` searches the directory it is given and executes a matching
  binary, and a root-contained directory can still be a nested checkout's
  build tree (`.claude/worktrees/pool-1/build`, `creations/game/build`).
  That is the boundary D4 fixes Build's tree to keep, so Run keeps it the
  same way. In a process with no tool root, `handOff` is unchanged: no
  `--build-dir`, and the child inherits the parent's `IRREDEN_BUILD_DIR`.
- `ir-run --result-file <path>` writes the `RESULT=` verdict it prints as
  JSON (`verdict`, `exe`, `exit`, `signal`, `argv`, `at`). With a tool root,
  `handOff` passes `<root>/build/ir-run-results/<exe>.last.json` and surfaces
  the parsed verdict as `HandOffResult::verdict_` (Lua `r.verdict`); without
  one it passes no result file and `r.verdict` is absent. The catalog
  (D3) reads the same file for "last run" on every target, so a run from the
  shell shows up too.

Rejected: a detached child with the launcher live — a new spawn-and-reacquire
mechanism, and it blanks on Windows regardless; exit-and-relaunch — loses
in-memory state and needs a file handoff for the verdict.

### D6 — The launcher builds into its own tree and updates on demand

`engine/tools/bin/ir-launcher` configures `<checkout>/build-launcher/` with
the host's preset (`-B` override) on first use and builds `IRLauncher` there.
The launcher itself builds and runs creations in `<checkout>/build`, so a
broken worktree build never takes the launcher down.

**The launcher runs from an installed copy.** The build output is never the
running program.
- After a successful build, the script copies the executable's directory
  into a versioned directory `build-launcher/installed/<id>/`, where `<id>`
  is a UTC stamp and the script's pid (`20261018T071500Z-48213`), so two
  updates in the same second never share a name. It holds the executable and its
  staged `data/`, `shaders/`, `scripts/` and runtime DLLs.
- `build-launcher/installed/current` is a one-line file naming the live
  `<id>`.
- The launcher runs as `ir-run --build-dir build-launcher/installed/<id>
  IRLauncher --window-mode normal --supervised`. `ir-run` needs only an
  existing directory and finds the executable by name beneath it, so no
  `CMakeCache.txt` is involved.
- The copy is needed because `<target>Assets` stages scripts and shaders
  before the link. A build that fails to compile can still leave new
  `scripts/` beside the old executable.

**`ir-launcher` is the supervisor, and the update happens after the engine
exits.** The script never `exec`s. It loops:
1. It runs the installed launcher and waits for it. `ir-run`'s interactive
   path `exec`s the executable, so the exit code reaches the script
   unchanged.
2. If the exit code is `kUpdateRequestedExit`, it updates, then goes back
   to step 1. The value is chosen apart from 0, 1, 2, `ir-run`'s 75 and 76,
   and the signal range from 128 up.
3. Any other exit code ends the loop, and the script exits with that code.

The in-app "Update launcher" button calls `IRLauncher.requestUpdate()`. This
is a launcher-local binding that `main_lua.cpp` registers; it is not part of
the engine-wide `IREngine` table. The call sets a flag and closes the
window. `main` returns `kUpdateRequestedExit` after `IREngine::gameLoop()`
returns, so every manager has already been torn down. That includes the D4
`ToolService` and any build it was running. The binding exists only under
`--supervised`, which only `ir-launcher` passes. Without that flag the
button is hidden, because nothing would act on the exit code.

**One writer per checkout.** Two invocations can reach `installed/`: the
supervisor loop and a shell `ir-launcher --update`. Unserialized, they
break the invariant below. Updater A finishes step 3 with `installed/<a>/`
complete. Updater B's recovery then sees `current` still naming the old
copy and removes `<a>`. A's step 4 then points `current` at a directory
that no longer exists. So every write to `installed/` happens under one
lock.
- **The lock** is `build-launcher/lock/`, scoped to the checkout's launcher
  tree. It uses the primitive `ir-acquire` uses
  (`engine/tools/lib/concurrency_helpers.sh`), once #4546 lands, and
  reaches it only through the helper's functions: `_ir_try_lock` to acquire
  or seed, `_ir_lock_holder` to read the holder, and the helper's release.
  The record layout inside `lock/` belongs to the helper; neither the
  launcher nor its suite names a file there. What the helper guarantees:
  - Ownership is an exclusive record (pid, the native-Windows two-runtime
    token, and a generation nonce); exactly one contender creates it.
  - A dead holder's lock is transferred, never removed. Only the contender
    that claims that generation's reap marker may replace the record, and
    only while the record is unchanged.
  - Release renames the directory away before deleting it.
- **Exclusion holds for stale holders too.** Both double-win paths in
  today's `_ir_try_lock` are reproduced in #4546:
  - two contenders reclaim the same dead holder, and the second `rm -rf`
    removes the first one's new lock;
  - a contender reads a live creator's lock between its `mkdir` and its
    `pid` stamp, takes the empty record as stale, and removes it.

  Either one is the two-writer interleaving above, so the launcher
  creation slice is blocked by #4546 and does not ship a launcher lock on
  the current primitive.
- **The supervisor holds it for its whole life:** recovery, every launch,
  and every update, until the script exits. `--update` holds it from
  recovery through step 5. Nothing reads or writes `installed/` before
  taking it. So one launcher runs per checkout, matching D7's single root.
- **Contention fails fast.** A second invocation (a second `ir-launcher`,
  or `--update` while a launcher runs) does not wait. It prints the
  holder's pid and exits non-zero, having touched nothing under
  `installed/`. While a launcher runs, the in-app button is the update
  path.

**The update step** commits with one atomic file replace:
1. The script builds `IRLauncher` in `build-launcher/` and writes the output
   to `build-launcher/update.log`.
2. It copies the new executable directory into `installed/<id>.staging/`,
   and copies the live installation's `save_files/` (which holds
   `launcher.irkv`, D8) into it.
3. It renames `installed/<id>.staging/` to `installed/<id>/`. The directory
   is now complete.
4. It writes `installed/current.tmp` and replaces `installed/current` with
   it in one rename (`mv -f`: `rename(2)` on POSIX; MSYS2's `mv` is
   `MoveFileEx(MOVEFILE_REPLACE_EXISTING)`, atomic for a file on NTFS).
5. It removes, best effort, every `installed/*/` that `current` does not
   name.

First use is steps 1–4 with nothing to carry. `ir-launcher --update` from a
shell runs recovery and the update step without the loop, and exits
non-zero on failure.

**Invariant.** `current` is written only by the lock holder, only after the
directory it names is complete, and only by an atomic replace. At every
instant `current` names a runnable launcher, and the previous launcher is
never removed before the new one is live.

**Recovery** runs under the lock, first in every loop pass, in `--update`,
and on first use:
- Remove every `installed/*.staging/`.
- If `current` names a directory holding the executable, remove every other
  `installed/*/`: the orphan of a crash between steps 3 and 5, or of a
  failed step 4.
- If `current` is missing or dangling, the launcher is not installed, and
  the script runs first use. That path deletes nothing but `.staging`
  directories.

**Failure arms.** A failure in steps 1–4 relaunches the copy `current`
still names with `--update-failed build-launcher/update.log`. The previous
launcher comes back with a banner and an "Open log in IDE" button that
opens that root-relative path. An interrupted script leaves the same states,
cleared by recovery at the next start.
- Step 1 fails: nothing changes.
- Step 2 or 3 fails or is interrupted: the `.staging` directory goes at
  recovery.
- Step 4 fails: `current` still names the old copy. The new directory is an
  orphan, removed in step 5 of the same pass or at the next recovery.
- Step 5 fails (a handle held open on Windows): logged, retried at the next
  recovery, never fatal.

An interrupt anywhere leaves at most one `.staging` directory and one
orphan. No process waits for its parent, and no child outlives the engine.
Nothing has an installed executable open while the update runs, which
matters on Windows, where a running `.exe` cannot be overwritten or
removed.

**Stated limit.** A supervisor killed with `SIGKILL` leaves its launcher
running under a dead holder pid. The next invocation takes the lock over,
and a later update can remove the directory that orphan runs from.

Rejected:
- Locking only around the update step. Recovery and step 5 in a shell
  update would still remove the copy a running launcher executes from.
- Waiting on a held lock. A shell `--update` would block until a GUI
  session ends.
- A kernel advisory lock (`flock`). It needs no stale reclaim, but macOS
  ships no `flock(1)`, and on native Windows MSYS2 and Git for Windows each
  emulate it inside their own runtime: the two-runtime gap the primitive's
  Windows token exists to close.
- Swapping `installed.next/` → `installed/` → `installed.prev/` with two
  directory renames. Between the first rename and the second, no
  `installed/` exists. A failed second rename, or an interrupt in that
  window, strands the launcher even though the previous copy survives, and
  recovering it needs a bespoke three-name state machine.
- Another target in `build/`: any engine compile break kills the launcher.
- A packaged zip: isolated, but updating is manual.
- A detached `ir-launcher --update --relaunch` child spawned by the
  launcher, which waits for its parent to exit. A spawned tool belongs to
  `ToolService`, which kills it at teardown (D4). Exempting this one child
  would need a second, detached launch path with its own handle and reaping
  rules on each OS.

### D7 — One root in v1: the checkout the launcher was built from

The root is the checkout `IRLauncher` was compiled from (`IR_LAUNCHER_ROOT`,
D4). There is no `--root` flag, because a command-line root would let any
`handOff` parent aim a launcher at another tree and build it. The scan covers
the root's `creations/**`, gitignored private creations included.
`--catalog-root <path>` changes only the default `root` of `IRCatalog.scan`,
a read-only walk. It is root-relative and goes through the same check, so the
fixture tree that GUI sessions and render-verify show lives inside the
checkout. The tool root never follows it. Unit tests point the C++ services
at a fixture tree through their constructors. A root
list in the settings store (D8) is v2, with a worktree picker; each entry
would go through the same containment rules. The data model already carries
`root` on every creation, so nothing in the UI assumes a single root.

Rejected: a configurable root list on day one — setup before value;
auto-scanning `~/src` — noisy with ten fleet worktrees.

### D8 — Settings in `.irkv`; the IDE is a per-host setting

Launcher settings live in the launcher's own run dir through `IRSave`
(`save_files/launcher.irkv`): editor command, favourites, collapsed groups,
last preset per target, panel layout (the dockspace serializer). The `editor`
tool (D4) runs one entry of a fixed C++ list (`cursor`, `code`) resolved on
`PATH`. The settings panel stores the entry's name, and a stored value
outside the list falls back to the first entry found. A stored string is
never run as a command, because `.irkv` is writable from Lua. `open-folder`
runs the host's `open` / `explorer` / `xdg-open`. Both take one D4-checked
`path`: the creation directory or a docs file. Both are hand-offs (D4): the
editor window outlives the call, so Open in IDE followed by Build, and
closing the launcher, leave it open.

### D9 — Previews: live viewports for voxels, a trixelized image widget for PNGs

- A selected `.vxs` or prefab is loaded into a hidden entity
  (`IRAsset.loadVoxelSet`, `Prefab.spawn`) and shown through
  `IRRender.createViewport` in the asset panel with yaw drag; nothing is
  pre-rendered.
- `IRGui.makeImage(x, y, w, h, path)` is a new widget (`WidgetKind::IMAGE`,
  `C_WidgetImage`, `WIDGET_RENDER_IMAGE`): the PNG is box-filtered into the
  widget's trixel rectangle (a texel is two pixels wide, one tall) and
  written into the GUI canvas like any fill. Thumbnails read on-brand; the
  rendering is exact enough to recognise a scene, not to inspect pixels.
- **The image `path` follows the D4 path model.** It is relative to the
  *image base*, a bind-time parameter with the `bindFiles` shape:
  `IRScript::detail::bindWidgets(LuaScript &, const std::filesystem::path
  &imageBase)`, plus the existing `bindWidgets(script)` overload, which
  defaults the base to the run directory, the tree `IRFile` is already
  confined to. The launcher passes its tool root; every other creation
  keeps the run directory. The widget never reads `IREngine::toolRoot()`,
  so there is no runtime branch on it. `IRGui` is an engine-wide table, so
  the widget must not become a host-file read for an arbitrary creation.
  The check runs once, in the Lua binding, through `resolveContainedPath`
  (D4), before the prefab call. `IRPrefab::Widgets::makeImage` and
  `C_WidgetImage` take the resolved path, trusting their C++ callers as
  `IRFile`'s internals do. `IRGui.makeImage` and `IRGui.setImagePath` raise
  before the file is opened or decoded when the path:
  - is empty or holds a NUL byte;
  - is absolute;
  - has a `..` component;
  - resolves outside the base, through a symlink or as a sibling that has
    the base as a string prefix.

  An in-base path that is missing or is not a PNG draws the placeholder and
  logs once, because a thumbnail can vanish between the scan and the draw.
  The catalog's paths are tool-root-relative (D3), so the launcher passes
  them through unchanged. `IRUIWidgetsDemo` stages its test PNG into its run
  directory.
- A creation's thumbnail is `thumbnail` from its manifest, else the first
  render-verify reference for the current preset, else its newest
  `save_files/screenshots/*.png`, else a placeholder.

Rejected: cached PNG thumbnails for voxels — needs a thumbnail job before
anything shows; the sprite layer for thumbnails — pixel-exact, but it
composites under GUI panels, so the layout would have to leave holes.

### D10 — Asset browser v1: voxel entities, prefabs, screenshots

`IRCatalog.assets` indexes `assets/voxel/entities/**` (`.vxs` with its
sidecar fields, prefab manifests with their parts), every creation's `data/`,
and the PNGs under `test/references/` and `save_files/screenshots/`. The
panel lists them by kind with sidecar fields (bounds, mode, frames, parts)
and previews on select (D9). Writing, renaming and tagging assets are not in
scope; the asset's own tool is.

### D11 — Non-goals

No fleet integration (jobs, PRs, claims); no package or export button in v1
(a `<target>Package` button is a follow-up under the cross-host-shippability
objective); no target renaming; no multi-root; no embedding a running demo in
the launcher window.

## UI

A four-zone dockspace (`ui_dockspace` layout API, serialized to settings):

- **Left — catalog tree.** Groups from `group` paths, collapsible, with a
  filter text input over name, tags and description. Favourites float to a
  pinned group. Invalid manifests and manifest-less directories show in a
  trailing "Unsorted" group so nothing on disk is invisible.
- **Center — details.** Thumbnail, name, description, docs links (open in
  editor), tags, and one row per target: display name, built state and mtime,
  last verdict with time, preset dropdown, Build, Run, Open in IDE, Open
  folder.
- **Bottom — log.** The live `ir-build` stream (scroll list over a line
  ring; tail follows unless scrolled up), the last hand-off result, and
  "Open log in IDE" for the full file.
- **Right — assets.** Kind tabs, list, sidecar fields, and the viewport or
  image preview.

Keyboard: `/` focuses the filter, `Enter` runs the selected target's current
preset, `B` builds, `Escape` clears. Every surface is reachable by the GUI
harness (`--gui-session`), the way the voxel editor's sessions are.

## Lua surface added

| Table | Call | Owner |
|---|---|---|
| `IRCatalog` | `scan{ root, buildDir }`, `assets{ root }` (relative to the bound tool root); installed only by `bindLuaCatalog(toolRoot)` | `engine/catalog` |
| `IREngine` | `spawnTool{ tool, target \| path [, editor] }` → handle (`poll`, `exit`, `kill`; `editor` is the optional editor name a `tool = "editor"` request launches); `toolEditors()` → `{ name, found }` entries; `toolRoot()`; `handOff{ argv }` → `r.verdict` (runs from `<root>/build` when a tool root is set, D5) | `engine/world` |
| `IRGui` | `makeImage(x, y, w, h, path)`, `setImagePath(id, path)` (`path` relative to the image base bound by `bindWidgets`, D9); `isPressed(id)` | `engine/prefabs/irreden/render` |
| `IRRender` | `viewportSubjectOversize(id)` | `engine/script` |
| `IRGui` | `focus(id)` queues keyboard focus for a live value widget at the next widget-input frame | `engine/prefabs/irreden/render` |
| `IRLayout` | `SizeMode`, `makeRow`, `makeColumn`, `makeLeaf`, `buildSplitters`, `setRoot`, `leafRect`, `revision`, `serialize`, `deserialize` | `engine/prefabs/irreden/render` |
| `IRLauncher` | `requestUpdate()` (launcher-local, `--supervised` only, D6) | `creations/launcher` |

## Validators

- `ctest -R 'ContainedPath'` (`test/utility/`) — `resolveContainedPath`
  over a fixture base, once, for the whole D4 rejection list: empty, a NUL
  byte, absolute (`/etc`, `C:\x`, `\x`, `\\host\s`), any `..` component, a
  symlink escaping the base, and a `<base>2` sibling with the base as a
  string prefix. An in-base symlink resolves. The catalog, tool-service,
  hand-off and widget suites keep their binding-level refusal rows (zero
  reads, zero launches, zero decodes) but do not re-test the helper. The
  existing `IRFile` tests pass unchanged once `resolveRunPath` calls it.
- `ctest -R 'JsonReader|CreationManifest|CatalogScan'` — the reader over a
  conformance corpus (valid, each error class with line/column), the schema
  (required keys, unknown keys ignored, every `kind`), and a scan over a
  fixture tree with a valid, an invalid, and a missing manifest plus a fake
  build dir with one built target and one result file. A manifest is
  invalid, with the key named, when a `targets[].name` starts with `-`
  (`-x`, `--build-dir`), holds `/`, `\`, whitespace or a glob character
  (`IR*`, `IR?`, `IR[ab]`), is empty, or has a reserved device stem
  (`NUL`, `com1`, `NUL.txt`, `CON.log`, `con.log.bak`, `Aux.cpp`, `COM1.x`,
  `lpt0`). `NULL`, `LPT10`, `nul_x` and `x.nul` are accepted. A manifest is
  also invalid when a `thumbnail` or `docs` path is absolute or holds `..`,
  when a `thumbnail` escapes the creation directory, or when a `docs` entry
  escapes the tool root. `CatalogScanTest.DocsResolve`: the fixture root
  carries `docs/design/x.md`, and a manifest with
  `"docs": ["docs/design/x.md"]` yields a creation whose docs entry is
  `std::filesystem::equivalent` to that file and whose `thumbnail`
  resolves under the creation directory. `IRCatalog` raises, having read
  nothing, for a `root` or `buildDir` that is absolute, holds `..`, or
  escapes through a symlink. A Lua state that never called
  `bindLuaCatalog` has no `IRCatalog` table. Every returned path is
  relative to the tool root.
- `ctest -R 'WidgetImage'` — the image path check over a fixture base, with
  a stub decoder that records each call. These raise from both `makeImage`
  and `setImagePath`, and the decoder records zero calls:
  - an absolute path (`/etc/x.png`, `C:\x.png`, `\\host\s\x.png`);
  - a `..` path (`../x.png`);
  - a fixture symlink that points at a PNG outside the base;
  - a fixture symlink into a `<base>2` sibling, whose path has the base as
    a string prefix.

  An in-base symlink to an in-base PNG decodes once, with the resolved path.
  A missing in-base file draws the placeholder. `bindWidgets(script,
  imageBase)` confines paths to `imageBase`; `bindWidgets(script)` confines
  them to the run directory.
- `ctest -R 'SpawnTool|HandOff'`. The service runs over a fixture root.
  Every refusal case asserts that the stub spawn primitive recorded zero
  launches:
  - **Lua-input refusals.** These all raise:
    - an unknown tool, or an unknown key (`cwd`, `buildDir`, `args`);
    - a NUL byte;
    - a `target` that fails `isTargetName` (the manifest cases above),
      including `spawnTool{ tool = "ir-build", target = "CON.log" }`;
    - a `handOff` whose `argv[0]` fails `isTargetName`, including
      `{ "--build-dir", "<elsewhere>", "x" }` and `{ "NUL.txt" }`. The
      drain also records zero calls;
    - a `handOff` while a tool run is live;
    - a `path` that is absolute (`/etc`, `C:\x`, `\\host\s`) or holds `..`
      (`creations/../..`);
    - a fixture symlink `creations/escape` pointing outside the root, plus a
      file beneath it (on native Windows, a junction stands in when the host
      cannot create symlinks);
    - a sibling directory whose name has the root as a string prefix
      (`<root>2`);
    - a `handOff` carrying `buildDir` or any other key outside `argv` and
      `resume`. That includes a contained nested-checkout tree,
      `buildDir = ".claude/worktrees/pool-1/build"`, where the fixture holds
      a stub binary named like `argv[0]`. The drain also records zero
      calls;
    - `spawnTool` in a process with no tool root;
    - a second `setToolRoot` call.
  - **Accepted requests.** A symlink that stays inside the root is accepted
    and passed resolved. A stub that prints its cwd and `IRREDEN_BUILD_DIR`
    reports `<root>` and `<root>/build` for `ir-build`, while the parent's
    `IRREDEN_BUILD_DIR` points elsewhere. It reports the variable unset for
    every other tool.
  - **Argv.** The `ir-build` argv is exactly `ir-build --target <T>`. The
    hand-off command has `--` immediately before `argv[0]`.
  - **Hand-off.** With a tool root, the child argv carries
    `--build-dir <root>/build` while the parent's `IRREDEN_BUILD_DIR`
    points elsewhere. With no tool root it carries no `--build-dir` and no
    result file. The verdict parses from the result file and is absent
    without one. For a valid name, the result path is exactly
    `<root>/build/ir-run-results/<name>.last.json`, a single component
    below that directory.
  - **Lifecycle, with real children, on Linux, macOS and native Windows.**
    The stub tool starts a sleeping grandchild, prints both process ids and
    keeps writing.
    - Poll returns lines across chunk boundaries.
    - A child that writes past the pipe capacity while nothing calls
      `poll()` still runs to exit as the test steps frames, because of the
      per-frame drain. The buffer reports the dropped-line count.
    - Garbage-collecting the handle leaves the child running. A handle from
      the earlier run raises after a second spawn.
    - `kill()`, and separately destroying the service with a run still live,
      each return within the 2 s grace plus one second. Afterwards neither
      process id is alive, and the child has been reaped (POSIX `waitpid`
      reports `ECHILD`; on Windows the job reports zero active processes).
- `bash scripts/fleet/tests/run_all.sh --only ir_launcher`, on every
  host that runs the suite, native Windows included. Stub `cmake` and
  `ir-run` sit on `PATH`, and a stub installed `IRLauncher` logs each step
  with a timestamp and its pid.
  - **Update.** The stub exits with `kUpdateRequestedExit`. The log shows
    its exit before the build starts. The second launch runs the new
    installed copy with `save_files/launcher.irkv` carried over, and the
    script exits with the second launch's code. Afterwards `installed/`
    holds exactly one directory, and `current` names it.
  - **Failed update.** The stub build fails after staging a new
    `scripts/` file. `current` and the directory it names are
    byte-identical before and after (a hashed tree listing), the relaunch
    gets `--update-failed build-launcher/update.log`, and no `.staging`
    directory remains.
  - **Commit failure.** A `mv` stub on the suite's `PATH` makes step 4's
    replace fail, the same seam as the stub `cmake` and `ir-run`; the
    shipped script has no runtime env seam. The old `<id>` runs with
    `--update-failed`, and the new directory is gone after the pass.
  - **Interrupted swap.** Three pre-seeded states, each followed by one
    pass: (a) an `installed/<x>.staging/`; (b) a complete orphan
    `installed/<y>/` while `current` names `<z>`; (c) no `current`, with an
    orphan present. After (a) and (b) only `<z>` remains, and it ran. (c)
    runs first use and writes `current`.
  - **Overlapping writers.** The `mv` stub is the barrier: on step 4's
    replace it touches a ready file and waits for a release file. Updater A
    (`--update`) parks there with `installed/<a>/` complete and `current`
    still naming the old copy. While A is parked, a second `--update` and a
    plain `ir-launcher` each exit non-zero within one second and print A's
    pid. The stub `cmake` logs no call from either, and the hashed tree
    listing of `installed/` is identical before and after them. After the
    release, `current` names `<a>`, `<a>` holds the executable, and no
    other directory remains.
  - **Running launcher.** While a supervisor's stub launcher is still
    running, `--update` refuses the same way, and `installed/` is
    unchanged.
  - **Stale lock.** `build-launcher/lock/` is pre-seeded through the helper
    (`_ir_try_lock build-launcher/lock <dead-pid> ""`) with a record whose
    pid is not alive. `--update` takes it over and completes.
  - **Two stale reclaimers.** Over the same pre-seeded dead record, two
    `--update` runs start together, for 20 rounds. In every round the
    winner parks on the step-4 `mv` barrier. While it is parked, the other
    run has exited non-zero through the contention path with zero
    stub-`cmake` calls, and `_ir_lock_holder build-launcher/lock` still
    names the winner.
    After the release, exactly one new `<id>` exists. The barrier-ordered
    proof that exactly one contender wins lives in #4546's
    `ir_lock_transfer` suite, which can park the liveness probe; the
    shipped script has no seam for it.
  - **Unique ids.** A `date` stub returns one fixed stamp. Two sequential
    `--update` runs still produce two different `<id>`s.
  - **No leftovers.** After the script exits, no pid it logged is alive.
  - **Pass-through.** Any other exit code ends the loop with that code and no
    build. `--update` alone runs one update and exits non-zero on a failed
    build.
- `bash scripts/fleet/tests/run_all.sh --only ir_run_result_file` — `ir-run
  --result-file` writes every verdict class the stdout line reports.
- `python3 scripts/lint_creation_manifests.py` — every directory under
  `creations/demos` and `creations/editors` has a valid manifest and every
  `targets[].name` is a target the configured tree knows (CI runs it after
  configure). It applies the same value checks as `parseCreationManifest`.
  Its test and the `CreationManifest` ctest read one shared corpus of
  invalid manifests, so the lint and the launcher cannot disagree about
  what is invalid. It also checks existence, which the scan does not: every
  `thumbnail` is a tracked file under its creation directory, and every
  `docs` entry is a tracked file under the checkout root.
- `python3 scripts/gui-verify.py IRLauncher -- --gui-session <name>` —
  `catalog_tree` (groups fold and filter), `details_build` (Build streams a
  stub tool's lines into the log and reports its exit), `run_hand_off`
  (Run hands off to `IRLuaWidgetsDemo --auto-screenshot 3` and shows its
  verdict), `asset_preview` (selecting a `.vxs` shows a viewport whose
  entity-id readback names the loaded entity), `image_widget` (a known PNG
  resamples to the expected texel colours at four probe points).
- `python3 scripts/render-verify.py --target IRLauncher` — the main screen
  over the fixture catalog, and `--target IRUIWidgetsDemo` for the image
  widget's reference.

## Consumers and follow-ups

- **Landing order.** The JSON reader and manifest slice lands
  `IRAsset::isTargetName` and `IRUtility::resolveContainedPath` together,
  and every later engine slice depends on it alone. Catalog, tool service
  and image widget each need only those two functions, so they land in any
  order. The launcher creation follows the catalog, the tool service and
  the lock-primitive fix (#4546, D6), and previews follow the launcher and
  the image widget. The catalog and
  the image widget take their base at bind time (D3, D9), so neither waits
  on the tool service for the tool root. The dockspace follows previews, so
  every final catalog, details, assets and log control reflows from its leaf.
- `creations/CLAUDE.md` and `creations/demos/CLAUDE.md` gain "every creation
  ships `creation.json`" with the schema pointer; `create-creation` scaffolds
  one.
- Target-name normalisation is a separate sonnet task once `display` carries
  the shown names.
- The `<target>Package` button and a clean-box smoke row belong to
  `docs/design/objectives/cross-host-shippability.md`.
- Multi-root and a worktree picker are the first v2 slice; a fleet status
  panel is deliberately not planned.
