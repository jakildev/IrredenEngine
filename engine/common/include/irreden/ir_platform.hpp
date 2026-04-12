#ifndef IR_PLATFORM_H
#define IR_PLATFORM_H

#include <irreden/math/ir_math_types.hpp>

namespace IRPlatform {

/// Identifies which GPU graphics API the engine is compiled against.
/// Selected at build time via the `IR_GRAPHICS_METAL` or `IR_GRAPHICS_VULKAN`
/// preprocessor defines; defaults to OpenGL when neither is defined.
enum class GraphicsBackend {
    OPENGL,
    METAL,
    VULKAN
};

/// Identifies the host operating system at compile time.
/// Derived from standard predefined macros (`__APPLE__`, `_WIN32`);
/// defaults to Linux when neither is defined.
enum class OperatingSystem {
    LINUX,
    MACOS,
    WINDOWS
};

/// Compile-time graphics backend selected for this build.
#if defined(IR_GRAPHICS_METAL)
inline constexpr GraphicsBackend kGraphicsBackend = GraphicsBackend::METAL;
#elif defined(IR_GRAPHICS_VULKAN)
inline constexpr GraphicsBackend kGraphicsBackend = GraphicsBackend::VULKAN;
#else
inline constexpr GraphicsBackend kGraphicsBackend = GraphicsBackend::OPENGL;
#endif

/// Compile-time operating system selected for this build.
#if defined(__APPLE__)
inline constexpr OperatingSystem kOperatingSystem = OperatingSystem::MACOS;
#elif defined(_WIN32)
inline constexpr OperatingSystem kOperatingSystem = OperatingSystem::WINDOWS;
#else
inline constexpr OperatingSystem kOperatingSystem = OperatingSystem::LINUX;
#endif

/// True when the active backend is OpenGL.
inline constexpr bool kIsOpenGL = kGraphicsBackend == GraphicsBackend::OPENGL;

/// True when building for Linux (WSL or native).
inline constexpr bool kIsLinux = kOperatingSystem == OperatingSystem::LINUX;
/// True when building for macOS.
inline constexpr bool kIsMacOS = kOperatingSystem == OperatingSystem::MACOS;
/// True when building for Windows (MSYS2 / native).
inline constexpr bool kIsWindows = kOperatingSystem == OperatingSystem::WINDOWS;

/// Per-backend coordinate-convention constants used to reconcile differences
/// between OpenGL and Metal/Vulkan clip-space and screen-space orientations.
struct GraphicsConventions {
    /// Sign applied to the iso-space Y axis when converting to screen space.
    /// OpenGL clip-space is Y-up, so iso +Y must flip to -1 to go down the
    /// screen. Metal and Vulkan are Y-down, so no flip is needed (+1).
    float screenYDirection_;
    /// Whether to negate GLFW's top-down mouse Y before use.
    /// OpenGL expects bottom-up screen coordinates, so the mouse Y must be
    /// flipped. Metal/Vulkan are already top-down, matching GLFW directly.
    bool flipMouseY_;
    /// Which GLM orthographic depth range to use: true → [0, 1] (Metal/Vulkan
    /// NDC convention), false → [-1, 1] (OpenGL NDC convention).
    bool ndcDepthZeroToOne_;
};

/// Returns the GraphicsConventions for a given backend.
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

/// Compile-time graphics conventions for the active backend.
/// Use `kGfx.screenYDirection_` etc. instead of querying the backend enum
/// directly — it centralises all backend-specific sign logic here.
inline constexpr GraphicsConventions kGfx = conventionsFor(kGraphicsBackend);

/// Per-component sign vector for converting an iso-space (x, y) vector to
/// screen space. X is always +1 (iso and screen X agree); Y uses
/// `kGfx.screenYDirection_` to account for the Y-axis flip on OpenGL.
inline constexpr IRMath::vec2 kIsoToScreenSign{1.0f, kGfx.screenYDirection_};

} // namespace IRPlatform

#endif /* IR_PLATFORM_H */
