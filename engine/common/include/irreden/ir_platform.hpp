#ifndef IR_PLATFORM_H
#define IR_PLATFORM_H

#include <irreden/math/ir_math_types.hpp>

namespace IRPlatform {

enum class GraphicsBackend {
    OPENGL,
    METAL,
    VULKAN
};

enum class OperatingSystem {
    LINUX,
    MACOS,
    WINDOWS
};

#if defined(IR_GRAPHICS_METAL)
inline constexpr GraphicsBackend kGraphicsBackend = GraphicsBackend::METAL;
#elif defined(IR_GRAPHICS_VULKAN)
inline constexpr GraphicsBackend kGraphicsBackend = GraphicsBackend::VULKAN;
#else
inline constexpr GraphicsBackend kGraphicsBackend = GraphicsBackend::OPENGL;
#endif

#if defined(__APPLE__)
inline constexpr OperatingSystem kOperatingSystem = OperatingSystem::MACOS;
#elif defined(_WIN32)
inline constexpr OperatingSystem kOperatingSystem = OperatingSystem::WINDOWS;
#else
inline constexpr OperatingSystem kOperatingSystem = OperatingSystem::LINUX;
#endif

inline constexpr bool kIsOpenGL = kGraphicsBackend == GraphicsBackend::OPENGL;

inline constexpr bool kIsLinux = kOperatingSystem == OperatingSystem::LINUX;
inline constexpr bool kIsMacOS = kOperatingSystem == OperatingSystem::MACOS;
inline constexpr bool kIsWindows = kOperatingSystem == OperatingSystem::WINDOWS;

struct GraphicsConventions {
    // Net screen-vs-iso Y direction after backend-specific clip/texture transforms.
    float screenYDirection_;
    // GLFW reports mouse Y top-down; flip when the backend expects bottom-up screen space.
    bool flipMouseY_;
    // GLM orthographic helper choice: true uses [0, 1], false uses [-1, 1].
    bool ndcDepthZeroToOne_;
};

constexpr GraphicsConventions conventionsFor(GraphicsBackend backend) {
    switch (backend) {
        case GraphicsBackend::OPENGL:
            return GraphicsConventions{-1.0f, true, false};
        case GraphicsBackend::METAL:
            return GraphicsConventions{1.0f, false, true};
        case GraphicsBackend::VULKAN:
            return GraphicsConventions{1.0f, false, true};
    }

    return GraphicsConventions{-1.0f, true, false};
}

inline constexpr GraphicsConventions kGfx = conventionsFor(kGraphicsBackend);
inline constexpr IRMath::vec2 kIsoToScreenSign{1.0f, kGfx.screenYDirection_};

} // namespace IRPlatform

#endif /* IR_PLATFORM_H */
