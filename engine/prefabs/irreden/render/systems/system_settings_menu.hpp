#ifndef SYSTEM_SETTINGS_MENU_H
#define SYSTEM_SETTINGS_MENU_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/common/settings_registry.hpp>
#include <irreden/common/sim_clock.hpp>
#include <irreden/render/components/component_settings_menu.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/widget_draw.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/widgets.hpp>

#include <string>
#include <vector>

namespace IRSystem {

// Panel geometry, in GUI-canvas trixels. The menu is centered rather than
// corner-anchored: the two GUI corners are already spoken for (help overlay
// top-left, perf stats top-right), and a pause menu reads as modal when it
// sits in the middle.
inline constexpr int kSettingsMenuWidth = 380;
inline constexpr int kSettingsMenuMargin = 16;
inline constexpr int kSettingsMenuPad = 12;
inline constexpr int kSettingsMenuRowHeight = 24;
inline constexpr int kSettingsMenuRowGap = 6;
inline constexpr int kSettingsMenuTitleHeight = 26;
inline constexpr int kSettingsMenuQuitHeight = 26;

// Fraction of a row's width given to an ENUM setting's name label, leaving the
// rest for its dropdown. Checkbox and slider carry their own label, so this
// applies to the dropdown row only.
inline constexpr int kSettingsMenuEnumLabelPercent = 45;

// Slider round-trips its float through widget pixels, so an exact compare
// would ping-pong a value between the widget and its setter. BOOL and ENUM
// values are integral and compare exactly.
inline constexpr float kSettingsMenuFloatEpsilon = 1e-4f;

// Standard pause/settings menu (#2551).
//
// Renders one interactive row per `C_SettingsRegistry` entry — checkbox for
// BOOL, dropdown for ENUM, slider for FLOAT — and applies edits back through
// the entry's setter. A creation registers settings; it does not lay out,
// poll, or draw anything.
//
// **Zero cost while closed.** The menu owns no entities until it opens: the
// whole closed-frame cost is the one-row tick below plus a branch in endTick.
// That is what keeps every existing `--auto-screenshot` capture byte-identical
// — an unopened menu contributes nothing to the GUI canvas.
//
// Widget entities are spawned in `endTick` (main-thread, past this system's
// own iteration) and torn down with `IREntity::destroyEntity`, which marks
// rather than destroys. Both edges land safely inside the frame: INPUT runs
// before `destroyMarkedEntities()`, which runs before RENDER, so a menu opened
// this frame draws this frame and a menu closed this frame never does.
//
// Pipeline placement: INPUT, after the `WIDGET_APPLY_*` chain whose output it
// polls. `IRPrefab::SettingsMenu::inputSystems()` / `renderSystems()` splice
// the full set in the right order.
template <> struct System<SETTINGS_MENU> {
    struct Params {
        // Freeze the sim clock while the menu is open, restoring the previous
        // time scale on close (never `IRSim::resume()`, which would clobber a
        // custom rate with 1.0). Leave false when the creation would rather
        // expose pause as its own registered BOOL setting — running both means
        // two writers race over one clock.
        bool pauseSimWhileOpen_ = false;
    };

    // One rendered setting. `label_` is used only by ENUM rows; checkbox and
    // slider render their own name, so spawning a second label for them would
    // double-draw it.
    struct Row {
        int settingIndex_ = -1;
        IRComponents::SettingEntry::Kind kind_ = IRComponents::SettingEntry::Kind::BOOL;
        IREntity::EntityId control_ = IREntity::kNullEntity;
        IREntity::EntityId label_ = IREntity::kNullEntity;
        // Last value seen agreeing on both sides. Distinguishes "the user moved
        // the widget" from "something else moved the setting" without either
        // edit chasing the other.
        float lastValue_ = 0.0f;
    };

    Params params_;

    bool open_ = false;
    bool built_ = false;
    float savedTimeScale_ = 1.0f;

    std::vector<Row> rows_;
    IREntity::EntityId panel_ = IREntity::kNullEntity;
    IREntity::EntityId controlsLabel_ = IREntity::kNullEntity;
    IREntity::EntityId quitButton_ = IREntity::kNullEntity;

    void beginTick() {
        open_ = false;
    }

    void tick(const IRComponents::C_SettingsMenuState &state) {
        open_ = state.open_;
    }

    void endTick() {
        if (open_ && !built_) {
            buildMenu();
        } else if (!open_ && built_) {
            destroyMenu();
        } else if (open_) {
            // Skipped on the frame the menu is built: its widgets were seeded
            // straight from the getters, so there is nothing to reconcile yet.
            applyEdits();
        }
    }

    static SystemId create() {
        return create(Params{});
    }

    static SystemId create(const Params &initialParams) {
        // Registration-time singleton touches, for the main-thread reason
        // `widget_theme.hpp` documents: `singleton<T>()`'s first call is a
        // `createEntity`, which must not first run inside a beginTick.
        // Touching the menu-state singleton is also what puts the row in the
        // archetype this system iterates, so the tick fires from frame 1.
        IRPrefab::SettingsMenu::ensureStateSingleton();
        IRPrefab::Settings::ensureRegistrySingleton();
        IRPrefab::Widget::ensureThemeSingleton();
        const SystemId systemId =
            registerSystem<SETTINGS_MENU, IRComponents::C_SettingsMenuState>("SettingsMenu");
        auto *params = getSystemParams<System<SETTINGS_MENU>>(systemId);
        params->params_ = initialParams;
        // panel_ (and every other widget id below) is not C_Persistent, so
        // IREntity::resetGameplay() destroys it mid-open (destroyAllExceptPreserved
        // destroys per-entity via destroyEntity, which fires this hook) while
        // C_SettingsMenuState::open_ survives as a preserved singleton. Key off
        // panel_ — it exists whenever the menu is built, regardless of registry
        // size — and take the same non-destructive tail destroyMenu() takes, so
        // the two paths cannot forget the built state differently: the widget ids
        // and rows_ are dropped (destroyMenu() never runs on this path, so rows_
        // would otherwise still hold stale entries the next buildMenu() appends
        // onto), the paused sim clock is restored, and built_ falls so the next
        // endTick's `!built_` branch rebuilds instead of applyEdits() reaching
        // getComponent through dead control_/label_ ids.
        IREntity::getEntityManager().registerPreDestroyHook([params](IREntity::EntityId destroyed) {
            if (params->panel_ != destroyed) {
                return;
            }
            params->forgetBuiltState();
        });
        return systemId;
    }

  private:
    void buildMenu() {
        const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        if (guiCanvas == IREntity::kNullEntity) {
            return;
        }
        const IRMath::ivec2 canvasSize =
            IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas).size_;

        const auto &settings = IRPrefab::Settings::registry().settings_;
        const int rowCount = static_cast<int>(settings.size());

        const int panelWidth =
            IRMath::min(kSettingsMenuWidth, canvasSize.x - 2 * kSettingsMenuMargin);
        const int rowStride = kSettingsMenuRowHeight + kSettingsMenuRowGap;
        // Built once and reused for both the height reservation and the label —
        // it scans the command registry, so calling it twice per open would
        // double that walk for an identical answer.
        const std::string hint = controlsHintText();
        const int hintHeight = hint.empty() ? 0 : rowStride;
        const int panelHeight = 2 * kSettingsMenuPad + kSettingsMenuTitleHeight +
                                rowCount * rowStride + hintHeight + kSettingsMenuQuitHeight;

        // Centered, but never above the top margin — a registry long enough to
        // overflow the canvas grows downward off the bottom rather than
        // starting off-screen where the title would be unreachable.
        const IRMath::ivec2 panelPos(
            IRMath::max(kSettingsMenuMargin, (canvasSize.x - panelWidth) / 2),
            IRMath::max(kSettingsMenuMargin, (canvasSize.y - panelHeight) / 2)
        );

        // No hitbox on the panel: `makePanel` deliberately leaves interactive
        // routing to the controls on top of it (see `widgets.hpp`), and adding
        // one at the same z-order would let the backdrop steal their hover.
        panel_ = IRPrefab::Widget::makePanel(
            panelPos,
            IRMath::ivec2(panelWidth, panelHeight),
            "SETTINGS"
        );

        const int contentX = panelPos.x + kSettingsMenuPad;
        const int contentWidth = panelWidth - 2 * kSettingsMenuPad;
        int y = panelPos.y + kSettingsMenuPad + kSettingsMenuTitleHeight;

        rows_.reserve(static_cast<std::size_t>(rowCount));
        for (int i = 0; i < rowCount; ++i) {
            rows_.push_back(
                buildRow(settings[static_cast<std::size_t>(i)], i, contentX, y, contentWidth)
            );
            y += rowStride;
        }

        if (!hint.empty()) {
            controlsLabel_ =
                IRPrefab::Widget::makeLabel(IRMath::ivec2(contentX, textBaseline(y)), hint);
            y += rowStride;
        }

        quitButton_ = IRPrefab::Widget::makeButton(
            IRMath::ivec2(contentX, y),
            IRMath::ivec2(contentWidth, kSettingsMenuQuitHeight),
            "QUIT"
        );

        if (params_.pauseSimWhileOpen_) {
            savedTimeScale_ = IRSim::timeScale();
            IRSim::pause();
        }
        built_ = true;
    }

