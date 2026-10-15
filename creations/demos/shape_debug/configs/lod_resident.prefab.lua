local prefab = dofile("assets/prefabs/flower.prefab.lua")
for _, part in ipairs(prefab.parts) do
    part.resident = true
end
return prefab
