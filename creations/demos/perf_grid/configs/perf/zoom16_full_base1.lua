-- IRPerfGrid preset: zoom=16, subdivision_mode=full, base_subdivisions=1
-- Extreme-zoom / cull-audit config — stresses visibility-cull and
-- occlusion-grid rebuild at the edge of the trixel canvas zoom range.
-- Use when auditing render-cull or LOD behaviour at high zoom.
perf_grid = {
    zoom = 16,
    subdivision_mode = "full",
    base_subdivisions = 1,
}
