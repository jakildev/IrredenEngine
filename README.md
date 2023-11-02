# Irreden Engine

## Make a cool banner image (need font rendering first)

## About
The Irreden Engine is an isometric pixel-art voxel content and game engine.

## Design
-   Modules of engine are clearly seperated as individual static libraries.

## Features
-   Connects to an audio interface for to send and receive midi and *audio* data. TODO: audio.
[Isometric Pixelatable Voxel Renderer](/docs/features/renderer.md)\
[Archetype-based entity-component-system](/docs/features/ecs.md)

## Integrations and Dependencies

[Glad](/docs/dependencies/glad.md)
[GLFW](/docs/dependencies/glfw.md)
[GLM](/docs/dependencies/glm.md)
[OpenGL](/docs/dependencies/opengl.md)
[StbImage](/docs/dependencies/)
<!-- -   GoogleTest -->
[EasyProfiler](https://github.com/yse/easy_profiler)
[SpdLogger]()
[Fmt](/docs/dependencies/)
[RtAudio](/docs/dependencies/)
[RtMidi](/docs/dependencies/)
[Assimp](https://github.com/assimp/assimp)
[MeshOptimizer]()

## Build

### Prerequirsites

-   Windows OS
-   OpenGL 4.6 supported hardware.
-   MinGW (or other c/c++ compiler)
-   CMake

### Instructions

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


## Contributing
-   Submit pull requests directly to master.
[Style rules and guidelines](/docs/rules/style.md)\
[Opening a pull request](/docs/contributing/pull_requests.md)\
[Submitting a new issue](/docs/contributing/issues.md)

**I AM CURRENTLY VERY INTERESTED IN FEEDBACK REGARDING THE DESIGN OF THIS ENGINE.** If you have a suggestion, submit a new issue to discuss, or email me at jakildev@gmail.com.

-   You can request a new feature by opening up a issue with the feature request tag
-

- Requesting a new component:
    -   If you think a new component should be added to the engine (and thus added to the standard) you should submit a pull request with a new component file in assets/wip/components/\<new_component_name.hpp\>
        -   This can be worked on in code or left blank.
    -   Provide a reason why this new component is needed

## Limitations
-   Only builds for Windows.
-   Device must support OpenGL 4.6.
-   No IDE (may have one in the future).
-   Implementations must be written in c++.

## Contact
-   Join the discord [here](TODO: ).
-   Email me at jakildev@gmail.com.
-

## Noteworthy c++ concepts (not that one) in code
#### Type erasure

## FAQ
### See here.

### [TODO](/irreden-engine/docs/todo.md)

### Things to consider...
-   Should MORE of the engine's logic be part of the entity component system?