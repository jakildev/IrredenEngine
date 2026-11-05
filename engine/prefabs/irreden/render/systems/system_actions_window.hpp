#ifndef SYSTEM_ACTIONS_WINDOW_H
#define SYSTEM_ACTIONS_WINDOW_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/actions_window_layout.hpp>
#include <irreden/render/components/component_actions_window.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/trixel_text.hpp>
#include <irreden/render/widgets.hpp>

#include <string>
#include <vector>

namespace IRSystem {

// The Actions window: one searchable list of every registered action with its
// resolved chords, and the controls to run an action, assign or add a chord,
// remove one, restore defaults, and settle a conflict by replacing or
// cancelling.
//
// Built in `endTick` on the frame the window opens and destroyed on the frame
// it closes, so a closed window owns no entities and costs one bool read.
//
// Assigning a chord focuses a capture field, which makes `WIDGET_INPUT` capture
// the keyboard: neither the pressed chord's current owner nor any widget hotkey
// fires while the window listens. The first non-modifier key pressed with the
// held modifiers becomes the chord; Escape cancels.
//
// Pipeline placement: INPUT, after the `WIDGET_APPLY_*` chain (it polls the
// list selection and the search text they commit). Its widgets are drawn by
// the ordinary panel, label, button, list and text-input renderers.
template <> struct System<ACTIONS_WINDOW> {
    struct Params {
        /// Top-left corner of the window; negative centres it on the GUI canvas.
        IRMath::ivec2 origin_ = IRMath::ivec2(-1);
        int zOrder_ = 900;
    };

    enum class Mode { BROWSE, CAPTURE, CONFLICT };

    Params params_;
    bool open_ = false;
    bool built_ = false;
    Mode mode_ = Mode::BROWSE;
    bool captureAdds_ = false;
    IRCommand::KeyChord pendingChord_{};
    IRPrefab::ActionsWindow::Layout layout_{};

    std::string query_;
    std::uint32_t listedGeneration_ = ~std::uint32_t{0};
    std::vector<std::string> rowIds_;
    std::vector<std::string> rowText_;
    std::string selectedId_;
    std::string status_;

    IREntity::EntityId panel_ = IREntity::kNullEntity;
    IREntity::EntityId search_ = IREntity::kNullEntity;
    IREntity::EntityId list_ = IREntity::kNullEntity;
    IREntity::EntityId statusLabel_ = IREntity::kNullEntity;
    IREntity::EntityId capture_ = IREntity::kNullEntity;
    IREntity::EntityId replace_ = IREntity::kNullEntity;
    IREntity::EntityId cancel_ = IREntity::kNullEntity;
    std::array<IREntity::EntityId, IRPrefab::ActionsWindow::kButtonCount> buttons_{};

    void beginTick() {
        open_ = false;
    }

    void tick(const IRComponents::C_ActionsWindowState &state) {
        open_ = state.open_;
    }

    void endTick() {
        if (open_ && !built_) {
            build();
        } else if (!open_ && built_) {
            destroy();
        } else if (open_) {
            update();
        }
    }

    static SystemId create() {
        return create(Params{});
    }

    static SystemId create(const Params &initialParams) {
        IREntity::singleton<IRComponents::C_ActionsWindowState>();
        IRPrefab::Widget::ensureThemeSingleton();
        IRPrefab::Widget::ensureFocusRequestSingleton();
        const SystemId systemId =
            registerSystem<ACTIONS_WINDOW, IRComponents::C_ActionsWindowState, MainThread>(
                "ActionsWindow"
            );
        auto *params = getSystemParams<System<ACTIONS_WINDOW>>(systemId);
        params->params_ = initialParams;
        // Widget ids are not C_Persistent, so resetGameplay() can destroy the
        // window under an open state; forget the built state instead of
        // reaching through dead ids, and rebuild on the next open frame.
        IREntity::getEntityManager().registerPreDestroyHook([params](IREntity::EntityId destroyed) {
            if (params->panel_ == destroyed) {
                params->forget();
            }
        });
        return systemId;
    }

    IREntity::EntityId button(IRPrefab::ActionsWindow::Button which) const {
        return buttons_[static_cast<std::size_t>(which)];
    }

  private:
    void build() {
        IRMath::ivec2 origin = params_.origin_;
        if (origin.x < 0 || origin.y < 0) {
            const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
            const IRMath::ivec2 canvasSize =
                guiCanvas == IREntity::kNullEntity
                    ? IRPrefab::ActionsWindow::kSize
                    : IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas)
                          .size_;
            origin = IRPrefab::ActionsWindow::centeredOrigin(canvasSize);
        }
        layout_ = IRPrefab::ActionsWindow::layoutAt(origin);
        const int z = params_.zOrder_;
        const auto raise = [z](IREntity::EntityId id) {
            IREntity::getComponent<IRComponents::C_Widget>(id).zOrder_ = z;
            return id;
        };
        panel_ = IRPrefab::Widget::makePanel(
            layout_.panel_.pos_,
            layout_.panel_.size_,
            "ACTIONS",
            true,
            z - 1
        );
        IREntity::setComponent(panel_, IRComponents::C_HitBox2DGui{layout_.panel_.size_});
        search_ = raise(
            IRPrefab::Widget::makeTextInput(layout_.search_.pos_, layout_.search_.size_, "", 48)
        );
        list_ = raise(
            IRPrefab::Widget::makeList(
                layout_.list_.pos_,
                layout_.list_.size_,
                {},
                -1,
                IRPrefab::ActionsWindow::kListItemHeight,
                IRRender::TextSize::SMALL
            )
        );
        statusLabel_ = IRPrefab::Widget::makeLabel(
            layout_.status_.pos_ + IRMath::ivec2(0, 4),
            "",
            IRMath::Color{0, 0, 0, 0},
            IRRender::TextSize::SMALL
        );
        capture_ = raise(
            IRPrefab::Widget::makeTextInput(layout_.capture_.pos_, layout_.capture_.size_, "", 1)
        );
        replace_ = raise(
            IRPrefab::Widget::makeButton(
                layout_.replace_.pos_,
                layout_.replace_.size_,
                "REPLACE",
                IRRender::TextSize::SMALL
            )
        );
        cancel_ = raise(
            IRPrefab::Widget::makeButton(
                layout_.cancel_.pos_,
                layout_.cancel_.size_,
                "CANCEL",
                IRRender::TextSize::SMALL
            )
        );
        constexpr const char *kLabels[IRPrefab::ActionsWindow::kButtonCount] =
            {"RUN", "ASSIGN", "ADD KEY", "REMOVE", "RESET", "CLOSE"};
        for (int i = 0; i < IRPrefab::ActionsWindow::kButtonCount; ++i) {
            const IRPrefab::GuiRect rect = layout_.buttons_[static_cast<std::size_t>(i)];
            buttons_[static_cast<std::size_t>(i)] = raise(
                IRPrefab::Widget::makeButton(
                    rect.pos_,
                    rect.size_,
                    kLabels[i],
                    IRRender::TextSize::SMALL
                )
            );
        }
        mode_ = Mode::BROWSE;
        query_.clear();
        selectedId_.clear();
        listedGeneration_ = ~std::uint32_t{0};
        refreshList();
        setStatus("TYPE TO SEARCH BY NAME OR KEY");
        showPrompt();
        IRPrefab::Widget::focus(search_);
        built_ = true;
    }

