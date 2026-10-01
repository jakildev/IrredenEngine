local Recipe = {}

function Recipe.build(params)
    local radius = params.radius or 8
    local height = params.height or 14
    local center = radius + 4
    local inner2 = (radius - 2) * (radius - 2)
    local outer2 = radius * radius
    local voxels = {}

    for x = center - radius, center + radius do
        for y = center - radius, center + radius do
            local dx = x - center
            local dy = y - center
            local distance2 = dx * dx + dy * dy
            if distance2 >= inner2 and distance2 <= outer2 then
                voxels[#voxels + 1] = { x = x, y = y, z = height - 1, r = 70, g = 190, b = 235 }
            end
        end
    end

    for z = 3, height - 2 do
        local spread = math.floor((height - z) / 4)
        voxels[#voxels + 1] = { x = center, y = center, z = z, r = 105, g = 210, b = 120 }
        voxels[#voxels + 1] = { x = center + spread, y = center, z = z, r = 80, g = 175, b = 105 }
        voxels[#voxels + 1] = { x = center, y = center - spread, z = z, r = 80, g = 175, b = 105 }
    end

    return voxels
end

return Recipe