    Row
    buildRow(const IRComponents::SettingEntry &setting, int settingIndex, int x, int y, int width) {
        Row row;
        row.settingIndex_ = settingIndex;
        row.kind_ = setting.kind_;
        row.lastValue_ = setting.get_ ? setting.get_() : 0.0f;

        const IRMath::ivec2 pos(x, y);
        const IRMath::ivec2 size(width, kSettingsMenuRowHeight);
        switch (setting.kind_) {
        case IRComponents::SettingEntry::Kind::BOOL:
            row.control_ =
                IRPrefab::Widget::makeCheckbox(pos, size, setting.name_, row.lastValue_ != 0.0f);
            break;
        case IRComponents::SettingEntry::Kind::ENUM: {
            const int labelWidth = width * kSettingsMenuEnumLabelPercent / 100;
            row.label_ =
                IRPrefab::Widget::makeLabel(IRMath::ivec2(x, textBaseline(y)), setting.name_);
            row.control_ = IRPrefab::Widget::makeDropdown(
                IRMath::ivec2(x + labelWidth, y),
                IRMath::ivec2(width - labelWidth, kSettingsMenuRowHeight),
                setting.enumLabels_,
                static_cast<int>(row.lastValue_)
            );
            break;
        }
        case IRComponents::SettingEntry::Kind::FLOAT:
            row.control_ = IRPrefab::Widget::makeSlider(
                pos,
                size,
                setting.name_,
                setting.min_,
                setting.max_,
                row.lastValue_
            );
            break;
        }
        return row;
    }

