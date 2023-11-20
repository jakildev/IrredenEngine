# Renderer
OpenGL 4.6
The rendering pipeline consists of 3 main steps that result in a pixelated isometric rendering of 3D voxels.
 Takes a unique approach to rendering voxels with a fixed isometric perspective. Instead of using ray marching to render voxels to the screen,
fixed voxel grid
ray looking?

### Rendering Pipeline

#### 1. [System](/irreden-engine/src/game_systems/system_rendering_single_voxel_to_canvas.hpp) [voxels](/irreden-engine/src/game_entities/entity_single_voxel.hpp) to [canvases](/irreden-engine/src/game_entities/entity_triangle_canvas.cpp)

#### 2. [System](/irreden-engine/src/game_systems/system_rendering_canvas_to_framebuffer.hpp) [voxels](/irreden-engine/src/game_entities/entity_single_voxel.hpp) to [canvases](/irreden-engine/src/game_entities/entity_triangle_canvas.cpp)

#### 1. [System](/irreden-engine/src/game_systems/system_rendering_single_voxel_to_canvas.hpp) [voxels](/irreden-engine/src/game_entities/entity_single_voxel.hpp) to [canvases](/irreden-engine/src/game_entities/entity_triangle_canvas.cpp)

#### 2. Canvas to framebuffer
**Files:**

#### 3. Framebuffer to screen
**Files:**

## TODO:
-   Levels of details for texturing over voxels