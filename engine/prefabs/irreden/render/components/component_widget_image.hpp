#ifndef COMPONENT_WIDGET_IMAGE_H
#define COMPONENT_WIDGET_IMAGE_H

#include <irreden/ir_math.hpp>

#include <string>
#include <vector>

namespace IRComponents {

// IMAGE — a PNG resampled into GUI-canvas texels when the widget is built or
// its path or size changes, never in a render tick. `texels_` is row-major,
// `texelSize_` wide and tall, and covers the widget's whole rect: the image is
// centred on the theme background, or, when the file could not be decoded,
// `texels_` holds the placeholder hatch, `loaded_` is false and `error_`
// names the path. `WIDGET_RENDER_IMAGE` writes `texels_` verbatim.
struct C_WidgetImage {
    std::string path_;
    IRMath::ivec2 texelSize_ = IRMath::ivec2(0);
    std::vector<IRMath::Color> texels_;
    bool loaded_ = false;
    std::string error_;
};

} // namespace IRComponents

#endif /* COMPONENT_WIDGET_IMAGE_H */
