-- IRPerfGrid preset: sparse scatter stress — 1³ voxels on a 2.0-spaced
-- lattice (1/8 volume density) with the per-cell diagonal wave that shears
-- neighboring cells apart. Reads as striped at cardinals and see-through
-- under yaw *by construction*; useful as a scatter / non-coherent-motion
-- stress where nothing on screen is a coherent surface. The default scene
-- (config.lua) is the solid contiguous block with the tear-free per-cell
-- wave.
perf_grid = {
    mode = "voxel_set",
    spacing = 2.0,
    wave_mode = "per_cell",
}