    void destroyMenu() {
        for (Row &row : rows_) {
            destroyIfLive(row.control_);
            destroyIfLive(row.label_);
        }
        destroyIfLive(panel_);
        destroyIfLive(controlsLabel_);
        destroyIfLive(quitButton_);
        forgetBuiltState();
    }

    // The non-destroying half of destroyMenu(): forgets the built state without
    // touching entities, so the pre-destroy hook — which runs while the widgets
    // are already being destroyed underneath it — can share it. Every teardown
    // responsibility lives here rather than in destroyMenu(), so a path that
    // performs only some of them cannot exist: dropping the widget ids while
    // leaving the clock paused would make the next buildMenu() re-save a
    // savedTimeScale_ of 0 and freeze the sim with no way back.
    void forgetBuiltState() {
        rows_.clear();
        panel_ = IREntity::kNullEntity;
        controlsLabel_ = IREntity::kNullEntity;
        quitButton_ = IREntity::kNullEntity;

        if (params_.pauseSimWhileOpen_) {
            // Restore the scale that was running, not 1.0 — `IRSim::resume()`
            // hard-resets to 1x and would silently discard a demo's custom rate.
            IRSim::setTimeScale(savedTimeScale_);
        }
        built_ = false;
    }

    static void destroyIfLive(IREntity::EntityId &entity) {
        if (entity == IREntity::kNullEntity) {
            return;
        }
        IREntity::destroyEntity(entity);
        entity = IREntity::kNullEntity;
    }

