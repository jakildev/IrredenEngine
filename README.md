# Irreden Engine
![Banner image](/docs/images/irriden_engine_baner.png)

## About
The Irreden Engine is an isometric "pixelatable" voxel content and game engine.

Created by and maintained by [jakildev](https://github.com/jakildev).

## Build
- Windows: CMake, IRCreationDefault launch target.
- MacOS: bazel (WIP)

### Prerequisites
-   Windows OS
-   OpenGL 4.6 supported hardware
-   MinGW (or other c/c++ compiler)
-   CMake

### Instructions
-   Clone the repository.
-   Configure cmake.
-   Build all targets.
-   Launch a demo creation.


## Content and Socials
| Platform | Account | Usage |
| -------- | ----- | ---- | 
| TikTok |  [@jakildev](https://www.tiktok.com/@jakildev) | Creations that I make. |
| Instagram | [@jakildev](https://www.instagram.com/jakildev) | More of my life. |
| Youtube | [@jakildev](https://www.youtube.com/@jakildev) | Future long-form informative videos. | 

## Modules and Key Features
- Modules are built individually as static libraries and linked to dependant modules and third-party libraries.
- Modules are are included by other modules via a single ir_\<module_name\>.hpp file.
- Check out each module link for a complete list of features and developement roadmap. (TODO)

| Module | Features |
| ------ | -------- |
| IRAudio | - Probe and connect to audio devices including audio interfaces. <br> - Interface midi input channels and messages to be used as controllers for custom commands. <br> - Realtime audio streams from audio devices (WIP). |
| IRCommand | - Define commands with lambdas that are triggered by a variety of input sources. |
| IRECS | - Cache-efficient relational archetype-based entity-component-system. <br> - Define systems with lambdas that operate on entities containing specific components/relations. <br> - Create entities in batches with lambdas defining variable initialization of components. |
| IRInput | - OpenGL window and context creation with GLFW. <br> - Keyboard, mouse, and gamepad input events synchronized with game loop. <br> - Uncapped FPS mouse position for smooth rendering. |
| IRMath | - Isometric calculations for converting 3D positions to 2D isometric and 2D screen coordinates. <br> - GLM wrappers for vector math, RBG to HSV color conversion, and more. <br> - Easing functions for simple animations. <br>  |
| IRProfile | - Separated logging sinks for the engine and user creations. |
| IRRender | - Meshless voxel rendering using compute shaders to write 3D voxels to 2D isometric canvases. <br> - Fixed orthographic isometric view for rendering voxels removing the need for raymarching. <br>  - Interpolated pixel scrolling for a smooth pixel-art camera. <br> - Multiple voxel canvases allows for select game entities to be unlocked from voxel grid. |
| IRScript | - Provides a wrapper for common Lua C API functionality (WIP). <br> - Runtime configuration for engine such as window size, resolution, etc. (WIP). <br> - User can define entire implementation using just Lua files (Future). |
| IRTime | - Fixed FPS events for consistent number of update ticks per second. <br> - Uncapped FPS events with delta time for faster rendering updates. <br> - Constructable event pipelines using custom and built-in systems. |
| IRVideo | - MP4/H264 framebuffer capture is enabled via FFmpeg. <br> - Runtime toggle recording support (F9). |

## Usage

### Getting Started

1. Make a new folder under "creations" named after your new project.
```

├───creations
│   ├───default
|   ├───...
|   ├───your_new_creation

```
2. Create a new CMakeLists.txt in that directory.
```
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(YourNewCreation main.cpp)

target_link_libraries(YourNewCreation PUBLIC IrredenEngine)
```
3. Add your new project to the creation's top-level CMakeLists.txt file.
```
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/default)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/...)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/your_new_creation) # Add your new project
```
4. Create a basic main.cpp file for use with the engine.
```
#include <irreden/ir_engine.hpp>

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: your-creation-here");

    IREngine::init(argc, argv);

    // Initialize entities, command, and systems here
    // ...

    IREngine::gameLoop();

    return 0;
}
```
5. Include your first components.
```
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
```
6. Create your first entity.
```
IRECS::createEntity{
    C_Position3D{0, 0, 0},
    C_VoxelSetNew{
        ivec3{1, 1, 1},
        Color{0, 255, 0, 255}
    }
};
```
7. Run your project.
    -   You should see a single green voxel in the center of the screen.

8. TODO: Check out the wiki for documentation on available features.

## Dependencies

| Name | Owning Module | Description/Usage  | Integration Details |
| ---- | -------| ------------------ | ------------------- |
| [RtAudio](https://github.com/thestk/rtaudio) | IRAudio | Realtime audio input/output. | [Details](/docs/text/dependencies/rtaudio.md)|
| [RtMidi](https://github.com/thestk/rtmidi) | IRAudio | Realtime MIDI input/output. | [Details](/docs/text/dependencies/rtmidi.md) |
| [GLFW](https://github.com/glfw/glfw) | IRInput | OpenGL window creation, inputs, and events. | [Details](/docs/text/dependencies/glfw.md) |
| [GLM](https://github.com/g-truc/glm) | IRMath | Mathematics operations for graphics programming. | [Details](/docs/text/dependencies/glm.md) |
| [EasyProfiler](https://github.com/yse/easy_profiler) | IRProfile | CPU profiler and GUI. | [Details](/docs/text/dependencies/easy_profiler.md) |
| [SpdLogger](https://github.com/gabime/spdlog) | IRProfile | Configurable and fast logging. | [Details](/docs/text/dependencies/spdlog.md) |
| [Fmt](https://github.com/fmtlib/fmt) | IRProfile | Formatting library used by SpdLog. | [Details](/docs/text/dependencies/fmt.md) |
| [Glad](https://github.com/Dav1dde/glad) | IRRender | OpenGL loading library. | [Details](/docs/text/dependencies/glad.md) |
| [OpenGL/glsl](https://www.khronos.org/opengl/) | IRRender | Graphics API/shading language. | [Details](/docs/text/dependencies/opengl.md) |
| [StbImage](https://github.com/nothings/stb/tree/master) | IRRender | Multi-format image loading. | [Details](/docs/text/dependencies/stb_image.md) |
| [MeshOptimizer](https://github.com/zeux/meshoptimizer) | IRRender | Mesh storage and size optimization. | [Details](/docs/text/dependencies/mesh_optimizer.md) |
| [Assimp](https://github.com/assimp/assimp) | IRRender | Multi-format 3D file loader. | [Details](/docs/text/dependencies/assimp.md) |
| [Lua](https://www.lua.org/manual/5.3/) | IRScript | Scripting language for C/C++. | [Details](/docs/text/dependencies/lua.md) |
| [FFMpeg](https://ffmpeg.org/) | IRVideo | Compression and encoding algorithms. | [Details](/docs/text/dependencies/ffmpeg.md)
<!-- -   GoogleTest -->

## Contributing

**I AM CURRENTLY VERY INTERESTED IN FEEDBACK REGARDING THE DESIGN OF THIS ENGINE.**
-   If you have a suggestion, create a new discussion post or email me at jakildev@gmail.com.

### Ways to contribute
1. Submit an issue on github.
2. Add, expand, or modify the engine's ***prefabs*** (built-in components, entities, systems, and commands).
3. Other work on the engine itself.
4. Publish an open-source project using the Irreden Engine.

### Style Guidelines
#### Logical
1. Prefer early exit over chained or nested branching statements.
2. Prefer unique pointers over shared pointers or C-style memory allocation.
3. Use raw pointers when there is no transfer of ownership of memory.

#### Non-logical
1. Private class member variables are prefixed with "m_".
2. Public class member variables are postfixed with "_".
3. User-defined types used as *components* are prefixed with "C_".
4. Compute shader files are prefixed with "c_".
5. Vertex shader files are prefixed with "v_".
6. Fragment shader files are prefixed with "f_".
7. Geometry shader files are prefixed with "g_".

### Development quality checks
The repository includes quality targets for formatting and linting C/C++ code.

1. Configure with CMake:
```
cmake -S . -B build
```
2. Check formatting:
```
cmake --build build --target format-check
```
3. Auto-format files:
```
cmake --build build --target format
```
4. Run lint checks (includes naming conventions such as `m_` private members and `_` public members):
```
cmake --build build --target lint
```

Requirements:
- `clang-format`
- `clang-tidy`

### CMake presets
Use presets for consistent local and CI commands:

1. Configure:
```
cmake --preset default
```
2. Build:
```
cmake --build --preset build-all
```
3. Run formatting and lint checks:
```
cmake --build --preset format-check
cmake --build --preset lint
```
4. Build and run tests:
```
cmake --build --preset tests
ctest --preset default-tests
```

<!-- ## Performance (TODO) -->
<!-- -   Highlight sections that perform well. -->
<!-- -   Identify bottlenecks and main areas for improvement. -->
