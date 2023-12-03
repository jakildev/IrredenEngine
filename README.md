# Irreden Engine

## Make a cool banner image (need font rendering first)

## About
The Irreden Engine is an isometric pixel-art voxel content and game engine.

## Build

### Prerequirsites
-   Windows OS
-   OpenGL 4.6 supported hardware.
-   MinGW (or other c/c++ compiler)
-   CMake

### Instructions
-   Clone the repository
-   Configure cmake
-   Build all targets
-   Run the default project

## Modules and Key Features
| Module | Features |
| ------ | -------- |
| IRAudio | - Probe and connect to audio devices including audio interfaces<br> - Send and receive midi messages to create integraded sequences and CC parameters tied to game parameters<br> - Process realtime audio stream from audio devices (WIP) |
| IRCommand | - Lambda defined commands triggered by keyboard, mouse, controller, midi note, or midi cc message events |
| IRECS | - Archetype-based entity-component-system in which components are stored across entities in memory grouped by archetype<br> - Lambda-defined systems that operate on entities with specified components/relations <br> - Heirarchical relations and storage for memory efficient breadth-first updating of components <br> - Ability to create entities in batches with lambdas defining the initalization of components
| IRInput | - OpenGL window and context creation with GLFW<br> - Keyboard, mouse, and gamepad input events syncronized with game loop<br> - Uncapped FPS mouse position for smooth rendering|
| IRMath | - GLM wrappers for vector math, RBG to HSV color conversion, and more<br> - Easing functions for simple animations<br> - Specialized isometric calculations for converting 3D positions to 2D isometric and 2D screen coordinates |
| IRProfile | - Seperated color-coded logging sinks for the engine, user creations, and OpenGL API calls. |
| IRRender | - Fixed orthographic isometric view for rendering voxels allowing for marchless ray rendering<br> - Meshless voxel rendering using compute shaders to write 3D voxels to 2D isometric canvases <br> - Interpolated pixel scrolling for a smooth pixel-art camera <br> - Multiple voxel canvases allows for select game entities (players, etc) to be unlocked from voxel grid |
| IRScript | - Provides a wrapper for Lua C API (WIP)<br> - Runtime configuration for engine such as window size, resolution, etc (WIP) <br> - User can define entire implementation using just Lua files (Future) |
| IRTime | - Fixed update and unfixed events for consistent update loop and uncapped rendering, audio processing, etc.<br> - Delta time unique for each event |
| IRVideo | |
| Misc | |
## Dependencies

| Name | Owning Module | Description/Usage  | Integration Details |
| ---- | -------| ------------------ | ------------------- |
| [RtAudio](https://github.com/thestk/rtaudio) | IRAudio | Realtime audio input/output | [Details](/docs/text/dependencies/rtaudio.md)|
| [RtMidi](https://github.com/thestk/rtmidi) | IRAudio | Realtime MIDI input/output | [Details](/docs/text/dependencies/rtmidi.md) |
| [GLFW](https://github.com/glfw/glfw) | IRInput | OpenGL window creation, inputs, and events | [Details](/docs/text/dependencies/glfw.md) |
| [GLM](https://github.com/g-truc/glm) | IRMath | Mathametics operations for graphics programming | [Details](/docs/text/dependencies/glm.md) |
| [EasyProfiler](https://github.com/yse/easy_profiler) | IRProfile | CPU profiler and GUI | [Details](/docs/text/dependencies/easy_profiler.md) |
| [SpdLogger](https://github.com/gabime/spdlog) | IRProfile | Configurable logging library | [Details](/docs/text/dependencies/spdlog.md) |
| [Fmt](https://github.com/fmtlib/fmt) | IRProfile | Formatting library used by SpdLog | [Details](/docs/text/dependencies/fmt.md) |
| [Glad](https://github.com/Dav1dde/glad) | IRRender | OpenGL loading library | [Details](/docs/text/dependencies/glad.md) |
| [OpenGL/glsl](https://www.khronos.org/opengl/) | IRRender | Graphics API/shading language | [Details](/docs/text/dependencies/opengl.md) |
| [StbImage](https://github.com/nothings/stb/tree/master) | IRRender | Image loader | [Details](/docs/text/dependencies/stb_image.md) |
| [MeshOptimizer](https://github.com/zeux/meshoptimizer) | IRRender | Mesh storage and size optimization | [Details](/docs/text/dependencies/mesh_optimizer.md) |
| [Assimp](https://github.com/assimp/assimp) | IRRender | 3D file loader | [Details](/docs/text/dependencies/assimp.md) |
| [Lua](https://www.lua.org/manual/5.3/) | IRScript | Scripting language for C/C++ | [Details](/docs/text/dependencies/lua.md) |
| [FFMpeg](https://ffmpeg.org/) | IRVideo | Compression and encoding algorithms | [Details](/docs/text/dependencies/ffmpeg.md)
<!-- -   GoogleTest -->


## Design

## Licensing
This project is under the [MIT License](/docs/usage/licensing.md).\
It relies on other open-source dependencies as described in [dependencies](#dependencies).\
More details can be found [here](/docs/usage/licensing.md).


## Usage

<!-- ### Navigating the Engine
The engine is broken up into modules. Each module contains the following directories (when applicable):

-   **components:** Game components associated with this module
-   **entities:** Game entities, also known as prefabs, associated with this module
-   **include:** All include files for the module, including associated third-party files
-   **lib:** Precompiled binaries for third party libraries.
-   **patches:** Patch files for third-party packages pulled in during build.
-   **scripts:** Lua/python scripts associated with the module.
-   **shaders:** GLSL shader files used for rendering pipeline and GPU compute.
-   **src:** Main source files composing the module,
-   **systems:** Game systems associated with the module. -->

### Project Setup
[Project Setup](/docs/usage/project_setup.md)

### Building Your Project
When building your project, you should note the available systems from each of the modules, as well as create your own, to create your update, input, and rendering pipelines. You shold use the availble prefab entities, and create new entities with the supplied components. You should also write your own components to create unique game logic.

### Coordinate System (move somewhere else)
-   2D Screen coordinates
-   2D Isometric coordinates
-   3D World space coordinates

## Contributing
-   Submit pull requests directly to master.

[Style rules and guidelines](/docs/rules/style.md)\
[Opening a pull request](/docs/text/contributing/pull_requests.md)\
[Submitting a new issue](/docs/contributing/issues.md)
[Forking this repository]()

**I AM CURRENTLY VERY INTERESTED IN FEEDBACK REGARDING THE DESIGN OF THIS ENGINE.** If you have a suggestion, submit a new issue to discuss, or email me at jakildev@gmail.com.

-   You can request a new feature by opening up a issue with the feature request tag
-

- Requesting a new component:
    -   If you think a new component should be added to the engine (and thus added to the standard) you should submit a pull request with a new component file in assets/wip/components/\<new_component_name.hpp\>
        -   This can be worked on in code or left blank.
    -   Provide a reason why this new component is needed

## Performance
-   Highlight sections that perform well
-   Identify bottlenecks and main areas for improvement

## Limitations
-   Only builds for Windows.
-   Device must support OpenGL 4.6.
-   No IDE (may have one in the future).
-   Implementations must be written in c++.


## FAQ
### See here.

### [TODO](/irreden-engine/docs/todo.md)