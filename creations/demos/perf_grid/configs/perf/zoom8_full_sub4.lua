-- IRPerfGrid preset: zoom=8, subdivision_mode=full, base_subdivisions=4
-- Heavy benchmark — 4x geometry subdivisions amplify voxel-to-trixel
-- throughput cost relative to the zoom1 baseline. Use for before/after
-- comparison on subdivision or voxel-upload optimisations.
perf_grid = {
    zoom = 8,
    subdivision_mode = "full",
    base_subdivisions = 4,
}
