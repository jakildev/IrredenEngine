-- Voxel titles: world-space text made of voxel letters that face the camera.
-- IRText.createVoxel builds each letter on the lattice plane the camera sees
-- head-on, so the word reads upright and left to right; the path decides
-- where the letters stand. A heading of 3*pi/4 (world (-1, 1, 0)) is
-- screen-horizontal at the default camera yaw; the default path runs along
-- world +Y and rises to the right. Contract: docs/design/voxel-text.md.

local function title(text, opts)
    local root, letters = IRText.createVoxel(text, opts)
    print("[voxel-title] letters=" .. letters)
    return root
end

title("IRREDEN ENGINE", {
    origin = { 113, -113, 0 },
    color = Color.new(235, 200, 90, 255),
    scale = 2,
    path = 3 * math.pi / 4,
})

title("VOXEL TEXT", {
    origin = { 13, -183, 0 },
    color = Color.new(110, 200, 235, 255),
})
