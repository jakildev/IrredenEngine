#ifndef COMMAND_TOGGLE_SETTINGS_MENU_H
#define COMMAND_TOGGLE_SETTINGS_MENU_H

#include <irreden/ir_command.hpp>

#include <irreden/render/components/component_settings_menu.hpp>

namespace IRCommand {

template <> struct Command<TOGGLE_SETTINGS_MENU> {
    static auto create() {
        return []() { IRPrefab::SettingsMenu::toggleOpen(); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_TOGGLE_SETTINGS_MENU_H */
