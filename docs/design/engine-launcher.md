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
| GUI widgets from Lua | `IRGui.*` over `engine/prefabs/irreden/render/widgets.hpp`: panel, label, button, list, dropdown, radio, text input, scroll, checkbox, slider, color swatch, hit region; dockspace with splitters and panel drag (`ui_dockspace`); layout serialize |
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
  button's default. `thumbnail` is relative to the creation root and optional.
- Unknown keys are ignored (forward compatible); a missing required key
  (`version`, `name`, `targets[].name`) fails the manifest, and the launcher
  lists the directory as "invalid manifest" rather than hiding it.

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
`asset/` and `common/` only) owns the scan: walk `creations/**` of a root to
depth 3 for `creation.json` (gitignored directories included), parse each,
and join it with build-tree state from a build dir: which targets have a
built executable (the same basename search `ir-run` does), its mtime, and the
last run verdict (D5). Lua: `IRCatalog.scan{ root = ..., buildDir = ... }`
returns an array of creations with their targets and state;
`IRCatalog.assets{ root = ... }` returns the asset index (D9).

Rejected: scanning in Lua over a generic `IRFile.listDir` — spreads
filesystem policy into scripts; a Python catalog generator — a second
toolchain the trixel host cannot call back into.

### D4 — Build runs through a new tool-spawn service, not `handOff`

`handOff` blocks the main loop, which is right for a run (D5) and wrong for a
build. `IREngine.spawnTool{ tool = "ir-build", args = {...}, cwd = ..., buildDir = ... }`
starts a child from a fixed tool table and returns a handle;
`handle:poll()` returns the lines written since the last poll, `handle:exit()`
the exit code once it has one, `handle:kill()` stops it. One tool runs at a
time; the launcher's Build button spawns `ir-build --target <T>` with
`cwd = <root>` and `buildDir = <root>/build`, and a RENDER-phase Lua system
polls the handle into the log panel.

`ir-build` has no build-directory flag: it resolves the tree from the
invoking worktree (`git rev-parse --show-toplevel` of its cwd) unless
`IRREDEN_BUILD_DIR` is set, and forwards every other argument to
`cmake --build`. The cwd alone is not enough, because `ir-run` exports
`IRREDEN_BUILD_DIR` to the process it runs, so the launcher (D6) and every
child it spawns inherit `build-launcher/`. `buildDir` is therefore a typed
field, not free-form env: the service sets the child's
`IRREDEN_BUILD_DIR` to it, replacing the inherited value. Without `buildDir`
the service removes the variable from the child's environment, so a tool
never builds into the launcher's own tree by accident. Lua cannot set any
other environment variable.

The tool table is `ir-build`, `ir-launcher` (self-update, D6), `editor` and
`open-folder` (D8). A tool name outside the table raises; argv is never
joined into a shell command; on native Windows the extensionless wrappers run
through `IR_RUN_BASH` exactly as `handOff` does. The spawn, pipe and wait
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

Two extensions to the hand-off contract:

- `HandOffRequest::buildDir_` (Lua `buildDir`), passed to the child as
  `ir-run --build-dir`. Without it the child inherits the launcher's own
  `IRREDEN_BUILD_DIR` (`build-launcher/`, D6) and looks for the demo in the
  wrong tree.
- `ir-run --result-file <path>` writes the `RESULT=` verdict it prints as
  JSON (`verdict`, `exe`, `exit`, `signal`, `argv`, `at`). `handOff` passes a
  path under `<buildDir>/ir-run-results/<exe>.last.json` and surfaces the
  parsed verdict as `HandOffResult::verdict_` (Lua `r.verdict`); the catalog
  (D3) reads the same file for "last run" on every target, so a run from the
  shell shows up too.

Rejected: a detached child with the launcher live — a new spawn-and-reacquire
mechanism, and it blanks on Windows regardless; exit-and-relaunch — loses
in-memory state and needs a file handoff for the verdict.

### D6 — The launcher builds into its own tree and updates on demand

