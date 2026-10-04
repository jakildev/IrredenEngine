set(COMMON_SRC
    src/ir_glfw_window.cpp
    src/ir_window.cpp
)

# Window-mode platform hooks (ir_window_platform.hpp): the Cocoa half needs
# Objective-C++ for the activation policy, every other host compiles the
# plain C++ file (Win32 z-order push, X11 no-op).
if(IR_isDarwin)
    list(APPEND COMMON_SRC src/ir_window_platform_cocoa.mm)
    find_library(IR_WINDOW_APPKIT_LIB AppKit REQUIRED)
    set(IR_WINDOW_PLATFORM_LIBS ${IR_WINDOW_APPKIT_LIB})
else()
    list(APPEND COMMON_SRC src/ir_window_platform.cpp)
    set(IR_WINDOW_PLATFORM_LIBS "")
endif()

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG 3.3.10
)
FetchContent_MakeAvailable(glfw)


add_library(IrredenEngineWindow STATIC
    ${COMMON_SRC}

    # unused atm
    # ${PLATFORM_SRC}
)
IrredenEngine_setGraphicsBackendCompileDefinitions(IrredenEngineWindow)

target_compile_options(
    IrredenEngineWindow
    PUBLIC
    -O3
)

target_link_libraries(
    IrredenEngineWindow PUBLIC
    IrredenEngineProfile
    IrredenEngineMath
    IrredenEngineEntity
    IrredenEngineSystem

    glfw
    ${IR_WINDOW_PLATFORM_LIBS}
)
target_include_directories(
    IrredenEngineWindow PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

IR_copyDLL(
    IrredenEngineWindow
    glfw3d
    ${glfw_BINARY_DIR}/src
)
