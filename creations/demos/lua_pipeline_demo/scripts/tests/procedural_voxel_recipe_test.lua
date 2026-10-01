local recipe = dofile("scripts/procedural_voxel_recipe.lua")
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

print("ALL PASS")
