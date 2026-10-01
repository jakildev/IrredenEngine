local source = debug.getinfo(1, "S").source
local script_dir = source:match("^@(.*/)") or ""
local recipe = dofile(script_dir .. "../procedural_voxel_recipe.lua")
local voxels = recipe.build({ radius = 5, height = 10 })

assert(type(voxels) == "table")
assert(#voxels > 20)
for _, voxel in ipairs(voxels) do
    assert(type(voxel.x) == "number")
    assert(type(voxel.y) == "number")
    assert(type(voxel.z) == "number")
    assert(voxel.r >= 0 and voxel.r <= 255)
    assert(voxel.g >= 0 and voxel.g <= 255)
    assert(voxel.b >= 0 and voxel.b <= 255)
end

local applied = {}
local stub_set = {
    batch = function(self, fn)
        fn(self)
    end,
    setVoxel = function(_, x, y, z, color)
        applied[#applied + 1] = { x = x, y = y, z = z, color = color }
    end,
}
stub_set:batch(function(set)
    for _, voxel in ipairs(voxels) do
        set:setVoxel(voxel.x, voxel.y, voxel.z, { voxel.r, voxel.g, voxel.b, 255 })
    end
end)
assert(#applied == #voxels)

print("ALL PASS")
