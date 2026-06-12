-- IRPerfGrid preset: zoom=1, subdivision_mode=none, base_subdivisions=1
-- Minimal/fast config — no geometry subdivision, smallest zoom.
-- Use for isolating raw voxel-upload or update cost without subdivision
-- overhead, or as a quick smoke cell (~15 s at 300 frames).
perf_grid = {
    zoom = 1,
    subdivision_mode = "none",
    base_subdivisions = 1,
}
