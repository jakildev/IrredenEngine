#include <gtest/gtest.h>

#include <irreden/render/debug_overlay_draws.hpp>

#include <type_traits>

// `IRDebug::worldToScreen` is public engine API compiled against from outside
// this tree. It delegates to `IRRender::worldPos3DToMouseScreenPx`, the single
// world-to-window-pixel projection owner, so its value needs the live
// RenderManager and this lock holds only the signature.
TEST(DebugOverlayWorldToScreen, KeepsItsVec3ToVec2Signature) {
    static_assert(
        std::is_same_v<decltype(&IRDebug::worldToScreen), IRMath::vec2 (*)(IRMath::vec3)>
    );
    SUCCEED();
}
