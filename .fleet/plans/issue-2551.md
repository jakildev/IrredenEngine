## Plan: standard pause/settings menu — live-toggle registered demo modes (widget-framework consumer)

- **Issue:** #2551
- **Model:** opus
- **Date:** 2026-07-26
- **Blocked by:** #2550 (soft — see "Sibling reconciliation"; the Blocked-by ordering stays so the menu can consume #2550's shipped registry for its Controls section)

### Verified current state

- **No settings or help registry exists anywhere.** Repo-wide grep for `registerSetting` / `SettingEntry` / `C_Settings` / `SettingsRegistry` / `HelpOverlay` / `commandDescription` is empty; #2550 has no code and no posted plan yet (its planning claim is held in parallel as this posts). Candidate set checked: engine/prefabs widget layer, engine/command, IRArgs, engine/asset key_value_store.
- **Widget substrate is complete for this menu.** 11 widget kinds via `IRPrefab::Widget::make*` (`engine/prefabs/irreden/render/widgets.hpp` — makePanel :44, makeLabel :60, makeButton :74, makeSlider :87, makeCheckbox :110, makeDropdown :215). Interaction is **poll-based**: `C_WidgetState.fireAction_` one-frame pulse read via `wasClicked` / `checkboxState` / `sliderValue` (`widgets.hpp:129-147`); no callback fields on any widget component. Reference consumer: `creations/demos/ui_widgets/main.cpp` (creation-side polling in a lambda system's endTick). Theme via `defaultTheme()` — signature stable across the in-flight #2527 C_WidgetTheme migration (PR #2543).
- **Command registration is enum-templated with name-only metadata.** `CommandNames` (`engine/command/include/irreden/command/ir_command_types.hpp:14-51`; enum entry must exist before the `Command<NAME>` specialization links), `createCommand<NAME>(...)` (`ir_command.hpp:365`), `CommandRegistration{name, button, triggerStatus, requiredModifiers}` (`command_manager.hpp:23-28`), `buildCommandListText()` renders `MODIFIER+KEY: NAME` lines (`ir_command.hpp:322`; consumed by `system_text_to_trixel.hpp:86`). **Esc → CLOSE_WINDOW** binds inside `IRCommand::registerCameraCommands()` (`engine/prefabs/irreden/common/command_suite_camera.hpp:12-15`), reached by every demo through `IRPrefab::Camera::registerStandardKeyboardCommands()` (`camera_controls.hpp:55-57`).
- **Live-settable engine APIs already exist for the flagship settings:** `IRRender::setRotationPivotMode` / `getRotationPivotMode` (`ir_render.hpp:306-307`; `RotationPivotMode{ORIGIN, CAMERA_CENTER}` — no dependency on epic #2544 landing), `IRRender::setDebugOverlay` (`ir_render.hpp:433`). `IRSim::pause/isPaused/setTimeScale` (`engine/prefabs/irreden/common/sim_clock.hpp:73-91`) freezes only opt-in sim-clock consumers (render/input pipelines keep ticking); **`resume()` hard-resets timeScale to 1.0** — restoring requires saving the prior scale.
- **shape_debug's modes are CLI-latched, not runtime-togglable:** `--checkerboard` / `--depth-color` / `--debug-overlay` / `--pivot-origin` → `g_*` globals (`creations/demos/shape_debug/main.cpp:358-410`); only runtime keys are F10/F11 (:965-983). shape_debug has no GuiTestConfig; `creations/editors/voxel_editor/main.cpp` is the gui-test reference adoption (events :346, shots :502, assertions :3690+).
- **GUI-test injection reaches the real command dispatch:** `GuiInputEvent` carries `IRInput::KeyMouseButtons` and the driver calls `IRInput::injectButton(...)` (`engine/video/src/auto_screenshot.cpp:211-222`), so a scripted shot can genuinely press Esc and click widgets end-to-end.
- **Singleton components hold non-POD payloads today** (widget components carry `std::string` / `std::vector`), and `.claude/rules/cpp-globals.md` routes world-scoped mutable settings to `IREntity::singleton<T>()`.

### Approach

Decisions the issue delegated to planning, now picked:

1. **Esc opens/closes the menu in adopting demos; quitting moves into the menu.** `registerStandardKeyboardCommands()` (threading through `registerCameraCommands()`) gains a defaulted opt-out param (`bindEscapeCloseWindow = true`) — default unchanged, so every non-adopting demo keeps Esc = quit, byte-identical. An adopting demo passes `false`; the menu prefab registers Esc → `TOGGLE_SETTINGS_MENU` (new `CommandNames` entry). The panel carries a **Quit** button (poll → `IRWindow::closeWindow()`), so quit stays two inputs away; window-manager close is unaffected.
2. **Settings get their own typed registry; the command-help listing stays #2550's.** One registry per concern, both consumed by discoverability surfaces. The menu renders typed settings, plus a read-only "Controls" section (see Sibling reconciliation).
3. **Sim-freeze is opt-in two ways:** a `pauseSimWhileOpen_` flag on the menu CreateInfo (auto-pause on open, restore the saved timeScale on close), and — in shape_debug — a registered "Pause simulation" BOOL setting backed by `IRSim`, so the pause surface is itself a live toggle the gui-verify shot can positively assert.

Steps:

1. **Settings registry** — new `engine/prefabs/irreden/common/settings_registry.hpp`: `C_SettingsRegistry` singleton component (`IREntity::singleton<T>()`, the cpp-globals sanctioned pattern for world-scoped settings) holding `std::vector<SettingEntry>`; `SettingEntry{ name_, description_, kind_ (BOOL/ENUM/FLOAT), std::function getters/setters per kind, enumLabels_, min_/max_/step_ }`. `IRPrefab::Settings::` free functions: `registerBool` / `registerEnum` / `registerFloat`, `forEachSetting`. Registry dies with the world — matching the lifetime of the creation state the setters capture (the same lifetime contract `widget_hotkeys.hpp` documents for its Meyers-singleton registry; the ECS-owned form retires that hazard here).
2. **Command** — `TOGGLE_SETTINGS_MENU` in `CommandNames` + `commandNameToString` + new `engine/prefabs/irreden/input/commands/command_toggle_settings_menu.hpp` (calls `IRPrefab::SettingsMenu::toggle()`).
3. **Menu prefab** — new `engine/prefabs/irreden/render/settings_menu.hpp` + `systems/system_settings_menu.hpp` (`SystemName::SETTINGS_MENU` enum entry first — linker rule): `create(CreateInfo)` registers the system after the `WIDGET_APPLY_*` chain in the INPUT pipeline (mirroring ui_widgets' poll placement) and binds Esc; `open()` / `close()` / `toggle()` / `isOpen()`. Open spawns panel + one row per registered setting (BOOL → checkbox, ENUM → dropdown, FLOAT → slider) + Quit button, absolute layout like ui_widgets, all entity ops deferred; latches each entry's getter value. Close destroys the menu entities. Tick early-returns while closed — **zero overhead closed**: no widget entities exist, no per-frame text batching, the Esc binding is the only standing cost. While open: poll widget state, call setters on change, re-read getters so external writers stay reflected.
4. **shape_debug adoption** — register: "Rotation pivot" ENUM (`get/setRotationPivotMode`), "Debug overlay" ENUM (None/AO/Light level/Shadow → `setDebugOverlay`), "Checkerboard" BOOL + "Depth color" BOOL (demo-side setters re-apply the same shape-flag / voxel-color mutation the CLI path applies at entity creation — the demo owns those entities; voxel colors re-upload per frame so the flip is live), "Pause simulation" BOOL (`IRSim`, save/restore timeScale). Pass `bindEscapeCloseWindow=false` to the camera suite; `SettingsMenu::create(...)`.
5. **gui-verify** — a `GuiTestShot` table in shape_debug behind a flag (voxel_editor pattern): Esc PRESS/RELEASE (assert `predicate` `isOpen()`), MOVE + PRESS/RELEASE on the "Pause simulation" checkbox (assert `checkbox` state AND `predicate` `IRSim::isPaused()` — the positive-fire check), toggle back off, Esc again (assert closed). Wire into `scripts/gui-verify.py` if its demo list is hardcoded.
6. **Byte-identity** — shape_debug render-verify suite + ui_widgets shots `img_diff` = 0 against current references (menu never auto-opens; the Esc rebind changes key-press behavior only); RESULT=CLEAN.

### Affected files

- `engine/prefabs/irreden/common/settings_registry.hpp` — new: `C_SettingsRegistry` + `IRPrefab::Settings` API
- `engine/command/include/irreden/command/ir_command_types.hpp` — `TOGGLE_SETTINGS_MENU` enum entry
- `engine/command/include/irreden/ir_command.hpp` — `commandNameToString` case
- `engine/prefabs/irreden/input/commands/command_toggle_settings_menu.hpp` — new command
- `engine/prefabs/irreden/common/command_suite_camera.hpp` + `engine/prefabs/irreden/render/camera_controls.hpp` — Esc opt-out param (default true)
- `engine/system/include/irreden/ir_system_types.hpp` — `SystemName::SETTINGS_MENU`
- `engine/prefabs/irreden/render/settings_menu.hpp` + `engine/prefabs/irreden/render/systems/system_settings_menu.hpp` — new menu prefab + system
- `creations/demos/shape_debug/main.cpp` — registered settings, menu adoption, gui-test table
- `scripts/gui-verify.py` — only if the demo list is hardcoded (verify)

### Acceptance criteria

1. Menu opens/closes on Esc in shape_debug; one row per registered setting renders; Quit button quits.
2. Live toggles with visible effect: rotation pivot mode, debug overlay, checkerboard — attach-screenshots pair per toggle.
3. `gui-verify` passes on the new shape_debug table with a non-zero assertion count: open → toggle "Pause simulation" ON (`IRSim::isPaused()` asserted true — positive-fire) → toggle OFF → close, all PASS.
4. Every existing capture byte-identical (shape_debug render-verify green, ui_widgets `img_diff` = 0, pivot-verify / jitter gates untouched); RESULT=CLEAN.

### Gotchas

- **Widget interaction is poll, not callbacks** — do not add callback fields to widget components; poll like ui_widgets.
- **`IRSim::resume()` resets timeScale to 1.0** — save/restore the prior scale in both pause paths.
- **Command-list text drift:** `buildCommandListText()` renders every PRESSED registration; if a reference-captured demo displays that text via TEXT_TO_TRIXEL, the Esc swap changes the rendered lines. Check consumers before assuming byte-identity — only adopting demos change, and only if they render the list.
- **Deferred entity ops in-tick** for menu open/close spawn/destroy — mid-iteration structural changes are the ECS footgun.
- **Rotation-pivot setting vs #2548 (epic #2544 P4):** #2548 ships a shape_debug-local pivot-mode key as a stopgap; both writers mutate `IRRender::set/getRotationPivotMode`, so they compose. Whichever lands second deletes nothing; fold #2548's cursor-latch mode into the ENUM labels if it has shipped by then.
- **Menu must never auto-open** — keep it out of `AutoScreenshotShot` paths; the gui-test table is flag-gated.
- **GUI canvas scaling:** widgets render on the GUI canvas (bypasses lighting); shape_debug has not configured GUI-canvas resolution — verify readable layout at the default `guiScale` before hand-tuning positions.

### Sibling reconciliation (#2550, planned in parallel)

#2550 has no plan or code yet. This task needs **nothing** from it at implementation start — registry, menu, and adoption are self-contained. Integration point once #2550 ships (the Blocked-by guarantees ordering): a read-only "Controls" section in the menu listing key bindings from #2550's registry; `buildCommandListText()` is the rendering-ready fallback if #2550 shipped a different shape. If #2550's plan instead hosts typed settings inside its own registry, adapt `IRPrefab::Settings` onto that surface rather than shipping two competing registration APIs — comment on this issue if the shapes genuinely conflict.

### Task shape

One task, one PR, model **opus** (matches the issue's Model field): widget-consumer work against a concrete spec with the design decisions made above. Not worth splitting — the registry alone is one header and unshippable without its consumer.


---

## Amendment A1 — sibling #2550 shipped before implementation start (2026-07-28)

The "Verified current state" bullet above records *"#2550 has no code and no
posted plan yet"*. That was true when the plan was posted (2026-07-27) and is
now stale: #2550 is planned, implemented, and open as **PR #2622**
(`claude/2550-help-overlay-registry`), which this PR stacks on.

Reconciliation per the plan's own "Sibling reconciliation" clause — the shapes
do **not** conflict, so `IRPrefab::Settings` ships as planned rather than being
adapted onto #2550's surface:

- #2550 hosts **no typed settings** by design; its own plan states
  *"`C_SettingsRegistry` is #2551's"*. The two registries cover different
  concerns (key bindings vs typed settings) and both remain single-source.
- #2550 toggles on **F1** and leaves Esc untouched, so this task's Esc rebind
  is still free to take.
- #2550 ships the flag-gated shape_debug `GuiTestShot` table *"for #2551 to
  extend"* — step 5 below extends that table instead of creating one.
- The Controls section consumes `getCommandRegistrations()`, which #2550 widened
  to `(binding, name, description)` — so the planned `buildCommandListText()`
  fallback is not needed.
