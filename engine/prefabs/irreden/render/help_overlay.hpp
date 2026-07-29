#ifndef IR_PREFAB_HELP_OVERLAY_H
#define IR_PREFAB_HELP_OVERLAY_H

// Adoption surface for the registry-driven command help overlay (#2550).
//
// A creation adopts the overlay with two calls — one in its pipeline setup,
// one in its command setup:
//
//   // initSystems(), at the point the overlay should draw — after
//   // TEXT_TO_TRIXEL (required, see `systems()` below), before the composite:
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>());
//   renderPipeline.splice(renderPipeline.end(), IRPrefab::HelpOverlay::systems());
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>());
//
//   // initCommands():
//   IRPrefab::HelpOverlay::registerToggleCommand();   // F1 by default
//
// Content is whatever the creation actually registered: every
// `IRCommand::createCommand<NAME>(...)` records its display name and
// description automatically, so the camera-control bundle appears described
// with no per-creation wiring, and a demo's own keys appear as soon as they
// are registered through the named path.

#include <irreden/ir_command.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/commands/command_toggle_help_overlay.hpp>
#include <irreden/render/components/component_help_overlay.hpp>
#include <irreden/render/systems/system_help_overlay.hpp>

#include <list>
#include <string>

namespace IRPrefab::HelpOverlay {

// Default toggle key. F1 is unbound across the engine and every in-tree
// creation, and Escape is CLOSE_WINDOW everywhere (`command_suite_camera.hpp`)
// so it is not available.
inline constexpr int kDefaultToggleButton = IRInput::kKeyButtonF1;

// Splice-ready RENDER-pipeline systems for the overlay.
//
// **Precondition: `TEXT_TO_TRIXEL` must already be in this creation's RENDER
// pipeline, ahead of where this list is spliced.** It clears the GUI canvas
// and owns the shared text GPU resources (`TextToTrixelProgram`,
// `GlyphDrawCommandBuffer`) that the overlay's `dispatchGuiText` reuses — the
// same ordering every `WIDGET_RENDER_*` system requires. A creation that has
// no GUI text yet adds one line before splicing:
//
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>());
//   renderPipeline.splice(renderPipeline.end(), IRPrefab::HelpOverlay::systems());
//
// This deliberately does NOT auto-detect and prepend `TEXT_TO_TRIXEL`: both
// available probes are unsound. `IRRender::getNamedResource` asserts on a
// missing name rather than returning null (and the assert compiles out under
// `IR_RELEASE`, leaving an end-iterator dereference), and
// `IRSystem::findSystem`'s "never registered" answer is `kNullEntity` == 0,
// which is also a legitimate first-system id (#2540). Registering
// `TEXT_TO_TRIXEL` twice re-creates its named GPU resources, so a wrong guess
// is fatal — an explicit precondition is cheaper than an unsound probe. The
// list form is kept (rather than returning the bare id) so the overlay can
// grow a second system without touching any adopter.
inline std::list<IRSystem::SystemId> systems() {
    return {IRSystem::System<IRSystem::HELP_OVERLAY>::create()};
}

// Binds the overlay toggle to @p button (F1 by default), mirroring the shape
// of `IRPrefab::Camera::registerStandardKeyboardCommands()`. The binding
// registers through the named path, so the overlay lists its own toggle key —
// it is self-documenting once open.
inline IRCommand::CommandId registerToggleCommand(int button = kDefaultToggleButton) {
    return IRCommand::createCommand<IRCommand::TOGGLE_HELP_OVERLAY>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        button
    );
}

// --- Headless-test introspection -------------------------------------------
//
// The two observables a GUI test needs to prove the overlay actually fired,
// rather than merely believing itself visible: the text it built, and how many
// glyph commands it batched on the last frame. Both resolve the running system
// through the `SystemName` registry (#2526) and degrade to empty / 0 when it
// isn't registered, so a creation without the overlay reads a clean negative
// instead of dereferencing null.
//
// Not a per-frame surface — one hash lookup per call. Intended for
// `IRPrefab::GuiTest::predicate` bodies and diagnostics.
inline const IRSystem::System<IRSystem::HELP_OVERLAY> *systemOrNull() {
    const IRSystem::SystemId id = IRSystem::findSystem(IRSystem::HELP_OVERLAY);
    if (id == IREntity::kNullEntity) {
        return nullptr;
    }
    return IRSystem::getSystemParams<IRSystem::System<IRSystem::HELP_OVERLAY>>(id);
}

// The overlay text as last built from the command registry. Empty when the
// overlay has never been opened (the build is lazy) or the system is absent.
inline std::string builtText() {
    const auto *system = systemOrNull();
    return system == nullptr ? std::string{} : system->text_;
}

// Glyph draw commands batched on the most recent frame; 0 while hidden.
inline int lastGlyphCommandCount() {
    const auto *system = systemOrNull();
    return system == nullptr ? 0 : system->lastGlyphCommandCount_;
}

} // namespace IRPrefab::HelpOverlay

#endif /* IR_PREFAB_HELP_OVERLAY_H */
