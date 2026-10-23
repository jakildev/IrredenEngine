# creations/launcher/ — IRLauncher

The engine's GUI hub: browse the checkout's creations and those of its
configured worktrees, build and run them, open them in the IDE. Contract: [`docs/design/engine-launcher.md`](../../docs/design/engine-launcher.md).
Humans start it with `ir-launcher` ([`docs/agents/BUILD.md`](../../docs/agents/BUILD.md)
§"Launcher"); scripts launch it only through `ir-run` / `fleet-run`.

## Validators

```bash
fleet-build --target IRLauncher
fleet-build --target IRLuaWidgetsDemo            # run_hand_off hands off to it
python3 scripts/gui-verify.py IRLauncher -- --gui-session <session> \
    --catalog-root creations/launcher/test/fixture_root
python3 scripts/gui-verify.py IRLauncher -- --gui-session multi_root
python3 scripts/render-verify.py --target IRLauncher
bash scripts/fleet/tests/run_all.sh --only ir_launcher
```

Sessions: `catalog_tree`, `details_build`, `details_package`, `run_hand_off`,
`settings_roundtrip`, `asset_preview`, `thumbnail_chain`, and
`dockspace_roundtrip` (`main_lua.cpp` shot tables) take the fixture
`--catalog-root`; `multi_root` takes none and generates its own base. `run_hand_off` needs the `ir-run` beside this checkout's sources
on PATH first, or an `ir-run` that knows `--result-file`.

## Layout

- `main_lua.cpp` — pipelines, the tool root (`IR_LAUNCHER_ROOT`, set before
  any script), the launcher-local `IRLauncher` table (`selectRoot`,
  `spawnBaseTool`), the session shot tables.
- `main.lua` — the app: catalog, Build / Package / Run / editor flows, keyboard,
  systems.
- `scripts/launcher_layout.lua` — the dock tree's leaf-relative widget geometry.
  The session shot tables click at these numbers; move both together.
- `scripts/catalog_view.lua`, `details.lua`, `log_panel.lua`, `settings.lua`,
  `assets.lua` — one zone each; `scripts/widget_list.lua` is the shared
  destroy-and-empty helper for a zone's widget ids.
- `test/fixture_root/` — the catalog the sessions and the reference show;
  `test/stub_bin/ir-build` — the build tool `details_build` and
  `details_package` run;
  `test/stub_editors/` — the whole PATH `settings_roundtrip` sees (`code` only);
  `test/multi_root/` — the base and worktree `multi_root` copies into its run
  directory; `test/stub_bin/ir-run` — the hand-off target it records.

## Gotchas

- **Only one child at a time.** A Build, an editor launch and a Run share the
  engine's single tool slot; `handOff` raises while a tool run is live, so Run
  on an unbuilt target waits for its Build to exit.
- **Widgets are rebuilt outside system ticks.** The Lua systems hand their
  work to `IREntity.deferredCall`; a direct rebuild inside a tick would create
  entities mid-iteration.
- **Scripted runs reset the settings store.** Any `--auto-screenshot` run
  starts from an empty `save_files/launcher.irkv`, so saved favourites never
  move the rows a session clicks or the captured reference.
- **The update exit code is 86** in both `main_lua.cpp` and `ir-launcher`.
- **Every creation action re-selects its root first** (`app.confirmRoot`):
  process work follows the active root, while every catalog path stays
  base-relative. A new action that skips it runs in whatever root is active.
- **One configured root renders the v1 tree and header.** The root tier and
  the picker appear at two roots, so v1 sessions and the reference never
  see them.