`engine/tools/bin/ir-launcher` configures `<checkout>/build-launcher/` with
the host's preset (`-B` override) on first use, builds `IRLauncher` there,
and runs it through `ir-run --build-dir build-launcher IRLauncher
--window-mode normal --root <checkout>`. It rebuilds only with `--update`; a
failed update leaves the previous executable in place and exits non-zero.
The launcher itself builds and runs creations in `<checkout>/build`, so a
broken worktree build never takes the launcher down. The in-app "Update
launcher" button spawns `ir-launcher --update --relaunch` (D4) and quits;
the script waits for the parent to exit, rebuilds, and relaunches.

Rejected: another target in `build/` — any engine compile break kills the
launcher; a packaged zip — isolated but updating is manual.

### D7 — One root in v1: the checkout the launcher was built from

`--root` is the checkout; the scan covers its `creations/**`, gitignored
private creations included. A root list in the settings store (D8) is v2,
with a worktree picker; the data model already carries `root` on every
creation so nothing in the UI assumes one.

Rejected: a configurable root list on day one — setup before value;
auto-scanning `~/src` — noisy with ten fleet worktrees.

### D8 — Settings in `.irkv`; the IDE is a per-host setting

Launcher settings live in the launcher's own run dir through `IRSave`
(`save_files/launcher.irkv`): editor command, favourites, collapsed groups,
last preset per target, panel layout (the dockspace serializer). The editor
command defaults to the first of `cursor`, `code` found on `PATH`; the
`editor` tool (D4) runs it with the creation directory; `open-folder` runs
`open` / `explorer` / `xdg-open`. Both are chosen by the settings panel, never
by Lua.

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
| `IRCatalog` | `scan{ root, buildDir }`, `assets{ root }` | `engine/catalog` |
| `IREngine` | `spawnTool{ tool, args, cwd, buildDir }` → handle (`poll`, `exit`, `kill`); `handOff{ argv, buildDir }` → `r.verdict` | `engine/world` |
| `IRGui` | `makeImage(x, y, w, h, path)`, `setImagePath(id, path)` | `engine/prefabs/irreden/render` |

## Validators

- `ctest -R 'JsonReader|CreationManifest|CatalogScan'` — the reader over a
  conformance corpus (valid, each error class with line/column), the schema
  (required keys, unknown keys ignored, every `kind`), and a scan over a
  fixture tree with a valid, an invalid, and a missing manifest plus a fake
  build dir with one built target and one result file.
- `ctest -R 'SpawnTool|HandOff'` — the tool table refuses unknown tools and
  NUL bytes; poll returns lines across chunk boundaries; kill ends a sleeping
  child; a stub tool that prints its working directory and
  `IRREDEN_BUILD_DIR` reports the requested `cwd` and `buildDir` with the
  parent's `IRREDEN_BUILD_DIR` set to a different path, and reports the
  variable unset when `buildDir` is omitted; the `ir-build` entry's argv is
  exactly `ir-build --target <T>` (no build-directory flag); the hand-off
  `buildDir_` reaches the child argv; the verdict parses from the result file
  and is absent without one.
- `bash scripts/fleet/tests/run_all.sh --only ir_run_result_file` — `ir-run
  --result-file` writes every verdict class the stdout line reports.
- `python3 scripts/lint_creation_manifests.py` — every directory under
  `creations/demos` and `creations/editors` has a valid manifest and every
  `targets[].name` is a target the configured tree knows (CI runs it after
  configure).
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

- `creations/CLAUDE.md` and `creations/demos/CLAUDE.md` gain "every creation
  ships `creation.json`" with the schema pointer; `create-creation` scaffolds
  one.
- Target-name normalisation is a separate sonnet task once `display` carries
  the shown names.
- The `<target>Package` button and a clean-box smoke row belong to
  `docs/design/objectives/cross-host-shippability.md`.
- Multi-root and a worktree picker are the first v2 slice; a fleet status
  panel is deliberately not planned.
