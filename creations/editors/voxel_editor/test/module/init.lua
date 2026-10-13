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
    IRGui.makeLabel(x + 4, y + 32, "FIELD ROW")
end)

IREditor.registerSession("test_parts", function(session)
    local size = session:sceneSize()
    local center = {
        x = math.floor(size.x / 2),
        y = math.floor(size.y / 2),
        z = math.floor(size.z / 2),
    }
    local blob_front = { center.x, center.y, center.z - 2 }
    local painted = { center.x, center.y, center.z - 3 }

    session:segment("bake")
    session:addVoxelPart()
    session:bakeSphere(2)
    session:expectPartOccupancy(0, blob_front, true, "blob_baked")

    session:segment("before_click")
    session:expectPartOccupancy(0, painted, false, "before_click_empty")
    session:segment("click")
    session:click(painted)
    session:expectPartOccupancy(0, painted, true, "clicked_cell_filled")

    session:segment("array_and_band")
    session:applyRadialArray(2)
    session:dragLodSlider(IREditor.SliderTarget.LOD_FINE, 3)
    session:dragLodSlider(IREditor.SliderTarget.LOD_TIER, 0)
    session:expectPartCount(3, "three_parts_before_save")
    session:expectPartGated(2, true, 0, "selected_copy_band_set")

    session:segment("save")
    session:save()
    session:segment("clear")
    session:clearEntityScene()
    session:segment("reload")
    session:reload()
    session:expectPartOccupancy(0, painted, true, "reload_occupancy")
    session:expectPartCount(3, "reload_part_count")
    session:expectPartGated(2, true, 0, "reload_band")
end)

IREditor.registerSession("test_single", function(session)
    local size = session:sceneSize()
    local cell = { math.floor(size.x / 2), math.floor(size.y / 2), size.z - 2 }
    session:segment("place")
    session:click(cell)
    session:expectOccupancy(cell, true, "single_cell_placed")
    session:segment("save")
    session:save()
end)

IREditor.registerSession("test_unreachable", function(session)
    session:click({ 0, 0, 0 })
end)
