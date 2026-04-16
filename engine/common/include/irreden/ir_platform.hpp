#ifndef IR_PLATFORM_H
#define IR_PLATFORM_H

#include <irreden/math/ir_math_types.hpp>

namespace IRPlatform {

/// Graphics backend compiled into this binary.
enum class GraphicsBackend {
    OPENGL,
    METAL,
    VULKAN
};

/// Operating system detected at compile time.
enum class OperatingSystem {
    LINUX,
    MACOS,
    WINDOWS
};

/// Compile-time graphics backend, selected by the CMake-injected
/// IR_GRAPHICS_METAL / IR_GRAPHICS_VULKAN / (default) preprocessor macro.
/// Branch on this to differ behaviour across backends.  For a quick
/// boolean test, @see kIsOpenGL — but note that no corresponding
/// kIsMetal or kIsVulkan bools exist; check kGraphicsBackend directly
/// for those backends.
#if defined(IR_GRAPHICS_METAL)
inline constexpr GraphicsBackend kGraphicsBackend = GraphicsBackend::METAL;
#elif defined(IR_GRAPHICS_VULKAN)
inline constexpr GraphicsBackend kGraphicsBackend = GraphicsBackend::VULKAN;
#else
inline constexpr GraphicsBackend kGraphicsBackend = GraphicsBackend::OPENGL;
#endif

/// Compile-time operating system, detected from standard predefined macros.
#if defined(__APPLE__)
inline constexpr OperatingSystem kOperatingSystem = OperatingSystem::MACOS;
#elif defined(_WIN32)
inline constexpr OperatingSystem kOperatingSystem = OperatingSystem::WINDOWS;
#else
inline constexpr OperatingSystem kOperatingSystem = OperatingSystem::LINUX;
#endif

/// True when the OpenGL backend is active.  No equivalent bools exist for
/// Metal or Vulkan — branch on kGraphicsBackend directly for those cases.
inline constexpr bool kIsOpenGL = kGraphicsBackend == GraphicsBackend::OPENGL;

/// True when running on Linux.
inline constexpr bool kIsLinux = kOperatingSystem == OperatingSystem::LINUX;
/// True when running on macOS.
inline constexpr bool kIsMacOS = kOperatingSystem == OperatingSystem::MACOS;
/// True when running on Windows.
inline constexpr bool kIsWindows = kOperatingSystem == OperatingSystem::WINDOWS;

/// Per-backend rendering conventions bundled into a single struct so call
/// sites can read kGfx.field instead of scattering #ifdefs everywhere.
/// All fields are constexpr and branches on them compile away.
struct GraphicsConventions {
    /// Net screen-vs-iso Y direction after backend-specific clip/texture
    /// transforms.  OpenGL: -1 (Y grows down in screen space after flip);
    /// Metal/Vulkan: +1.
    float screenYDirection_;
    /// Whether to invert the GLFW mouse Y coordinate before use.  GLFW
    /// reports Y top-down; OpenGL expects bottom-up screen space, so this
    /// is true for OpenGL and false for Metal/Vulkan.
    bool flipMouseY_;
    /// Whether the GLM orthographic projection uses a [0, 1] depth range
    /// (Metal/Vulkan) or [-1, 1] (OpenGL).
    bool ndcDepthZeroToOne_;
};

/// Returns the GraphicsConventions appropriate for the given backend.
/// Used only to initialise kGfx at compile time — prefer kGfx at all
/// call sites.
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

/// The active backend's rendering conventions.  This is the primary accessor
/// for per-backend differences — use kGfx.screenYDirection_, kGfx.flipMouseY_,
/// and kGfx.ndcDepthZeroToOne_ instead of #ifdefs.  All fields are constexpr;
/// branching on them is optimised away per build.
inline constexpr GraphicsConventions kGfx = conventionsFor(kGraphicsBackend);

/// Screen-space sign vector for the isometric Y axis.  Multiply iso-space
/// Y displacements by this before submitting to the renderer.
inline constexpr IRMath::vec2 kIsoToScreenSign{1.0f, kGfx.screenYDirection_};

} // namespace IRPlatform

#endif /* IR_PLATFORM_H */
