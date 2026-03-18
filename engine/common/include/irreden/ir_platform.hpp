#ifndef IR_PLATFORM_H
#define IR_PLATFORM_H

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

inline constexpr bool kIsMetal = kGraphicsBackend == GraphicsBackend::METAL;
inline constexpr bool kIsOpenGL = kGraphicsBackend == GraphicsBackend::OPENGL;
inline constexpr bool kIsVulkan = kGraphicsBackend == GraphicsBackend::VULKAN;

inline constexpr bool kIsLinux = kOperatingSystem == OperatingSystem::LINUX;
inline constexpr bool kIsMacOS = kOperatingSystem == OperatingSystem::MACOS;
inline constexpr bool kIsWindows = kOperatingSystem == OperatingSystem::WINDOWS;

inline constexpr float kScreenYDirection = kIsMetal ? 1.0f : -1.0f;
inline constexpr bool kFlipMouseY = !kIsMetal;

} // namespace IRPlatform

#endif /* IR_PLATFORM_H */
