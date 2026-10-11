#ifndef GUI_RECT_H
#define GUI_RECT_H

#include <irreden/ir_math.hpp>

namespace IRPrefab {

struct GuiRect {
    IRMath::ivec2 pos_ = IRMath::ivec2(0);
    IRMath::ivec2 size_ = IRMath::ivec2(0);
};

inline bool contains(const GuiRect &outer, const GuiRect &inner) {
    return inner.size_.x > 0 && inner.size_.y > 0 && inner.pos_.x >= outer.pos_.x &&
           inner.pos_.y >= outer.pos_.y &&
           inner.pos_.x + inner.size_.x <= outer.pos_.x + outer.size_.x &&
           inner.pos_.y + inner.size_.y <= outer.pos_.y + outer.size_.y;
}

inline bool intersects(const GuiRect &a, const GuiRect &b) {
    return a.pos_.x < b.pos_.x + b.size_.x && a.pos_.x + a.size_.x > b.pos_.x &&
           a.pos_.y < b.pos_.y + b.size_.y && a.pos_.y + a.size_.y > b.pos_.y;
}

} // namespace IRPrefab

#endif /* GUI_RECT_H */
