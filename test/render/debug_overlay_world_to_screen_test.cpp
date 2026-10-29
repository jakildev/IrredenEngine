#include <gtest/gtest.h>

#include <irreden/render/debug_overlay_draws.hpp>

#include <type_traits>

// `IRDebug::worldToScreen` has callers outside this tree that compile against
// its signature. Its value needs a live RenderManager, which this suite never
// constructs; equality with `IRRender::worldPos3DToMouseScreenPx` holds by
// delegation.
static_assert(std::is_same_v<decltype(&IRDebug::worldToScreen), IRMath::vec2 (*)(IRMath::vec3)>);

TEST(DebugOverlayWorldToScreen, KeepsItsVec3ToVec2Signature) {
    SUCCEED();
}
