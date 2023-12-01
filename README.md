# Irreden Engine

## Make a cool banner image (need font rendering first)

## About
The Irreden Engine is an isometric pixel-art voxel content and game engine.


## Design

### Modules
-   IRMath
-   IRRender

-   The Engine is seperated into "modules". Modules
    -   Contain functionality relating to a particular domain (rendering, profiling, etc).
    -   Build seperately as their own *static library*.
    -   Link to dependency modules and 3rd party libraries.
- Modules are used by the Engine and creations by including its "/irreden/ir_\<module_name\>.hpp" file.
    -   This file contains the module's public API and necessecary header files.

## Features
-
[Isometric Pixelatable Voxel Renderer](/docs/features/renderer.md)\
[Archetype-based entity-component-system](/docs/features/ecs.md)

## Dependencies

| Name | Owning Module | Description/Usage  | Integration Details |
| ---- | -------| ------------------ | ------------------- |
| [RtAudio](/docs/dependencies/) | IRAudio | | |
| [RtMidi](/docs/dependencies/) | IRAudio | | |
| [Glad](https://github.com/Dav1dde/glad) | IRRender | OpenGL loading library | [Details](/docs/text/dependencies/glad.md) |
| [OpenGL/glsl](https://www.khronos.org/opengl/) | IRRender | Graphics API/shading language | |
| [StbImage](/docs/dependencies/) | IRRender | | |
| [MeshOptimizer](ooo) | IRRender | | |
| [Assimp](https://github.com/assimp/assimp) | IRRender | | |
| [GLFW](https://github.com/glfw/glfw) | IRInput | | |
| [GLM](/docs/dependencies/glm.md) | IRMath | | |
| [EasyProfiler](https://github.com/yse/easy_profiler) | IRProfile | | |
| [SpdLogger](ooo) | IRProfile | | |
| [Fmt](/docs/dependencies/) | IRProfile | | |
| [Lua](ooo) | IRScript | | |
<!-- -   GoogleTest -->

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