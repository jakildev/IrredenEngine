#ifndef COMPONENT_TEXT_SEGMENT_H
#define COMPONENT_TEXT_SEGMENT_H

#include <string>

namespace IRComponents {

struct C_TextSegment {
    std::string text_;

    C_TextSegment(std::string text) : text_(text) {}

    // Default
    C_TextSegment() : text_("This is the default text segment...") {}
};

} // namespace IRComponents

#endif /* COMPONENT_TEXT_SEGMENT_H */
