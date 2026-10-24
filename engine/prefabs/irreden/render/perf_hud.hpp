#ifndef IR_PREFAB_PERF_HUD_H
#define IR_PREFAB_PERF_HUD_H

// Adoption surface for the player-facing performance HUD: FPS, window-minimum
// FPS, frame time, dropped frames and a frame-time graph, each coloured
// against the fixed frame budget. Hidden by default; one key toggles it.
//
// A creation adopts it with two calls, the same shape as the help overlay —
// one in its pipeline setup, one in its command setup:
//
//   // initSystems(), after TEXT_TO_TRIXEL (required, see `systems()`),
//   // before the composite:
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>());
//   renderPipeline.splice(renderPipeline.end(), IRPrefab::PerfHud::systems());
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>());
//
//   // initCommands():
//   IRPrefab::PerfHud::registerToggleCommand();   // F3 by default
//
// The toggle registers through the named command path, so the help overlay
// lists it with whichever key the creation chose.

#include <irreden/ir_command.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/commands/command_toggle_perf_hud.hpp>
#include <irreden/render/gui_rect.hpp>
#include <irreden/render/perf_hud_state.hpp>
#include <irreden/render/prefab_system_probe.hpp>
#include <irreden/render/systems/system_perf_hud.hpp>

#include <list>
#include <string>

namespace IRPrefab::PerfHud {

// Default toggle key. F3 is unbound across the engine and every in-tree
// creation; F1 is the help overlay, F7–F11 the capture and culling toggles.
inline constexpr int kDefaultToggleButton = IRInput::kKeyButtonF3;

// Splice-ready RENDER-pipeline systems for the HUD.
//
// **Precondition: `TEXT_TO_TRIXEL` must already be in this creation's RENDER
// pipeline, ahead of where this list is spliced.** It clears the GUI canvas
// and owns the shared text GPU resources the HUD's `dispatchGuiText` reuses.
// Nothing here probes for it or prepends it, for the reason
// `HelpOverlay::systems()` documents: while the adopter is still assembling
// its pipeline, "absent" and "about to be spliced in" are indistinguishable,
// and registering `TEXT_TO_TRIXEL` twice re-creates its named GPU resources.
// A mis-ordered adopter draws nothing rather than crashing.
inline std::list<IRSystem::SystemId> systems() {
    return {IRSystem::System<IRSystem::PERF_HUD>::create()};
}

// Binds the HUD toggle to @p button (F3 by default), mirroring
// `HelpOverlay::registerToggleCommand()`.
inline IRCommand::CommandId registerToggleCommand(int button = kDefaultToggleButton) {
    return IRCommand::createCommand<IRCommand::TOGGLE_PERF_HUD>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        button
    );
}

// --- Headless-test introspection -------------------------------------------
//
// The observables a GUI test needs to prove the HUD actually fired: the text it
// built, how many glyph commands it batched on the last frame, and where its
// panel landed. All resolve the running system through `detail::systemOrNull`
// (`prefab_system_probe.hpp`, which owns the sentinel contract) and degrade to
// empty / 0 when it isn't registered.
inline const IRSystem::System<IRSystem::PERF_HUD> *systemOrNull() {
    return IRPrefab::detail::systemOrNull<IRSystem::PERF_HUD>();
}

// The two HUD lines as last formatted, newline-separated. Empty while hidden
// or when the system is absent.
inline std::string builtText() {
    const auto *system = systemOrNull();
    return system == nullptr ? std::string{} : system->text_;
}

// Glyph draw commands batched on the most recent frame; 0 while hidden.
inline int lastGlyphCommandCount() {
    const auto *system = systemOrNull();
    return system == nullptr ? 0 : system->lastGlyphCommandCount_;
}

using GuiRect = IRPrefab::GuiRect;

// The background panel as last painted. Zero-sized when the HUD has never
// been drawn or the system is absent.
inline GuiRect panelRect() {
    const auto *system = systemOrNull();
    return system == nullptr ? GuiRect{} : GuiRect{system->panelPos_, system->panelSize_};
}

} // namespace IRPrefab::PerfHud

#endif /* IR_PREFAB_PERF_HUD_H */
