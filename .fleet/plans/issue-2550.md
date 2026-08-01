## Plan: registry-driven togglable help overlay — runtime command discoverability for every demo

- **Issue:** #2550
- **Model:** opus — bounded multi-file work against the concrete spec below; design decisions are made here (matches the issue's `**Model:**` field)
- **Date:** 2026-07-27

### Verified current state (differs from the issue sketch)

The sketch assumes the overlay is greenfield. It mostly exists — this task hardens and standardizes an existing seam:

- **Registry exists:** `CommandManager::m_commandRegistrations` (`engine/command/include/irreden/command/command_manager.hpp:23-28`; fields `name/button/triggerStatus/requiredModifiers`), populated by `createCommand` only when a non-empty `name` is passed AND `triggerStatus == PRESSED` (`command_manager.hpp:55-57`). Sole consumer: `IRCommand::buildCommandListText()` (`engine/command/include/irreden/ir_command.hpp:322-333`) — so extending the record is a one-consumer additive change.
- **Renderer exists:** `System<TEXT_TO_TRIXEL>::beginTick` draws that text top-left on the `"gui"` canvas, gated on `IRRender::isGuiVisible()` (default OFF, `render_manager.hpp:170`; that block is the flag's ONLY reader), flipped by `Command<TOGGLE_GUI>` (`engine/prefabs/irreden/render/systems/system_text_to_trixel.hpp:84-96`).
- **Every demo already has the gui canvas:** `RenderManager` itself creates "main"/"background"/"gui", tags them `C_Persistent`, and sets the gui canvas's `C_TrixelCanvasRenderBehavior` (`engine/render/src/render_manager.cpp:69-75, 135-141, 156-165`) — so `TRIXEL_TO_FRAMEBUFFER` already composites an (empty) gui canvas in every demo, shape_debug included. Adoption cost for a text-less demo is one `TEXT_TO_TRIXEL` registration.

Verified gaps (file:line-confirmed, not hypotheses):

1. **No description field anywhere.** `CommandRegistration.name` is the whole human-readable payload; nothing analogous to `IRArgs`' help string.
2. **Cache-forever bug.** `commandList_` builds once (`if (commandList_.empty())`, `system_text_to_trixel.hpp:85-87`) and is never invalidated — commands registered after the first visible frame never appear.
3. **Unnamed-registration holes.** The untemplated façade `IRCommand::createCommand` (`ir_command.hpp:343-360`) forwards NO name — voxel_editor's lambda bindings are invisible. The Lua `IRCommand.createCommand` binding (`engine/script/include/irreden/script/lua_command_bindings.hpp:~300-331`) passes no name either — every Lua-defined command body is invisible (`IRCommand.bindPrefab` routes through the enum path and IS visible). Only the enum-templated path (`ir_command.hpp:365-382`) auto-names via `commandNameToString`. The seam otherwise works: `random_voxels/main.cpp:122` calls the manager directly with a name and its keys appear.
4. **No display path in most demos.** Only 9 targets register `TEXT_TO_TRIXEL` (font_maker, voxel_editor, modifier_demo, default, ui_widgets, perf_grid, lua_widgets, ui_dockspace, random_voxels — repo grep); shape_debug registers 13 bindings and can show none of them.
5. **Presentation rot:** no wrap (`wrapWidth=0`), no background panel, hardcoded white/fontSize-2, and the hand-listed `commandNameToString` switch (`ir_command.hpp:63-118`) silently renders `SCREENSHOT_CANVAS` (`ir_command_types.hpp:31`) and `TOGGLE_CULLING_FREEZE` (`:49`, bound to F10 in shape_debug `main.cpp:971-975`) as `"UNKNOWN"`.
6. **Toggle key inconsistent:** `TOGGLE_GUI` bound to backtick in default/modifier_demo/font_maker, `G` in random_voxels, absent everywhere else. **F1 is verified unbound in the whole tree** (only enum/GLFW-map/Lua-table hits). Esc is `CLOSE_WINDOW` in every demo (`command_suite_camera.hpp:12-15`) — not touchable here; sibling #2551's plan repurposes it for the settings menu.
7. **`random_voxels` forces the overlay on at startup** (`IRRender::setGuiVisible(true)`, `main.cpp:163`) and has **no references dir** — the only creation whose visible output depends on the legacy block.

Negative-claim sweep (grep for help/overlay/hotkey/HelpOverlay/commandDescription across engine/ + creations/): the only other discoverability mechanisms are (a) modifier_demo's hardcoded help string over raw-polled keys (`modifier_demo/main.cpp:451-470` — non-goal), (b) voxel_editor's per-widget hover help (different feature), (c) the perf-stats overlay (not command help), (d) a dead widget-hotkey registry (`widget_hotkeys.hpp` — zero `registerHotkey` call sites; out of scope below). No `HelpOverlay` / `commandDescription` code exists.

No phase-0 measurement probe is needed: every mechanism premise above is a direct code citation, and the one behavioral premise ("adding `TEXT_TO_TRIXEL` to a gated demo with the overlay off stays byte-identical" — grounded in the every-demo-composites-gui fact above) gets a named acceptance check (render-verify vs a pre-change baseline, criterion 3) with a bail path: if NOT byte-identical, stop and comment the finding on the issue rather than coding around it.

### Scope

Turn the existing command-list overlay into the standard registry-driven help overlay: add descriptions to the registry record, fix invalidation, give ad-hoc/Lua commands a naming path, package rendering as an adoptable engine prefab (`IRPrefab::HelpOverlay`) with a standard F1 toggle, and adopt/migrate the five creations that already touch this surface plus shape_debug. The extended `CommandRegistration` stays the single source of truth #2551's settings menu also enumerates (typed-settings records are #2551's own plan; none are hosted here).

### Approach

**Phase 0 — baseline.** Run `render-verify` for shape_debug on the implementing host before any change and record the green baseline (a RED here is pre-existing — isolate before proceeding).

**Phase 1 — registry (engine/command + script binding).**
- Add `std::string description` to `CommandRegistration` (single aggregate-init site, `command_manager.hpp:56`); `CommandManager::createCommand` gains a trailing `std::string description = ""` stored alongside `name`.
- Add a registration generation counter to `CommandManager` (`uint32_t`, bumped only when `m_commandRegistrations` actually grows; `getRegistrationGeneration()` accessor) — the overlay's cache-invalidation signal.
- Replace `commandNameToString`'s switch with one static table (`kCommandInfo[]`: `CommandNames -> {display name, description}`); reimplement `commandNameToString` over it and add `commandDescription(CommandNames)`. Fill the missing rows (`SCREENSHOT_CANVAS`, `TOGGLE_CULLING_FREEZE`, audit all remaining enum values) so `"UNKNOWN"` can't silently ship. Camera-suite entries get real descriptions here — this is how every `standardControlSystems()` demo appears described with zero per-demo wiring.
- Façade: the enum-templated `createCommand<NAME>` forwards `commandDescription(NAME)`; the untemplated overload gains optional trailing `name`/`description` (defaults empty → byte-for-byte behavior at every existing call site; random_voxels can drop its manager-direct bypass).
- New prefab command `TOGGLE_HELP_OVERLAY` — full hand-list checklist per `engine/command/CLAUDE.md`: `CommandNames` entry, `kCommandInfo` row, `Command<TOGGLE_HELP_OVERLAY>` header mirroring `command_toggle_gui.hpp`, `bindPrefabCommand` + `fireByName` cases in `engine/command/src/ir_command.cpp`, `IR_BIND_CMD` row in the Lua binding.
- Lua: `IRCommand.createCommand` gains optional trailing `name`, `description` strings threaded to the manager (closes the Lua invisibility hole). `bindPrefab` needs no signature change.

**Phase 2 — overlay prefab (engine/prefabs/irreden/render).**
- `C_HelpOverlayState { bool visible_ = false; }` singleton component via `IREntity::singleton<T>()` (the `.claude/rules/cpp-globals.md` world-scoped-state row; mirror the `C_WidgetTheme` shape including the `IR_SAVE_OPT_OUT` line in `engine/world/include/irreden/world/save_component_inventory.hpp:323-326`). `Command<TOGGLE_HELP_OVERLAY>` flips it — a value mutation, safe from a command callback. Do NOT add a `RenderManager` field or `IRRender::` setter — `m_guiVisible` is a pre-existing deviation, not a precedent (`engine/prefabs/irreden/render/CLAUDE.md` §"Exposing system public API").
- New `System<HELP_OVERLAY>` (add the `SystemName` enum entry FIRST — linker rule), registered after `TEXT_TO_TRIXEL` (which clears the gui canvas and owns `TextToTrixelProgram`/`GlyphDrawCommandBuffer`) and before `TRIXEL_TO_FRAMEBUFFER`. Structure mirrors `system_perf_stats_overlay.hpp` + `system_widget_render_panel.hpp`: singleton + theme cached in `beginTick` (its `create()` calls `ensureThemeSingleton()` like the `WIDGET_RENDER_*` systems — #2543 is MERGED, `defaultTheme()` reads the `C_WidgetTheme` singleton); early-return everywhere when hidden — zero-cost hidden: no string build, no `fillRect`, no text batching.
- Content: rebuild the text only when `getRegistrationGeneration()` differs from the cached value (kills gap 2). Layout: header `COMMANDS (F1 TO CLOSE)`, key-column-aligned `MOD+KEY  NAME — description` lines (reusing `keyButtonToString`/`modifierString`), wrapped to canvas width minus padding; background `fillRect(kWidgetBackgroundDistance)` + border, colors from `defaultTheme()`. Top-left at `kGuiOverlayPadding` (perf overlay owns top-right). The overlay lists its own toggle binding — self-documenting.
- Remove the `isGuiVisible()`-gated command-list block and the `commandList_` member from `system_text_to_trixel.hpp` — `HELP_OVERLAY` becomes the single renderer. `IRRender::setGuiVisible/toggleGuiVisible/isGuiVisible`, `TOGGLE_GUI`, and `buildCommandListText()` all REMAIN (engine API removal rule; `buildCommandListText` is also #2551's declared fallback); file a follow-up to deprecate once nothing binds them.
- Adoption surface `engine/prefabs/irreden/render/help_overlay.hpp`: `IRPrefab::HelpOverlay::systems()` returning a splice-ready list (includes `TEXT_TO_TRIXEL` only when the creation hasn't registered it — check via the `SystemManager` name registry, #2526) + `registerToggleCommand(int button = IRInput::kKeyButtonF1)` mirroring the `registerStandardKeyboardCommands()` shape.

**Phase 3 — adoption + migration (5 creations).**
- `shape_debug`: adopt (splice `HelpOverlay::systems()` into RENDER before `TRIXEL_TO_FRAMEBUFFER` (`main.cpp:603-625`), `registerToggleCommand()`; its F10/F11 customs get `kCommandInfo` rows — and stop reading "UNKNOWN").
- `random_voxels`: rebind `G` to `TOGGLE_HELP_OVERLAY`, preserve help-visible-at-startup by setting the singleton at init (no references dir, so no capture gate), move its named recolor lambdas onto the now-capable façade with descriptions, update the file-doc paragraph (`main.cpp:15-25`).
- `modifier_demo` + `font_maker`: rebind backtick to `TOGGLE_HELP_OVERLAY` (modifier_demo's bespoke H-help stays untouched).
- `default`: swap the `commands.lua` line to `CN.TOGGLE_HELP_OVERLAY`.

**Phase 4 — tests + docs.**
- Unit test in `test/common/` (`ir_args_test.cpp` precedent): named+described PRESSED registration lands in `getCommandRegistrations()` with the description and bumps the generation; unnamed and non-PRESSED registrations stay filtered and do NOT bump it; every `CommandNames` value has a `kCommandInfo` row (completeness check — no silent "UNKNOWN").
- gui-verify (positive-fire): a minimal flag-gated GuiTest shot table in shape_debug (voxel_editor is the wiring reference; the injection path is real — `GuiInputEvent` → `IRInput::injectButton`, `engine/video/src/auto_screenshot.cpp:211-222`): inject F1, assert overlay-visible predicate + glyph batch non-empty + built text contains a camera-suite entry with description; inject F1 again, assert hidden. Wire into `scripts/gui-verify.py` if its demo list is hardcoded (verify at implementation). #2551 later extends this same table with its menu shots.
- Docs: `engine/command/CLAUDE.md` (table replaces switch; updated hand-list checklist; description conventions), prefabs render `CLAUDE.md` (HelpOverlay adoption recipe), `docs/design/lua-input-commands.md` amendment (named Lua commands).
- `attach-screenshots` pair with the overlay open on shape_debug (visible delta for the PR body).

### Affected files

- `engine/command/include/irreden/command/command_manager.hpp` — `description` field + param, generation counter + accessor
- `engine/command/include/irreden/ir_command.hpp` — `kCommandInfo[]` table, `commandDescription()`, façade param threading; `buildCommandListText` kept as-is
- `engine/command/include/irreden/command/ir_command_types.hpp` — `TOGGLE_HELP_OVERLAY` enum entry
- `engine/command/src/ir_command.cpp` — `bindPrefabCommand` / `fireByName` cases
- `engine/script/include/irreden/script/lua_command_bindings.hpp` — optional `name`/`description` on `IRCommand.createCommand`; `IR_BIND_CMD(TOGGLE_HELP_OVERLAY)`
- `engine/prefabs/irreden/render/commands/command_toggle_help_overlay.hpp` — new
- `engine/prefabs/irreden/render/components/component_help_overlay.hpp` — new `C_HelpOverlayState`
- `engine/prefabs/irreden/render/systems/system_help_overlay.hpp` — new `System<HELP_OVERLAY>`
- `engine/prefabs/irreden/render/help_overlay.hpp` — new adoption surface
- `engine/prefabs/irreden/render/systems/system_text_to_trixel.hpp` — remove gated block + `commandList_`
- `engine/system/include/irreden/system/ir_system_types.hpp` — `HELP_OVERLAY` SystemName
- `engine/world/include/irreden/world/save_component_inventory.hpp` — `C_HelpOverlayState` opt-out
- `creations/demos/shape_debug/main.cpp` — adopt + gui-test shot table
- `creations/demos/random_voxels/main.cpp` — migrate
- `creations/demos/modifier_demo/main.cpp`, `creations/editors/font_maker/main.cpp` — rebind
- `creations/demos/default/commands.lua` — rebind
- `test/common/command_registry_test.cpp` — new unit test
- `scripts/gui-verify.py` — only if the demo list is hardcoded (verify)
- `engine/command/CLAUDE.md`, `engine/prefabs/irreden/render/CLAUDE.md`, `docs/design/lua-input-commands.md` — docs

### Acceptance criteria

1. **Positive-fire (gui-verify):** the shape_debug GuiTest shots fire F1 and assert overlay-visible, glyph draw commands > 0, and the built text contains a camera-suite entry WITH its description (proving the camera bundle appears with zero per-demo wiring); second F1 asserts hidden. Non-zero `GUI-ASSERT` count.
2. **Unit test:** description stored + generation bumps on late registration (regression lock on the cache-forever bug); unnamed/non-PRESSED registrations filtered without a bump; `kCommandInfo` completeness over the whole enum (shape_debug's F10 shows a real name, not "UNKNOWN").
3. **Byte-identity OFF path:** shape_debug render-verify passes after adoption, matching the phase-0 baseline; pivot-verify/jitter gates untouched; existing captures across text-rendering demos unchanged; RESULT=CLEAN.
4. **#2551 contract:** `getCommandRegistrations()` yields `(binding, name, description)` — the read-only iterable its Controls section consumes.
5. **No call-site churn:** all new parameters defaulted; every existing `createCommand` / Lua call site compiles unchanged.
6. `attach-screenshots` pair shows the overlay open on shape_debug (visible delta).

### Gotchas

- `SystemName` enum entry before `registerSystem` — prefab systems fail at link, not compile, without it. Same 4-site hand-list discipline for `CommandNames` (`engine/command/CLAUDE.md`): silent "UNKNOWN" / fire-time error / Lua nil are the asymmetric omission classes.
- Ordering is a hard invariant: `TEXT_TO_TRIXEL` clears the gui canvas and owns the shared text GPU resources; `HELP_OVERLAY` must run after it and before `TRIXEL_TO_FRAMEBUFFER`.
- No structural entity ops from the command callback (it fires mid input-system tick) — the design only flips the singleton's bool; keep it that way.
- `measureText` has no fontSize parameter (measures at 1) — scale panel sizing manually for fontSize 2. The trixel font is uppercase-only — descriptions render uppercase; keep them to one short clause (~60 chars).
- `kMaxGlyphCommands` = 8192/frame shared with all gui text — a 40-command list is comfortably inside; truncate with a WARN on overflow.
- The PRESSED-only registry filter is intentional UX (the WASD `*_END` RELEASED halves stay hidden) — keep it.
- Bump the generation only when a registration actually appends, or zero-cost-hidden degrades into per-frame rebuilds.
- random_voxels shows help at frame 0 and has no reference dir — preserve that; never default-visible in a reference-gated demo.
- Read the singleton + theme once in `beginTick`, never per entity; gui canvas is `guiScale`-downscaled — verify readable layout at default scale before hand-tuning positions.
- Descriptions are engine-public text (plans and code are world-readable): generic engine wording only.

### Non-goals / follow-ups (file per TASK-FILING.md, don't fold in)

- Naming voxel_editor's lambda bindings — mechanical but bulky, and collides with in-flight editor work (PR #2558). Follow-up, [sonnet].
- Folding the dead widget-hotkey registry (`widget_hotkeys.hpp`, zero call sites) into the overlay — speculative until it has users.
- Deprecating `TOGGLE_GUI` / `isGuiVisible` / `buildCommandListText` once nothing binds them (post-#2551, which names `buildCommandListText` as its fallback).
- Registry entries for raw-polled mouse gestures (camera pan/rotate/zoom systems) and modifier_demo's polled keys.

### Sibling / in-flight reconciliation

- **#2551 (settings menu — plan posted 2026-07-27, queued, blocked on this):** consumes exactly what Phase 1 ships — a read-only iterable of (binding, name/description) via `getCommandRegistrations()` — with `buildCommandListText()` as its declared fallback (kept working here). This plan hosts **no typed settings** (`C_SettingsRegistry` is #2551's), Esc is untouched (F1 toggle), and the flag-gated shape_debug GuiTest table lands here first for #2551 to extend. No conflict to flag on #2551.
- **PR #2543 (widget theme → `C_WidgetTheme` singleton): MERGED** (b2d38ce6) — the overlay reads `defaultTheme()` and calls `ensureThemeSingleton()` in `create()` per the merged pattern.
- **PR #2561 (shape_debug sweep-shot helper, approved):** same `main.cpp` — expect a trivial rebase.
- **Epic #2544 chain (#2545-#2548, PR #2562):** disjoint (rotation internals). #2548's future shape_debug mode-toggle key will simply appear in this overlay once registered with a name.
- **Epic #212 (retained UI framework):** this consumes the shipped widget substrate (theme, rect primitives); no overlap with its remaining scope.

### Task shape

One task, one PR, model **opus**: the registry extension is unshippable without its overlay consumer, the migration sites are 1-3 lines each, and splitting would strand the legacy-block removal between PRs. High-stakes per the PLANNING-PROTOCOL checklist (cross-cutting: engine/command + script + prefabs + system + world and five creations) — `human:review-plan` set alongside `fleet:plan-review`.

## Plan review — SOUND (cleared `fleet:plan-review`)

Opus plan-review pass. Structure lint PASS (0 warnings); design-soundness judged against PLANNING-PROTOCOL step-2 rigor — sound on every axis:

- **Verified current state corrects the sketch.** The plan re-grounds the issue's "greenfield overlay" assumption with file:line citations: the registry (`CommandManager::m_commandRegistrations`), the renderer (`System<TEXT_TO_TRIXEL>` gated on `isGuiVisible()`), and the every-demo gui canvas already exist — so this hardens an existing seam. The six gaps are code-confirmed, not hypotheses (cache-forever bug at `system_text_to_trixel.hpp:85-87`, unnamed-registration holes in the façade + Lua binding, the `"UNKNOWN"` switch rows). Negative-claim sweep done and F1-unbound verified tree-wide.
- **Single correct approach**, no deferred option-A/B.
- **No unmeasured-mechanism premise.** Every mechanism is a citation; the one behavioral premise (adding `TEXT_TO_TRIXEL` to a gated demo stays byte-identical, off) is gated by a named acceptance check (criterion 3, render-verify vs the phase-0 baseline) with an explicit bail path — satisfies #2401.
- **Positive-fire acceptance.** gui-verify asserts overlay-visible + glyph batch >0 + a camera-suite entry rendered *with its description* (proving zero-per-demo-wiring adoption), plus the unit test locking the cache-invalidation regression and enum completeness. Not a vacuous RESULT=CLEAN.
- **Cross-system audit + hand-list discipline** (SystemName-before-registerSystem, CommandNames 4-site) are called out; world-scoped state uses `IREntity::singleton<T>()` per `.claude/rules/cpp-globals.md` rather than a new RenderManager field.
- **Sibling reconciliation is right:** #2551 consumes this registry read-only (no typed settings hosted here, Esc untouched), #2543 is MERGED (reads `defaultTheme()`), #2561 is a trivial rebase, epic #2544 is disjoint.

**`human:review-plan` stays** — the plan-reviewer clears only the agent gate; the human's approach sign-off on this cross-cutting (engine/command + script + prefabs + system + world + 5 creations) task is theirs to clear. The issue will not queue until both are gone.

