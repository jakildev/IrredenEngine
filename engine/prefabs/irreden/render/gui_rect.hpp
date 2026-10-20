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

inline bool isEmpty(const GuiRect &rect) {
    return rect.size_.x <= 0 || rect.size_.y <= 0;
}

// The overlap of two half-open rects; empty (zero size) when they only touch.
inline GuiRect intersection(const GuiRect &a, const GuiRect &b) {
    const IRMath::ivec2 lo = IRMath::max(a.pos_, b.pos_);
    const IRMath::ivec2 hi = IRMath::min(a.pos_ + a.size_, b.pos_ + b.size_);
    if (hi.x <= lo.x || hi.y <= lo.y) {
        return {};
    }
    return {lo, hi - lo};
}

// The smallest rect holding both; an empty rect contributes nothing.
inline GuiRect boundingBox(const GuiRect &a, const GuiRect &b) {
    if (isEmpty(a)) {
        return b;
    }
    if (isEmpty(b)) {
        return a;
    }
    const IRMath::ivec2 lo = IRMath::min(a.pos_, b.pos_);
    const IRMath::ivec2 hi = IRMath::max(a.pos_ + a.size_, b.pos_ + b.size_);
    return {lo, hi - lo};
}

// The bounding box of the part of @p rect outside @p bound; empty when
// @p rect lies inside it.
inline GuiRect outsidePart(const GuiRect &rect, const GuiRect &bound) {
    if (isEmpty(rect) || contains(bound, rect)) {
        return {};
    }
    const GuiRect inside = intersection(rect, bound);
    if (isEmpty(inside)) {
        return rect;
    }
    const IRMath::ivec2 lo = rect.pos_;
    const IRMath::ivec2 hi = rect.pos_ + rect.size_;
    const IRMath::ivec2 inLo = inside.pos_;
    const IRMath::ivec2 inHi = inside.pos_ + inside.size_;
    GuiRect outside{};
    outside = boundingBox(outside, {lo, IRMath::ivec2(inLo.x - lo.x, hi.y - lo.y)});
    outside = boundingBox(outside, {IRMath::ivec2(inHi.x, lo.y), hi - IRMath::ivec2(inHi.x, lo.y)});
    outside = boundingBox(outside, {lo, IRMath::ivec2(hi.x - lo.x, inLo.y - lo.y)});
    outside = boundingBox(outside, {IRMath::ivec2(lo.x, inHi.y), hi - IRMath::ivec2(lo.x, inHi.y)});
    return outside;
}

} // namespace IRPrefab

#endif /* GUI_RECT_H */
