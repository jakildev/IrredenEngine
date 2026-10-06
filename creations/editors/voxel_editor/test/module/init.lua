-- Neutral fixture for IRVoxelEditor --module: three components, one recipe and
-- one panel, so every registration path the editor hosts is exercised.

IRComponent.register("TestModuleTag", {
    weight = 1,
    offset = { type = "vec3" },
})

-- More fields than one page of the COMPONENTS field area, which lists them by
-- name.
IRComponent.register("TestModuleWide", {
    a = 1,
    b = 1,
    c = 1,
    d = 1,
    e = 1,
    f = 1,
    g = 1,
})

-- Field names a table constructor cannot spell bare: a reserved word and a
-- punctuated name.
IRComponent.register("TestModuleKeys", {
    ["end"] = 1,
    ["max-value"] = 1,
})

-- A 1x1 column standing on the seeded ground plane (local z == size.z - 1)
-- at the set's centre; it grows toward -z, the editor's "up".
IREditor.registerRecipe(
    "test_column",
    {
        { name = "height", type = IREditor.ParamType.INT, default = 5, min = 1, max = 8 },
    },
    function(values, size)
        local cx = math.floor(size.x / 2)
        local cy = math.floor(size.y / 2)
        local cells = {}
        for i = 1, values.height do
            cells[#cells + 1] = { cx, cy, size.z - 1 - i, { r = 220, g = 140, b = 60 } }
        end
        return cells
    end
)

IREditor.registerPanel("TestModulePanel", function(x, y, w, h)
    IRGui.makeLabel(x + 4, y + 18, "MODULE LOADED")
end)