    void destroy() {
        for (IREntity::EntityId &id : buttons_) {
            destroyIfLive(id);
        }
        destroyIfLive(search_);
        destroyIfLive(list_);
        destroyIfLive(statusLabel_);
        destroyIfLive(capture_);
        destroyIfLive(replace_);
        destroyIfLive(cancel_);
        destroyIfLive(panel_);
        forget();
    }

    void forget() {
        built_ = false;
        mode_ = Mode::BROWSE;
        panel_ = search_ = list_ = statusLabel_ = capture_ = replace_ = cancel_ =
            IREntity::kNullEntity;
        buttons_.fill(IREntity::kNullEntity);
        rowIds_.clear();
        rowText_.clear();
    }

    static void destroyIfLive(IREntity::EntityId &entity) {
        if (entity != IREntity::kNullEntity) {
            IREntity::destroyEntity(entity);
            entity = IREntity::kNullEntity;
        }
    }

    void setStatus(std::string text) {
        status_ = std::move(text);
        if (statusLabel_ != IREntity::kNullEntity) {
            IRPrefab::Widget::setLabelText(statusLabel_, fitText(status_, layout_.status_.size_.x));
        }
    }

    static std::string fitText(std::string text, int width) {
        if (IRRender::measureText(text, 0, IRRender::TextSize::SMALL).x <= width) {
            return text;
        }
        while (!text.empty() &&
               IRRender::measureText(text + "..", 0, IRRender::TextSize::SMALL).x > width) {
            text.pop_back();
        }
        return text + "..";
    }