    void applyEdits() {
        if (quitButton_ != IREntity::kNullEntity && IRPrefab::Widget::wasClicked(quitButton_)) {
            IRWindow::closeWindow();
            return;
        }

        auto &settings = IRPrefab::Settings::registry().settings_;
        for (Row &row : rows_) {
            if (row.settingIndex_ < 0 || row.settingIndex_ >= static_cast<int>(settings.size())) {
                continue;
            }
            const std::size_t settingIndex = static_cast<std::size_t>(row.settingIndex_);

            // The user's edit wins over an external write in the same frame:
            // it is the more recent intent, and re-reading the getter first
            // would overwrite the click that just happened.
            const float widgetValue = readWidget(row);
            if (differs(row.kind_, widgetValue, row.lastValue_)) {
                // Both callbacks are invoked straight out of the registry, and
                // what makes that safe is `settings_registry.hpp`'s contract:
                // neither a getter nor a setter may register a setting, so
                // `settings_` cannot reallocate under this loop. Copying the
                // `std::function` out first would not buy the getter path the
                // same protection anyway — `get_()` reads the object and
                // *then* calls it, so a re-entrant registration destroys it
                // mid-call either way. One rule for both, enforced at the
                // registry.
                if (settings[settingIndex].set_) {
                    settings[settingIndex].set_(widgetValue);
                }
                row.lastValue_ = widgetValue;
                continue;
            }
            if (!settings[settingIndex].get_) {
                continue;
            }
            const float settingValue = settings[settingIndex].get_();
            if (differs(row.kind_, settingValue, row.lastValue_)) {
                writeWidget(row, settingValue);
                row.lastValue_ = settingValue;
            }
        }
    }

    static bool differs(IRComponents::SettingEntry::Kind kind, float a, float b) {
        if (kind == IRComponents::SettingEntry::Kind::FLOAT) {
            return IRMath::abs(a - b) > kSettingsMenuFloatEpsilon;
        }
        return a != b;
    }

    static float readWidget(const Row &row) {
        switch (row.kind_) {
        case IRComponents::SettingEntry::Kind::BOOL:
            return IRPrefab::Widget::checkboxState(row.control_) ? 1.0f : 0.0f;
        case IRComponents::SettingEntry::Kind::ENUM:
            return static_cast<float>(IRPrefab::Widget::dropdownSelectedIndex(row.control_));
        case IRComponents::SettingEntry::Kind::FLOAT:
            return IRPrefab::Widget::sliderValue(row.control_);
        }
        return 0.0f;
    }

    static void writeWidget(const Row &row, float value) {
        switch (row.kind_) {
        case IRComponents::SettingEntry::Kind::BOOL:
            IRPrefab::Widget::setCheckboxState(row.control_, value != 0.0f);
            return;
        case IRComponents::SettingEntry::Kind::ENUM:
            IRPrefab::Widget::setDropdownSelectedIndex(row.control_, static_cast<int>(value));
            return;
        case IRComponents::SettingEntry::Kind::FLOAT:
            IRPrefab::Widget::setSliderValue(row.control_, value);
            return;
        }
    }

    // A bare label draws from its top-left, while checkbox / slider / dropdown
    // labels are vertically centered within the row. Offsetting the standalone
    // ENUM label by the same amount keeps a dropdown row's name aligned with
    // its control instead of riding high.
    static int textBaseline(int rowTop) {
        const int textHeight =
            IRRender::kGlyphHeight * IRPrefab::Widget::detail::kWidgetTextFontSize;
        return rowTop + (kSettingsMenuRowHeight - textHeight) / 2;
    }

    // The Controls line links to the help overlay rather than re-rendering the
    // key list: #2550 already owns that surface, and two renderers of one
    // registry drift apart. The key is read back out of the command registry
    // so the line stays correct in a creation that rebound the toggle, and the
    // line is omitted entirely when no help overlay is bound.
    static std::string controlsHintText() {
        const std::string helpName = IRCommand::commandNameToString(IRCommand::TOGGLE_HELP_OVERLAY);
        for (const auto &registration : IRCommand::getCommandManager().getCommandRegistrations()) {
            if (registration.name != helpName) {
                continue;
            }
            return "CONTROLS: " + IRCommand::modifierString(registration.requiredModifiers) +
                   IRCommand::keyButtonToString(registration.button);
        }
        return std::string{};
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SETTINGS_MENU_H */
