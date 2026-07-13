#ifndef COMMAND_TOGGLE_CULLING_MINIMAP_H
#define COMMAND_TOGGLE_CULLING_MINIMAP_H

#include <irreden/ir_command.hpp>
#include <irreden/render/cull_viewport_state.hpp>

namespace IRCommand {

// The minimap-visibility flag lives in IRRender (cull_viewport_state.hpp),
// next to the cull-freeze flag it's toggled alongside (#1438 precedent —
// see command_toggle_culling_freeze.hpp), so lower layers can drive it
// without depending on the command module.
template <> struct Command<TOGGLE_CULLING_MINIMAP> {
    static auto create() {
        return []() { IRRender::setCullingMinimapEnabled(!IRRender::isCullingMinimapEnabled()); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_TOGGLE_CULLING_MINIMAP_H */
