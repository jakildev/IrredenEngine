#ifndef FILE_DROP_H
#define FILE_DROP_H

#include <irreden/ir_math.hpp>

#include <string>
#include <vector>

namespace IRWindow {

/// One drag-and-drop of files or folders from the host onto the engine window.
struct FileDropEvent {
    // Absolute UTF-8 paths, in the order the host reported them.
    std::vector<std::string> paths_;
    // Cursor at the drop: logical window points, top-left origin; never
    // framebuffer pixels.
    IRMath::dvec2 windowPoint_{0.0, 0.0};
};

} // namespace IRWindow

#endif /* FILE_DROP_H */
