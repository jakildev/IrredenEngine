set(COMMON_SRC
    src/file_dialog.cpp
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

# The native open panel (file_dialog.hpp): NSOpenPanel, IFileOpenDialog, or the
# desktop's zenity. One source per host; file_dialog.cpp is the shared half.
if(IR_isDarwin)
    list(APPEND COMMON_SRC src/file_dialog_cocoa.mm)
    find_library(IR_WINDOW_UTI_LIB UniformTypeIdentifiers REQUIRED)
    list(APPEND IR_WINDOW_PLATFORM_LIBS ${IR_WINDOW_UTI_LIB})
elseif(IR_isWindows)
    list(APPEND COMMON_SRC src/file_dialog_win32.cpp)
    # ole32: COM; uuid: the dialog's CLSID / IID definitions; shell32:
    # SHCreateItemFromParsingName.
    list(APPEND IR_WINDOW_PLATFORM_LIBS ole32 uuid shell32)
else()
    list(APPEND COMMON_SRC src/file_dialog_linux.cpp)
endif()

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG 3.3.10
    GIT_SHALLOW TRUE
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
    IrredenEngineUtility
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

IR_copyDLL(IrredenEngineWindow glfw)
