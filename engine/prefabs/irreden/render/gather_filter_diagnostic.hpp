#ifndef GATHER_FILTER_DIAGNOSTIC_H
#define GATHER_FILTER_DIAGNOSTIC_H

#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>

#include <cstdlib>
#include <string_view>

namespace IRPrefab::GatherFilter {

// The gather colour resolve selected by `IR_GATHER_FILTER_DIAGNOSTIC`
// (`unrestricted` or `owner`; unset or anything else is nearest). A
// measurement switch read once per system at creation, never a presentation
// setting: docs/design/continuous-camera-zoom.md § "Filtered minification".
inline IRRender::GatherFilterDiagnostic diagnosticFromEnvironment() {
    const char *value = std::getenv("IR_GATHER_FILTER_DIAGNOSTIC");
    if (value == nullptr) {
        return IRRender::GatherFilterDiagnostic::NEAREST;
    }
    const std::string_view name(value);
    IRRender::GatherFilterDiagnostic mode = IRRender::GatherFilterDiagnostic::NEAREST;
    if (name == "unrestricted") {
        mode = IRRender::GatherFilterDiagnostic::UNRESTRICTED;
    } else if (name == "owner") {
        mode = IRRender::GatherFilterDiagnostic::OWNER;
    }
    IR_LOG_INFO(
        "[gather-filter] diagnostic={} (IR_GATHER_FILTER_DIAGNOSTIC={})",
        static_cast<int>(mode),
        name
    );
    return mode;
}

// The uniform value for one gather draw. Only a RECTANGULAR canvas that moves
// and scales with the camera is filtered: the GUI canvas, secondary viewports
// and screen-locked overlays keep the nearest resolve.
inline int diagnosticForDraw(
    IRRender::GatherFilterDiagnostic mode, bool followsCamera, IRRender::TrixelSampleLayout layout
) {
    return followsCamera && layout == IRRender::TrixelSampleLayout::RECTANGULAR
               ? static_cast<int>(mode)
               : 0;
}

} // namespace IRPrefab::GatherFilter

#endif /* GATHER_FILTER_DIAGNOSTIC_H */
