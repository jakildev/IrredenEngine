# System: function-local `static` deviations

Render system headers that still use `function-local static` for
mutable system state instead of the canonical `SystemParams` pattern,
per the rule in `engine/system/CLAUDE.md` "Don't use function-local
`static` for system state". Migration tracked in T-065.

- `engine/prefabs/irreden/render/systems/system_trixel_to_trixel.hpp`
- `engine/prefabs/irreden/render/systems/system_lighting_to_trixel.hpp`
- `engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp`
- `engine/prefabs/irreden/render/systems/system_build_occupancy_grid.hpp`
- `engine/prefabs/irreden/render/systems/system_fog_to_trixel.hpp`
- `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp`
- `engine/prefabs/irreden/render/systems/system_text_to_trixel.hpp`
- `engine/prefabs/irreden/render/systems/system_compute_sun_shadow.hpp`
- `engine/prefabs/irreden/render/systems/system_sprites_to_screen.hpp`
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp`
- `engine/prefabs/irreden/render/systems/system_framebuffer_to_screen.hpp`
- `engine/prefabs/irreden/render/systems/system_compute_voxel_ao.hpp`