    std::string selectedLabel() const {
        const auto info = IRCommand::actions().findAction(selectedId_);
        return info ? info->label_ : selectedId_;
    }

    void showPrompt() {
        IRPrefab::Widget::setVisible(capture_, mode_ == Mode::CAPTURE);
        IRPrefab::Widget::setVisible(replace_, mode_ == Mode::CONFLICT);
        IRPrefab::Widget::setVisible(cancel_, mode_ == Mode::CONFLICT);
        if (mode_ == Mode::CAPTURE) {
            IRPrefab::Widget::focus(capture_);
        }
    }

    void refreshList() {
        const std::vector<IRCommand::ActionInfo> found = IRCommand::actions().searchActions(query_);
        rowIds_.clear();
        rowText_.clear();
        rowIds_.reserve(found.size());
        rowText_.reserve(found.size());
        const int width = layout_.list_.size_.x - 8;
        int selected = -1;
        for (const IRCommand::ActionInfo &info : found) {
            std::string keys;
            for (const IRCommand::KeyChord &chord : info.chords_) {
                keys += (keys.empty() ? "" : " ") + IRCommand::chordLabel(chord);
            }
            std::string text = info.section_ + ": " + info.label_;
            if (!keys.empty()) {
                text += " = " + keys;
            }
            if (info.id_ == selectedId_) {
                selected = static_cast<int>(rowIds_.size());
            }
            rowIds_.push_back(info.id_);
            rowText_.push_back(text);
        }
        std::vector<std::string> items;
        items.reserve(rowText_.size());
        for (const std::string &text : rowText_) {
            items.push_back(fitText(text, width));
        }
        IRPrefab::Widget::setListItems(list_, std::move(items));
        IRPrefab::Widget::setListSelectedIndex(list_, selected);
        if (selected < 0) {
            selectedId_.clear();
        }
        listedGeneration_ = IRCommand::actions().generation();
    }

    void update() {
        if (IRPrefab::Widget::wasClicked(button(IRPrefab::ActionsWindow::Button::CLOSE))) {
            IREntity::singleton<IRComponents::C_ActionsWindowState>().open_ = false;
            destroy();
            return;
        }
        const std::string &text = IRPrefab::Widget::textInputValue(search_);
        if (text != query_ || listedGeneration_ != IRCommand::actions().generation()) {
            query_ = text;
            refreshList();
        }
        const int index = IRPrefab::Widget::listSelectedIndex(list_);
        selectedId_ = index >= 0 && index < static_cast<int>(rowIds_.size())
                          ? rowIds_[static_cast<std::size_t>(index)]
                          : std::string{};

        switch (mode_) {
        case Mode::BROWSE:
            browse();
            break;
        case Mode::CAPTURE:
            capture();
            break;
        case Mode::CONFLICT:
            if (IRPrefab::Widget::wasClicked(replace_)) {
                applyChord(pendingChord_, IRCommand::ConflictPolicy::REPLACE);
            } else if (IRPrefab::Widget::wasClicked(cancel_)) {
                mode_ = Mode::BROWSE;
                setStatus("KEPT THE EXISTING BINDING");
                showPrompt();
            }
            break;
        }
        const bool hasSelection = !selectedId_.empty();
        for (int i = 0; i < IRPrefab::ActionsWindow::kButtonCount; ++i) {
            const auto which = static_cast<IRPrefab::ActionsWindow::Button>(i);
            const bool needsSelection = which != IRPrefab::ActionsWindow::Button::CLOSE;
            IRPrefab::Widget::setDisabled(
                buttons_[static_cast<std::size_t>(i)],
                needsSelection && (!hasSelection || mode_ != Mode::BROWSE)
            );
        }
    }

