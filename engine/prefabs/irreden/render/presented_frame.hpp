#ifndef IR_PREFAB_PRESENTED_FRAME_H
#define IR_PREFAB_PRESENTED_FRAME_H

// The image presented for the main framebuffer on the current RENDER tick.
//
// The screen passes run in a fixed order: the composite, then
// `FRAMEBUFFER_GLITCH`, then `FRAMEBUFFER_POST_EFFECTS`. Each draws an output
// image, the composite's twin, only on a tick it is enabled, and each reads
// the image presented before it. The last one that drew owns the presented
// image; `FRAMEBUFFER_TO_SCREEN` places it on screen and the recorder reads
// it, so a clip carries what the screen showed.

#include <irreden/render/glitch.hpp>
#include <irreden/render/post_effects.hpp>

namespace IRPrefab::PresentedFrame {

enum class Source { COMPOSITE, GLITCH, POST_EFFECTS };

struct MainColor {
    Source source_ = Source::COMPOSITE;
    // The presenting pass's output; null for `COMPOSITE`, where the main
    // framebuffer's own colour is presented.
    const IRRender::Texture2D *color_ = nullptr;

    bool isComposite() const {
        return source_ == Source::COMPOSITE;
    }
};

// Call after the screen passes ran this tick; before, it reports the
// previous tick's answer.
inline MainColor mainColor() {
    if (PostEffects::outputPublishedForCurrentRenderTick()) {
        return {Source::POST_EFFECTS, PostEffects::outputColorOrNull()};
    }
    if (Glitch::outputPublishedForCurrentRenderTick()) {
        return {Source::GLITCH, Glitch::presentedMainColorOrNull()};
    }
    return {};
}

} // namespace IRPrefab::PresentedFrame

#endif /* IR_PREFAB_PRESENTED_FRAME_H */