    void browse() {
        using IRPrefab::ActionsWindow::Button;
        if (selectedId_.empty()) {
            return;
        }
        if (IRPrefab::Widget::wasClicked(button(Button::RUN))) {
            setStatus(
                IRCommand::invokeAction(selectedId_) ? "RAN " + selectedLabel()
                                                     : selectedLabel() + " IS UNAVAILABLE"
            );
        } else if (
            IRPrefab::Widget::wasClicked(button(Button::ASSIGN)) ||
            IRPrefab::Widget::wasClicked(button(Button::ADD))
        ) {
            captureAdds_ = IRPrefab::Widget::wasClicked(button(Button::ADD));
            mode_ = Mode::CAPTURE;
            setStatus("PRESS KEYS FOR " + selectedLabel() + ", ESC CANCELS");
            showPrompt();
        } else if (IRPrefab::Widget::wasClicked(button(Button::REMOVE))) {
            const auto info = IRCommand::actions().findAction(selectedId_);
            if (info && !info->chords_.empty()) {
                const IRCommand::KeyChord last = info->chords_.back();
                IRCommand::actions().removeChord(selectedId_, last);
                setStatus("REMOVED " + IRCommand::chordLabel(last));
            } else {
                setStatus(selectedLabel() + " HAS NO KEY");
            }
        } else if (IRPrefab::Widget::wasClicked(button(Button::RESET))) {
            IRCommand::actions().restoreDefaults(selectedId_);
            setStatus("RESTORED DEFAULT KEYS");
        }
    }

    void capture() {
        IRPrefab::Widget::setTextInputValue(capture_, "");
        if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonEscape, IRInput::PRESSED)) {
            mode_ = Mode::BROWSE;
            setStatus("CANCELLED");
            showPrompt();
            return;
        }
        const IRInput::KeyModifierMask held = static_cast<IRInput::KeyModifierMask>(
            IRInput::heldKeyModifiers() & IRInput::kModifierAll
        );
        for (int key = static_cast<int>(IRInput::kNullButton) + 1;
             key < static_cast<int>(IRInput::kMouseButtonNull);
             ++key) {
            const auto button = static_cast<IRInput::KeyMouseButtons>(key);
            if (!IRInput::checkKeyMouseButton(button, IRInput::PRESSED)) {
                continue;
            }
            const IRCommand::KeyChord chord{button, held};
            if (!IRCommand::isAssignableChord(chord)) {
                continue;
            }
            applyChord(chord, IRCommand::ConflictPolicy::REJECT);
            return;
        }
    }

    void applyChord(IRCommand::KeyChord chord, IRCommand::ConflictPolicy policy) {
        IRCommand::ActionRegistry &actions = IRCommand::actions();
        const auto info = actions.findAction(selectedId_);
        if (!info) {
            mode_ = Mode::BROWSE;
            showPrompt();
            return;
        }
        const IRCommand::BindResult result =
            captureAdds_ || info->chords_.empty()
                ? actions.addChord(selectedId_, chord, policy)
                : actions.replaceChord(selectedId_, info->chords_.front(), chord, policy);
        const std::string label = IRCommand::chordLabel(chord);
        mode_ = Mode::BROWSE;
        if (result.ok()) {
            setStatus("BOUND " + label + " TO " + info->label_);
        } else if (result.status_ == IRCommand::BindStatus::ALREADY_BOUND) {
            setStatus(info->label_ + " ALREADY USES " + label);
        } else if (result.status_ == IRCommand::BindStatus::CONFLICT) {
            std::string owners;
            bool fixed = false;
            for (const IRCommand::ActionConflict &conflict : result.conflicts_) {
                fixed = fixed || conflict.actionId_.empty();
                owners += (owners.empty() ? "" : ", ") + conflict.label_;
            }
            if (fixed) {
                setStatus(label + " IS A FIXED COMMAND KEY");
            } else {
                pendingChord_ = chord;
                mode_ = Mode::CONFLICT;
                setStatus(label + " IS USED BY " + owners);
            }
        } else {
            setStatus(label + " CANNOT BE ASSIGNED");
        }
        showPrompt();
    }
};

} // namespace IRSystem

#endif /* SYSTEM_ACTIONS_WINDOW_H */
